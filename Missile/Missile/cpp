#include "Missile.h" // Включаем заголовок класса Missile
#include <windows.h> // Для HDC, RGB, CreateSolidBrush, CreatePen, DeleteObject, SelectObject
#include <cmath>     // Для abs (если используется проверка границ)

Missile::Missile() : pos({ 0.0f, 0.0f }), velocity({ 0.0f, 0.0f }), isActive(false), id(-1), launcherId(-1) {}

// --- Метод launch: инициализирует ракету для полета ---
void Missile::launch(int missileId, int launcherId, const Point& startPos, const Point& targetPos, float speed) {
    this->id = missileId;
    this->launcherId = launcherId;
    pos = startPos;
    Point direction = (targetPos - startPos).normalize();
    velocity = direction * speed;
    isActive = true;
} // Конец launch()

// --- Метод update: обновляет положение ракеты ---
void Missile::update(float dt) {
    if (isActive) {
        pos = pos + velocity * dt;
    }
} // Конец update()

// --- Метод draw: отрисовывает ракету (Цветной кружок) ---
void Missile::draw(HDC hdc, int winCenterX, int winCenterY) const {
    if (isActive) {
        int screenX = static_cast<int>(pos.x + winCenterX);
        int screenY = static_cast<int>(-pos.y + winCenterY); // Инверсия Y для экрана

        COLORREF missileColor = RGB(0, 255, 255); // Ярко-голубой

        // Создаем кисть и перо для отрисовки.
        HBRUSH hBrush = CreateSolidBrush(missileColor); // Кисть для заливки
        HPEN hPen = CreatePen(PS_SOLID, 1, missileColor); // Перо для контура

        // Сохраняем текущие объекты и выбираем наши
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        // Рисуем эллипс (ракету) размером 3x3 пикселя.
        int size_px = 3;
        Ellipse(hdc, screenX - size_px, screenY - size_px, screenX + size_px + 1, screenY + size_px + 1);

        // Восстанавливаем старые объекты HDC
        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);

        // Удаляем созданные объекты GDI
        DeleteObject(hBrush);
        DeleteObject(hPen);

    }
} // Конец draw()
