#pragma once

#include <cmath>

#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif

#define DEG_TO_RAD(deg) ((deg) * M_PI_F / 180.0f)
#define RAD_TO_DEG(rad) ((rad) * 180.0f / M_PI_F)

//Cтруктура для 2D точки с плавающей точкой.
struct Point {
    float x, y;
    Point operator+(const Point& other) const { return { x + other.x, y + other.y }; }
    Point operator-(const Point& other) const { return { x - other.x, y - other.y }; }
    Point operator*(float scale) const { return { x * scale, y * scale }; }
    float length() const { return std::sqrt(x * x + y * y); }
    Point normalize() const { float len = length(); return len > 0.0f ? Point{ x / len, y / len } : Point{ 0.0f, 0.0f }; }
};

inline float distance(const Point& p1, const Point& p2) {
    return (p1 - p2).length();
}

inline float normalizeAngle(float angle) {
    angle = std::fmod(angle, 2.0f * M_PI_F);
    if (angle < 0.0f) {
        angle += 2.0f * M_PI_F;
    }
    return angle;
}

inline bool isAngleBetween(float targetAngle, float startAngle, float endAngle) {
    targetAngle = normalizeAngle(targetAngle);
    startAngle = normalizeAngle(startAngle);
    endAngle = normalizeAngle(endAngle);

    if (startAngle <= endAngle) {
        return targetAngle >= startAngle && targetAngle <= endAngle;
    }
    else {
        return targetAngle >= startAngle || targetAngle <= endAngle;
    }
}
