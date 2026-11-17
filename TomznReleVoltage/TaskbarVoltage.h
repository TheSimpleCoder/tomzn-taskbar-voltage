#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <atomic>
#include <string>

class TaskbarVoltage final
{
public:
	TaskbarVoltage();
	~TaskbarVoltage();
	TaskbarVoltage(const TaskbarVoltage&) = delete;
	TaskbarVoltage& operator=(const TaskbarVoltage&) = delete;

	bool Create(HINSTANCE instance);
	void StartMonitoring();
	void SetVoltage(std::wstring voltage);
	HWND GetHandle() const noexcept { return window_; }

private:
	static constexpr UINT PositionTimerId = 1;
	static constexpr UINT VoltageMessage = WM_APP + 1;
	static constexpr int WindowBaseWidth = 90;
	static constexpr int WindowBaseHeight = 30;

	static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
	void UpdatePosition();
	void Render();
	void SetVoltageOnUi(std::wstring voltage);

	void CreateSurface(int width, int height);
	void DestroySurface();

	std::atomic<HWND> window_{ nullptr };
	HWND tooltip_ = nullptr;
	std::wstring voltage_ = L"--.- V";
	double numericVoltage_ = 0.0;
	bool hasNumericVoltage_ = false;
	bool isCritical_ = false;
	bool hovered_ = false;
	bool hasPosition_ = false;
	RECT placed_{};
	int currentWidth_ = WindowBaseWidth;
	int currentHeight_ = WindowBaseHeight;

	ID2D1Factory* d2dFactory_ = nullptr;
	IDWriteFactory* dwriteFactory_ = nullptr;
	IDWriteTextFormat* textFormat_ = nullptr;
	ID2D1DCRenderTarget* dcRenderTarget_ = nullptr;

	HDC surfaceDc_ = nullptr;
	HBITMAP surfaceBmp_ = nullptr;
	HGDIOBJ oldBmp_ = nullptr;
	void* surfaceBits_ = nullptr;
	int surfaceWidth_ = 0;
	int surfaceHeight_ = 0;
};
