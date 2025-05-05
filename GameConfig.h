#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <windows.h>
#include "Point.h" // Для DEG_TO_RAD

// --- Конфигурация игры ---
struct GameConfig {
    float missile_speed;
    float distance_corner_center;
    float radar_sweep_speed;
    float radar_turning_speed;      // Пока не используется
    float radar_beam_width;
    float radar_range;              // Радиус внешнего ЗЕЛЕНОГО круга
    float radar_engagement_radius;  // Радиус среднего ЖЕЛТОГО круга (поражения)
    float danger_zone_radius;       // Радиус внутреннего КРАСНОГО круга (мертвая зона)
    float radar_acquire_time;       // Пока не используется

    bool loadFromFile(const std::string& filename);
};

extern GameConfig g_config;
