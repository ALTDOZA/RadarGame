#include <windows.h>
#include <tchar.h>
#include <gdiplus.h>
#include <CommCtrl.h>
#include <cmath>
#include <objbase.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdlib> 

#include "Point.h"
#include "GameConfig.h"
#include "Missile.h"
#include "Launcher.h"
#include "Radar.h"
#include "SimulationState.h"
#include "MissileLog.h"

// Глобальные константы и переменные
const wchar_t CLASS_NAME[] = L"RadarSimWindowClass";
const wchar_t WINDOW_TITLE[] = L"Radar Simulation";
const UINT_PTR IDT_SIMULATION_TIMER = 1;
const UINT TIMER_INTERVAL_MS = 30;
#define IDC_BUTTON_RESTART 101
#define IDC_BUTTON_EXIT 102

// Глобальные объекты
CRITICAL_SECTION g_cs;
SimulationState g_simulationState; // Определение глобального объекта
// GameConfig g_config; // Определяется в GameConfig.cpp

RECT g_windowedRect = { 0 }; // Сохраняем размеры и положение окна в оконном режиме
bool g_isFullscreen = false;

// GDI+
Gdiplus::GdiplusStartupInput gdiplusStartupInput;
ULONG_PTR gdiplusToken = 0;
Gdiplus::Image* g_pImageBackground = nullptr;


void GlobalCriticalSectionCleanup() {
    DeleteCriticalSection(&g_cs);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void ToggleFullscreen(HWND hWnd)
{
    // Получаем текущие стили окна
    DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);

    if (g_isFullscreen) // ЕСЛИ сейчас ПОЛНОЭКРАННЫЙ режим -> Переключаемся в ОКОННЫЙ
    {
        // 1. Восстанавливаем старые стили окна (добавляем обратно WS_OVERLAPPEDWINDOW)
        dwStyle |= WS_OVERLAPPEDWINDOW; // Добавляем стили рамки, заголовка и т.п.
        dwStyle &= ~WS_POPUP; // Удаляем стиль полноэкранного POPUP

        SetWindowLong(hWnd, GWL_STYLE, dwStyle); // Устанавливаем новые стили

        // 2. Восстанавливаем старые размеры и положение окна в оконном режиме
        SetWindowPos(hWnd, NULL,
            g_windowedRect.left, g_windowedRect.top,
            g_windowedRect.right - g_windowedRect.left,
            g_windowedRect.bottom - g_windowedRect.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED); // SWP_FRAMECHANGED заставляет Windows перерисовать рамки/заголовок

        g_isFullscreen = false; // Обновляем флаг состояния
    }
    else // ЕСЛИ сейчас ОКОННЫЙ режим -> Переключаемся в ПОЛНОЭКРАННЫЙ
    {
        // Получаем информацию о мониторе, на котором сейчас находится окно
        // Это важно для правильного получения размеров БЕЗ ТАСКБАРА.
        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(mi) }; // Структура для информации о мониторе
        GetMonitorInfo(hMonitor, &mi); // Получаем информацию

        // 1. Сохраняем текущие размеры окна В ОКОННОМ режиме, если еще не сохранили
        // (Это должно быть уже сделано в WM_CREATE, но перепроверка не помешает)
        if (GetWindowRect(hWnd, &g_windowedRect)) // GetWindowRect возвращает true при успехе
        {
            // Размеры окна успешно сохранены.
        }
        else {
            // Handle error, couldn't get window rect (optional)
        }


        // 2. Устанавливаем полноэкранные стили окна (WS_POPUP, без рамки и заголовка)
        dwStyle &= ~WS_OVERLAPPEDWINDOW; // Удаляем стандартные стили рамки, заголовка и т.п.
        dwStyle |= WS_POPUP; // Добавляем стиль полноэкранного всплывающего окна (без рамки/заголовка)

        SetWindowLong(hWnd, GWL_STYLE, dwStyle); // Устанавливаем новые стили


        // 3. Устанавливаем размеры и положение окна на весь экран
        // Используем rcMonitor.rcMonitor (рабочая область монитора МИНУС таскбар)
        SetWindowPos(hWnd, HWND_TOPMOST, // Делаем окно верхним (чтобы оно не скрывалось за другими)
            mi.rcMonitor.left, mi.rcMonitor.top, // Перемещаем в (0,0) рабочего стола монитора
            mi.rcMonitor.right - mi.rcMonitor.left, // Ширина равна ширине рабочей области
            mi.rcMonitor.bottom - mi.rcMonitor.top, // Высота равна высоте рабочей области
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED); // SWP_FRAMECHANGED пересчитает неклиентскую область (которой уже нет, но полезно)

        g_isFullscreen = true; // Обновляем флаг состояния
    }

    // Важно запросить перерисовку после изменения размеров и стилей окна
    InvalidateRect(hWnd, NULL, TRUE); // TRUE = стереть фон перед перерисовкой (важно при смене рамки/стиля)
}




// Оконная процедура
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
    {
        // Загрузка конфигурации
        if (!g_config.loadFromFile("radar_config.txt")) {
            PostQuitMessage(1);
            return -1;
        }

        // Загрузка фона
        g_pImageBackground = Gdiplus::Image::FromFile(L"background.png");
        if (g_pImageBackground && g_pImageBackground->GetLastStatus() != Gdiplus::Ok) {
            MessageBox(hWnd, L"Не удалось загрузить фоновое изображение (background.png).", L"Предупреждение", MB_OK | MB_ICONWARNING);
            delete g_pImageBackground; g_pImageBackground = nullptr;
        }

        // Инициализация симуляции
        g_simulationState.initialize(g_config, hWnd);

        // Установка таймера
        if (SetTimer(hWnd, IDT_SIMULATION_TIMER, TIMER_INTERVAL_MS, NULL) == 0) {
            MessageBox(hWnd, L"Не удалось создать таймер симуляции!", L"Ошибка", MB_OK | MB_ICONERROR);
            PostQuitMessage(1);
            return -1;
        }

        // Создание кнопки
        INITCOMMONCONTROLSEX iccx;
        iccx.dwSize = sizeof(INITCOMMONCONTROLSEX);
        iccx.dwICC = ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&iccx);
        int buttonWidth = 120;
        int buttonHeight = 30;
        int buttonX = 10;
        int padding = 10;
        int restartButtonX = 10;
        int restartButtonY = 250;
        int buttonY = 40 + 10 * 20 + 10; 

        HWND hButtonRestart = CreateWindow(
            L"BUTTON", L"Начать заново",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            buttonX, buttonY, buttonWidth, buttonHeight,
            hWnd, (HMENU)IDC_BUTTON_RESTART, GetModuleHandle(NULL), NULL
        );
        int exitButtonX = restartButtonX + buttonWidth + padding; 
        int exitButtonY = restartButtonY;

        HWND hButtonExit = CreateWindow(
            L"BUTTON", L"Выйти",         
            WS_TABSTOP | WS_VISIBLE | WS_CHILD, 
            exitButtonX, exitButtonY, 
            buttonWidth, buttonHeight,
            hWnd,                
            (HMENU)IDC_BUTTON_EXIT,  
            GetModuleHandle(NULL), NULL
        );
    GetWindowRect(hWnd, &g_windowedRect);
    g_isFullscreen = false;
    }
    break;

    case WM_TIMER:
        if (wParam == IDT_SIMULATION_TIMER) {
            float dt = TIMER_INTERVAL_MS / 1000.0f;
            g_simulationState.update(dt, g_config);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;

    case WM_SIZE:
        InvalidateRect(hWnd, NULL, FALSE);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BUTTON_RESTART && HIWORD(wParam) == BN_CLICKED) {
            g_simulationState.reset(g_config);
            EnableWindow(GetDlgItem(hWnd, IDC_BUTTON_RESTART), TRUE);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        else if (LOWORD(wParam) == IDC_BUTTON_EXIT && HIWORD(wParam) == BN_CLICKED) {
            SendMessage(hWnd, WM_CLOSE, 0, 0);
        }
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int winWidth = rcClient.right - rcClient.left;
        int winHeight = rcClient.bottom - rcClient.top;

        // Двойная буферизация
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, winWidth, winHeight);
        HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

        // Рисование фона GDI+
        {
            Gdiplus::Graphics graphics(hdcMem);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            if (g_pImageBackground && g_pImageBackground->GetLastStatus() == Gdiplus::Ok) {
                graphics.DrawImage(g_pImageBackground, 0, 0, winWidth, winHeight);
            }
            else {
                HBRUSH hBrushBg = CreateSolidBrush(RGB(20, 20, 40));
                FillRect(hdcMem, &rcClient, hBrushBg);
                DeleteObject(hBrushBg);
            }
        }

        // Рисование симуляции
        SetBkMode(hdcMem, TRANSPARENT);
        g_simulationState.draw(hdcMem, &rcClient, g_config);

        // Копирование буфера на экран
        BitBlt(hdc, 0, 0, winWidth, winHeight, hdcMem, 0, 0, SRCCOPY);

        // Очистка буферизации
        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);

        EndPaint(hWnd, &ps);
    }
    break;


    case WM_KEYDOWN:
    {
        // wParam содержит код виртуальной клавиши
        if (wParam == VK_F11) // VK_F11 - код виртуальной клавиши F11
        {
            // Вызываем функцию-переключатель полноэкранного режима
            ToggleFullscreen(hWnd);
        }
    }
    break;

    case WM_DESTROY:
        KillTimer(hWnd, IDT_SIMULATION_TIMER);
        g_simulationState.shutdown(); // Важно вызвать перед удалением g_cs
        if (g_pImageBackground) {
            delete g_pImageBackground; g_pImageBackground = nullptr;
        }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}


int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    InitializeCriticalSection(&g_cs);

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        // При сбое COM init, очистка CS все равно произойдет через atexit.
        MessageBox(NULL, L"Не удалось инициализировать COM!", L"Ошибка", MB_OK | MB_ICONERROR);
        return 1;
    }
    Gdiplus::GdiplusStartupInput gdiplusStartupInput_local;
    ULONG_PTR gdiplusToken_local;
    Gdiplus::Status gdiStatus = Gdiplus::GdiplusStartup(&gdiplusToken_local, &gdiplusStartupInput_local, NULL);
    if (gdiStatus != Gdiplus::Ok) {
        MessageBox(NULL, L"Не удалось инициализировать GDI+!", L"Ошибка", MB_OK | MB_ICONERROR);
        return 1;
    }
    gdiplusToken = gdiplusToken_local;
    std::srand(static_cast<unsigned int>(std::time(0)));
    WNDCLASSEX wc = { };
    wc.cbSize = sizeof(WNDCLASSEX);       // Обязательно: размер структуры.
    wc.lpfnWndProc = WndProc;             // Указываем на НАШУ оконную процедуру обработчика сообщений.
    wc.hInstance = hInstance;             // Передаем дескриптор экземпляра приложения.
    wc.lpszClassName = CLASS_NAME;       // Используем имя класса, которое мы определили.
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); // Устанавливаем стандартный курсор-стрелку.
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"Не удалось зарегистрировать класс окна!", L"Ошибка", MB_ICONERROR | MB_OK);
        return 1;
    }
    HWND hWnd = CreateWindowEx(0, CLASS_NAME, WINDOW_TITLE, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);
    if (hWnd == NULL) {
        MessageBox(NULL, L"Не удалось создать окно!", L"Ошибка", MB_ICONERROR | MB_OK);
        return 1;
    }
    g_simulationState.initialize(g_config, hWnd);
    ShowWindow(hWnd, nShowCmd);
    UpdateWindow(hWnd);
    ToggleFullscreen(hWnd);

    if (SetTimer(hWnd, IDT_SIMULATION_TIMER, TIMER_INTERVAL_MS, NULL) == 0) {
        MessageBox(NULL, L"Не удалось установить таймер симуляции!", L"Fatal Error", MB_OK | MB_ICONERROR);
        DestroyWindow(hWnd); // Закрыть окно (вызовет WM_DESTROY).
        return 1; // Выход.
    }
    MSG msg = { };
    int return_code = 0;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return_code = (int)msg.wParam;

    Gdiplus::GdiplusShutdown(gdiplusToken); 
    CoUninitialize();                       
    DeleteCriticalSection(&g_cs);

    return return_code;

}
