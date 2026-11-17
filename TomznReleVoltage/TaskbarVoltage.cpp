#include "TaskbarVoltage.h"

#include <CommCtrl.h>
#include <WindowsX.h>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <new>
#include <string>
#include <utility>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace
{
	constexpr wchar_t WindowClassName[] = L"TomznTaskbarVoltageWindow";
	constexpr UINT ExitMenuCommand = 1001;

	D2D1_COLOR_F D2dVoltageColor(const std::wstring& text)
	{
		const wchar_t* start = text.c_str();
		while (iswspace(*start))
			++start;

		std::wstring clean = start;
		for (wchar_t& ch : clean)
		{
			if (ch == L',')
				ch = L'.';
		}

		wchar_t* end = nullptr;
		const double voltage = wcstod(clean.c_str(), &end);
		if (end == clean.c_str() || !std::isfinite(voltage))
			return D2D1::ColorF(255.40f, 255.92f, 255.60f, 1.0f); // Не число (например, "--.- V") -> Красный

		// Пропускаем возможные пробелы и суффикс единицы измерения 'V' / 'В'
		while (iswspace(*end))
			++end;
		if (*end == L'V' || *end == L'v' || *end == L'В' || *end == L'в')
			++end;
		while (iswspace(*end))
			++end;
		if (*end != L'\0')
			return D2D1::ColorF(1.0f, 0.35f, 0.35f, 1.0f);

		// Диапазоны напряжений:
		// 215В - 229.9В: Зелёный (норма)
		// 200В - 214.9В или 230В - 244.9В: Оранжево-жёлтый (предупреждение)
		// <200В или >=245В: Красный (критично)
		if (voltage >= 215.0 && voltage < 230.0)
			return D2D1::ColorF(0.40f, 0.92f, 0.60f, 1.0f); // Зеленый
		if ((voltage >= 200.0 && voltage < 215.0) ||
			(voltage >= 230.0 && voltage < 245.0))
			return D2D1::ColorF(1.0f, 0.75f, 0.15f, 1.0f); // Оранжевый/Желтый
		return D2D1::ColorF(1.0f, 0.35f, 0.35f, 1.0f);     // Красный
	}
}

TaskbarVoltage::TaskbarVoltage()
{
	D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_);
	DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwriteFactory_));

	if (dwriteFactory_)
	{
		// Используем современный шрифт Segoe UI Variable Display (Windows 11) с откатом на Segoe UI
		HRESULT hr = dwriteFactory_->CreateTextFormat(
			L"Calibri",
			nullptr,
			DWRITE_FONT_WEIGHT_SEMI_LIGHT,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			21.0f,
			L"ru-RU",
			&textFormat_
		);
		if (FAILED(hr))
		{
			hr = dwriteFactory_->CreateTextFormat(
				L"Segoe UI",
				nullptr,
				DWRITE_FONT_WEIGHT_SEMI_BOLD,
				DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL,
				16.0f,
				L"ru-RU",
				&textFormat_
			);
		}

		if (textFormat_)
		{
			textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		}
	}

	if (d2dFactory_)
	{
		D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
		);
		d2dFactory_->CreateDCRenderTarget(&props, &dcRenderTarget_);
		if (dcRenderTarget_)
		{
			dcRenderTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
		}
	}
}

TaskbarVoltage::~TaskbarVoltage()
{
	DestroySurface();

	if (dcRenderTarget_)
	{
		dcRenderTarget_->Release();
		dcRenderTarget_ = nullptr;
	}
	if (textFormat_)
	{
		textFormat_->Release();
		textFormat_ = nullptr;
	}
	if (dwriteFactory_)
	{
		dwriteFactory_->Release();
		dwriteFactory_ = nullptr;
	}
	if (d2dFactory_)
	{
		d2dFactory_->Release();
		d2dFactory_ = nullptr;
	}
}

void TaskbarVoltage::CreateSurface(int width, int height)
{
	if (surfaceDc_ && surfaceWidth_ == width && surfaceHeight_ == height)
		return;

	DestroySurface();

	HDC screenDc = GetDC(nullptr);
	surfaceDc_ = CreateCompatibleDC(screenDc);
	ReleaseDC(nullptr, screenDc);

	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = -height; // Top-down DIB
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	surfaceBmp_ = CreateDIBSection(surfaceDc_, &bmi, DIB_RGB_COLORS, &surfaceBits_, nullptr, 0);
	if (surfaceBmp_)
	{
		oldBmp_ = SelectObject(surfaceDc_, surfaceBmp_);
		surfaceWidth_ = width;
		surfaceHeight_ = height;
	}
}

void TaskbarVoltage::DestroySurface()
{
	if (surfaceDc_)
	{
		if (oldBmp_)
		{
			SelectObject(surfaceDc_, oldBmp_);
			oldBmp_ = nullptr;
		}
		if (surfaceBmp_)
		{
			DeleteObject(surfaceBmp_);
			surfaceBmp_ = nullptr;
		}
		DeleteDC(surfaceDc_);
		surfaceDc_ = nullptr;
		surfaceBits_ = nullptr;
		surfaceWidth_ = 0;
		surfaceHeight_ = 0;
	}
}

bool TaskbarVoltage::Create(HINSTANCE instance)
{
	WNDCLASSEXW windowClass{};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = instance;
	windowClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
	windowClass.lpszClassName = WindowClassName;

	if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return false;

	const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
	if (!taskbar)
		return false;

	// Создаем как реальное дочернее окно панели задач (WS_CHILD) со стилем WS_EX_LAYERED
	// По аналогии с утилитой Busy, это дает собственную поверхность DWM DirectComposition
	window_ = CreateWindowExW(
		WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOPARENTNOTIFY,
		WindowClassName,
		L"TomznTaskbarVoltage",
		WS_CHILD | WS_CLIPSIBLINGS,
		0, 0, currentWidth_, currentHeight_,
		taskbar,
		nullptr,
		instance,
		this);
	if (!window_)
	{
		const DWORD error = GetLastError();
		wchar_t errorText[256]{};
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, error, 0, errorText, static_cast<DWORD>(std::size(errorText)), nullptr);
		OutputDebugStringW((L"CreateWindowExW failed: " + std::to_wstring(error) + L" " + errorText + L"\n").c_str());
		return false;
	}

	CreateSurface(currentWidth_, currentHeight_);

	INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES };
	InitCommonControlsEx(&controls);
	tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
		WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT, window_, nullptr, instance, nullptr);
	if (tooltip_)
	{
		TOOLINFOW tool{};
		tool.cbSize = sizeof(tool);
		tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		tool.hwnd = window_;
		tool.uId = reinterpret_cast<UINT_PTR>(window_.load());
		tool.lpszText = const_cast<LPWSTR>(L"Текущее напряжение");
		SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
	}

	return true;
}

void TaskbarVoltage::StartMonitoring()
{
	if (window_)
	{
		ShowWindow(window_, SW_SHOWNOACTIVATE);
		UpdatePosition();
		Render();
		SetTimer(window_, PositionTimerId, 100, nullptr);
	}
}

void TaskbarVoltage::SetVoltage(std::wstring voltage)
{
	const HWND target = window_.load();
	if (!target)
		return;

	auto* value = new (std::nothrow) std::wstring(std::move(voltage));
	if (!value)
		return;
	if (!PostMessageW(target, VoltageMessage, 0, reinterpret_cast<LPARAM>(value)))
		delete value;
}

LRESULT CALLBACK TaskbarVoltage::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	TaskbarVoltage* self = reinterpret_cast<TaskbarVoltage*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
		self = static_cast<TaskbarVoltage*>(create->lpCreateParams);
		self->window_ = window;
		SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TaskbarVoltage::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	case WM_SETCURSOR:
		SetCursor(LoadCursorW(nullptr, IDC_HAND));
		return TRUE;
	case WM_TIMER:
		if (wParam == PositionTimerId)
		{
			UpdatePosition();
			if (isCritical_)
				Render();
		}
		return 0;
	case VoltageMessage:
	{
		std::unique_ptr<std::wstring> value(reinterpret_cast<std::wstring*>(lParam));
		SetVoltageOnUi(std::move(*value));
		return 0;
	}
	case WM_MOUSEMOVE:
		if (!hovered_)
		{
			hovered_ = true;
			TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, window_, 0 };
			TrackMouseEvent(&tracking);
			Render();
		}
		return 0;
	case WM_MOUSELEAVE:
		hovered_ = false;
		Render();
		return 0;
	case WM_PAINT:
	{
		PAINTSTRUCT paint{};
		BeginPaint(window_, &paint);
		EndPaint(window_, &paint);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_CONTEXTMENU:
	{
		HMENU menu = CreatePopupMenu();
		if (menu)
		{
			AppendMenuW(menu, MF_STRING, ExitMenuCommand, L"Выход");
			TrackPopupMenu(menu, TPM_RIGHTBUTTON, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0, window_, nullptr);
			DestroyMenu(menu);
		}
		return 0;
	}
	case WM_COMMAND:
		if (LOWORD(wParam) == ExitMenuCommand)
		{
			DestroyWindow(window_);
			PostQuitMessage(0);
		}
		return 0;
	case WM_NCDESTROY:
	{
		const HWND destroyedWindow = window_;
		if (tooltip_)
		{
			DestroyWindow(tooltip_);
			tooltip_ = nullptr;
		}
		KillTimer(destroyedWindow, PositionTimerId);
		SetWindowLongPtrW(destroyedWindow, GWLP_USERDATA, 0);
		window_ = nullptr;
		return DefWindowProcW(destroyedWindow, message, wParam, lParam);
	}
	default:
		return DefWindowProcW(window_, message, wParam, lParam);
	}
}

void TaskbarVoltage::SetVoltageOnUi(std::wstring voltage)
{
	std::wstring clean = voltage;
	for (wchar_t& ch : clean)
	{
		if (ch == L',')
			ch = L'.';
	}
	wchar_t* end = nullptr;
	const double val = wcstod(clean.c_str(), &end);
	if (end != clean.c_str() && std::isfinite(val))
	{
		numericVoltage_ = val;
		hasNumericVoltage_ = true;
		isCritical_ = (numericVoltage_ <= 200.0 || numericVoltage_ >= 250.0);
	}
	else
	{
		numericVoltage_ = 0.0;
		hasNumericVoltage_ = false;
		isCritical_ = false;
	}

	voltage_ = std::move(voltage) + L" V";
	Render();
}

void TaskbarVoltage::UpdatePosition()
{
	const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
	if (!taskbar || !IsWindowVisible(taskbar))
	{
		hasPosition_ = false;
		ShowWindow(window_, SW_HIDE);
		return;
	}

	// Восстанавливаем родителя, если explorer перезапускался
	if (GetParent(window_) != taskbar)
	{
		SetParent(window_, taskbar);
		hasPosition_ = false;
	}

	RECT client{};
	if (!GetClientRect(taskbar, &client))
	{
		hasPosition_ = false;
		ShowWindow(window_, SW_HIDE);
		return;
	}

	// Учитываем масштаб DPI панели задач
	UINT dpi = GetDpiForWindow(taskbar);
	if (dpi == 0)
		dpi = 96;
	const float scale = dpi / 96.0f;
	currentWidth_ = static_cast<int>(std::round(WindowBaseWidth * scale));
	currentHeight_ = static_cast<int>(std::round(WindowBaseHeight * scale));

	const int clientWidth = client.right - client.left;
	const int clientHeight = client.bottom - client.top;
	const bool isVertical = (clientHeight > clientWidth);

	if (currentHeight_ > (isVertical ? clientWidth : clientHeight) - 6)
		currentHeight_ = (isVertical ? clientWidth : clientHeight) - 6;

	CreateSurface(currentWidth_, currentHeight_);

	// Ищем TrayNotifyWnd в панели задач
	const HWND tray = FindWindowExW(taskbar, nullptr, L"TrayNotifyWnd", nullptr);
	int trayAnchor = isVertical ? client.bottom : client.right;

	if (tray)
	{
		RECT trayScreen{};
		if (GetWindowRect(tray, &trayScreen))
		{
			POINT pts[2] = {
				{ trayScreen.left, trayScreen.top },
				{ trayScreen.right, trayScreen.bottom }
			};
			MapWindowPoints(nullptr, taskbar, pts, 2);
			if (isVertical)
			{
				if (pts[0].y > 50)
					trayAnchor = pts[0].y;
			}
			else
			{
				if (pts[0].x > 50)
					trayAnchor = pts[0].x;
			}
		}
	}

	int x = 0;
	int y = 0;
	if (isVertical)
	{
		x = (clientWidth - currentWidth_) / 2;
		y = trayAnchor - currentHeight_ - 8;
		if (y < 0)
			y = (clientHeight - currentHeight_) / 2;
	}
	else
	{
		x = trayAnchor - currentWidth_ - 8;
		y = (clientHeight - currentHeight_) / 2;
		if (x < 0)
			x = clientWidth - currentWidth_ - static_cast<int>(160 * scale);
	}

	RECT want = { x, y, x + currentWidth_, y + currentHeight_ };
	bool moved = (want.left != placed_.left || want.top != placed_.top ||
	              want.right != placed_.right || want.bottom != placed_.bottom);

	// КЛЮЧЕВОЙ МОМЕНТ (как в Busy):
	// Панель задач Explorer периодически поднимает свой XAML Island (Windows.UI.Composition.DesktopWindowContentBridge)
	// наверх в Z-order внутри Shell_TrayWnd. Если над нами появилось окно (covered),
	// мы возвращаем наше дочернее окно наверх вызовом SetWindowPos с HWND_TOP!
	bool covered = (GetWindow(window_, GW_HWNDPREV) != nullptr);
	bool notVisible = (IsWindowVisible(window_) == FALSE);

	if (moved || covered || notVisible || !hasPosition_)
	{
		placed_ = want;
		hasPosition_ = true;
		SetWindowPos(window_, HWND_TOP, want.left, want.top, currentWidth_, currentHeight_,
			SWP_NOACTIVATE | SWP_SHOWWINDOW);
		Render();
	}
}

void TaskbarVoltage::Render()
{
	if (!window_ || !surfaceDc_ || !dcRenderTarget_ || !textFormat_)
		return;

	RECT bindRect = { 0, 0, currentWidth_, currentHeight_ };
	if (FAILED(dcRenderTarget_->BindDC(surfaceDc_, &bindRect)))
		return;

	dcRenderTarget_->BeginDraw();

	// Прозрачный фон с минимальной альфой (1/255) для корректного hit-тестирования мышью
	dcRenderTarget_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f / 255.0f));

	D2D1_ROUNDED_RECT roundedRect;
	roundedRect.rect = D2D1::RectF(
		1.0f,
		1.0f,
		static_cast<float>(currentWidth_ - 1),
		static_cast<float>(currentHeight_ - 1));
	roundedRect.radiusX = 4.0f;
	roundedRect.radiusY = 4.0f;

	// Фон:
	// 1. При наведении мыши - полупрозрачная подсветка
	// 2. Если напряжение критическое (<= 200В или >= 250В) - плавное мягкое мигание
	if (hovered_)
	{
		// Полупрозрачная подсветка в стиле кнопок панели задач Windows 11/10
		D2D1_COLOR_F bgColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f);
		ID2D1SolidColorBrush* bgBrush = nullptr;
		if (SUCCEEDED(dcRenderTarget_->CreateSolidColorBrush(bgColor, &bgBrush)))
		{
			dcRenderTarget_->FillRoundedRectangle(&roundedRect, bgBrush);
			bgBrush->Release();
		}

		ID2D1SolidColorBrush* borderBrush = nullptr;
		if (SUCCEEDED(dcRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.18f), &borderBrush)))
		{
			dcRenderTarget_->DrawRoundedRectangle(&roundedRect, borderBrush, 1.0f);
			borderBrush->Release();
		}
	}
	else if (isCritical_)
	{
		// Медленная и мягкая синусоидальная пульсация (период 1.8 с)
		const ULONGLONG now = GetTickCount64();
		const float phase = static_cast<float>((now % 1800) / 1800.0);
		const float sinVal = 0.5f * (1.0f + std::sin(phase * 6.2831853f));

		// Мягкая неяркая прозрачность: от 0.03 до 0.18
		const float alpha = 0.03f + sinVal * 0.15f;

		D2D1_COLOR_F bgColor = D2D1::ColorF(0.95f, 0.18f, 0.18f, alpha);
		ID2D1SolidColorBrush* bgBrush = nullptr;
		if (SUCCEEDED(dcRenderTarget_->CreateSolidColorBrush(bgColor, &bgBrush)))
		{
			dcRenderTarget_->FillRoundedRectangle(&roundedRect, bgBrush);
			bgBrush->Release();
		}

		ID2D1SolidColorBrush* borderBrush = nullptr;
		if (SUCCEEDED(dcRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.35f, 0.35f, alpha * 1.3f), &borderBrush)))
		{
			dcRenderTarget_->DrawRoundedRectangle(&roundedRect, borderBrush, 1.0f);
			borderBrush->Release();
		}
	}

	// Текст напряжения
	D2D1_COLOR_F textColor = D2dVoltageColor(voltage_);
	ID2D1SolidColorBrush* textBrush = nullptr;
	if (SUCCEEDED(dcRenderTarget_->CreateSolidColorBrush(textColor, &textBrush)))
	{
		D2D1_RECT_F textRect = D2D1::RectF(0.0f, 0.0f, static_cast<float>(currentWidth_), static_cast<float>(currentHeight_));
		dcRenderTarget_->DrawTextW(
			voltage_.c_str(),
			static_cast<UINT32>(voltage_.size()),
			textFormat_,
			&textRect,
			textBrush,
			D2D1_DRAW_TEXT_OPTIONS_NONE
		);
		textBrush->Release();
	}

	if (FAILED(dcRenderTarget_->EndDraw()))
		return;

	SIZE size = { currentWidth_, currentHeight_ };
	POINT ptSrc = { 0, 0 };
	BLENDFUNCTION blend{};
	blend.BlendOp = AC_SRC_OVER;
	blend.SourceConstantAlpha = 255;
	blend.AlphaFormat = AC_SRC_ALPHA;

	UpdateLayeredWindow(
		window_,
		nullptr,
		nullptr,
		&size,
		surfaceDc_,
		&ptSrc,
		0,
		&blend,
		ULW_ALPHA
	);
}
