#include "Launcher.h" // Включаем заголовок класса Launcher
#include <windows.h>  // Для HDC, RGB, CreateSolidBrush, CreatePen, DeleteObject, SelectObject, Rectangle

// --- Конструктор по умолчанию ---
Launcher::Launcher() : pos({ 0.0f, 0.0f }), launcherId(-1) {}

// --- Конструктор с параметрами ---
// Инициализирует позицию и ID пусковой.
Launcher::Launcher(Point p, int id) : pos(p), launcherId(id) {}

// --- Метод отрисовки пусковой установки (Синий квадрат) ---
// Сигнатура должна соответствовать объявлению в Launcher.h
void Launcher::draw(HDC hdc, int winCenterX, int winCenterY) const { // <<< ИСПРАВЛЕНИЕ ОЧЕПЯТКИ в сигнатуре draw

    // Определяем цвет и размер квадратика (Синий, как на скриншоте)
    COLORREF launcherColor = RGB(0, 0, 255); // Ярко-синий
    int size_px = 15; // Размер стороны квадрата в пикселях. Подберите.

    // Рассчитываем экранные координаты центра пусковой установки
    int screenX = static_cast<int>(pos.x + winCenterX);
    int screenY = static_cast<int>(-pos.y + winCenterY); // Инверсия Y

    // Рассчитываем координаты углов квадрата
    int left = screenX - size_px / 2;
    int top = screenY - size_px / 2;
    int right = screenX + size_px - size_px / 2;
    int bottom = screenY + size_px - size_px / 2;

    // --- Создаем GDI объекты ---
    HBRUSH hBrush = CreateSolidBrush(launcherColor); // Кисть для заливки
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0)); // Перо для контура (черный)

    // --- Сохраняем текущие и выбираем наши ---
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    // --- Рисуем квадрат ---
    Rectangle(hdc, left, top, right, bottom);

    // --- Восстанавливаем старые ---
    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);

    // --- Удаляем созданные ---
    DeleteObject(hBrush);
    DeleteObject(hPen);
}
