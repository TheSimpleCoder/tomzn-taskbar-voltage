#include "TaskbarVoltage.h"

#include <Windows.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace
{
	std::wstring QuoteArgument(const std::wstring& argument)
	{
		std::wstring quoted = L"\"";
		for (const wchar_t character : argument)
		{
			if (character == L'\"')
				quoted += L'\\';
			quoted += character;
		}
		quoted += L'\"';
		return quoted;
	}

	std::wstring ExecutableDirectory()
	{
		std::vector<wchar_t> path(32768);
		const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0 || length >= path.size())
			return {};
		std::wstring result(path.data(), length);
		const auto separator = result.find_last_of(L"\\/");
		if (separator == std::wstring::npos)
			return {};
		result.resize(separator + 1);
		return result;
	}

	bool StartVoltageReader(TaskbarVoltage& display, std::atomic<bool>& stopping)
	{
		const std::wstring script = ExecutableDirectory() + L"rele.py";
		if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			MessageBoxW(nullptr, (L"Не найден файл скрипта:\n" + script).c_str(),
				L"TomznReleVoltage", MB_OK | MB_ICONERROR);
			return false;
		}

		SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
		HANDLE readPipe = nullptr;
		HANDLE writePipe = nullptr;
		if (!CreatePipe(&readPipe, &writePipe, &security, 0))
		{
			MessageBoxW(nullptr, L"Не удалось создать канал для получения напряжения.",
				L"TomznReleVoltage", MB_OK | MB_ICONERROR);
			return false;
		}
		SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
		startup.wShowWindow = SW_HIDE;
		startup.hStdOutput = writePipe;
		startup.hStdError = writePipe;
		HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			&security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (nullInput == INVALID_HANDLE_VALUE)
		{
			CloseHandle(readPipe);
			CloseHandle(writePipe);
			MessageBoxW(nullptr, L"Не удалось открыть стандартный ввод для Python.",
				L"TomznReleVoltage", MB_OK | MB_ICONERROR);
			return false;
		}
		startup.hStdInput = nullInput;
		PROCESS_INFORMATION process{};
		std::wstring command = L"python.exe -u " + QuoteArgument(script);
		std::vector<wchar_t> mutableCommand(command.begin(), command.end());
		mutableCommand.push_back(L'\0');
		const BOOL started = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW, nullptr, ExecutableDirectory().c_str(), &startup, &process);
		CloseHandle(nullInput);
		CloseHandle(writePipe);
		if (!started)
		{
			CloseHandle(readPipe);
			const DWORD error = GetLastError();
			const std::wstring message = L"Не удалось запустить python.exe (ошибка " + std::to_wstring(error) + L"). Установите Python и tinytuya.";
			MessageBoxW(nullptr, message.c_str(), L"TomznReleVoltage", MB_OK | MB_ICONERROR);
			return false;
		}
		CloseHandle(process.hThread);

		std::thread reader([readPipe, processHandle = process.hProcess, &display, &stopping]()
		{
			std::string pending;
			char buffer[512];
			DWORD bytesRead = 0;
			while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
			{
				pending.append(buffer, bytesRead);
				std::size_t newline = std::string::npos;
				while ((newline = pending.find('\n')) != std::string::npos)
				{
					std::string line = pending.substr(0, newline);
					pending.erase(0, newline + 1);
					if (!line.empty() && line.back() == '\r')
						line.pop_back();
					constexpr char prefix[] = "VOLTAGE:";
					if (line.rfind(prefix, 0) == 0)
					{
						const std::string value = line.substr(sizeof(prefix) - 1);
						const int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
						if (count > 1)
						{
							std::wstring wide(static_cast<std::size_t>(count), L'\0');
							MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), count);
							wide.resize(static_cast<std::size_t>(count - 1));
							display.SetVoltage(std::move(wide));
						}
					}
					else if (!line.empty())
					{
						OutputDebugStringA((line + "\n").c_str());
					}
				}
			}
			if (!pending.empty())
				OutputDebugStringA(pending.c_str());
			CloseHandle(readPipe);
			WaitForSingleObject(processHandle, INFINITE);
			CloseHandle(processHandle);
			if (!stopping.load())
				OutputDebugStringW(L"Скрипт получения напряжения завершился.\n");
		});

		MSG message{};
		while (GetMessageW(&message, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		stopping.store(true);
		TerminateProcess(process.hProcess, 0);
		if (reader.joinable())
			reader.join();
		return true;
	}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
	TaskbarVoltage voltage;
	if (!voltage.Create(instance))
	{
		MessageBoxW(nullptr, L"Не удалось создать индикатор напряжения на панели задач.",
			L"TomznReleVoltage", MB_OK | MB_ICONERROR);
		return 1;
	}
	voltage.StartMonitoring();

	std::atomic<bool> stopping = false;
	return StartVoltageReader(voltage, stopping) ? 0 : 1;
}
