#include "GameConfig.h" // Включаем заголовок структуры GameConfig
#include "Point.h"      // Для DEG_TO_RAD
#include <string>       // Для std::string
#include <sstream>      // Для std::istringstream
#include <fstream>      // Для std::ifstream
#include <algorithm>    // Для erase, remove_if
#include <stdexcept>    // Для std::stof

GameConfig g_config; // Определение глобальной переменной конфигурации.

// --- Метод для загрузки конфигурации из файла ---
bool GameConfig::loadFromFile(const std::string& filename) {
    std::ifstream infile(filename);
    if (!infile.is_open()) {
        MessageBox(NULL, L"Ошибка: файл конфигурации 'radar_config.txt' не найден или не открывается!", L"Ошибка конфигурации", MB_OK | MB_ICONERROR);
        return false;
    }

    // --- Значения по умолчанию ---
    missile_speed = 75.0f;
    distance_corner_center = 400.0f;

    danger_zone_radius = 20.0f;         // Красный
    radar_engagement_radius = 150.0f;    // Желтый
    radar_range = 350.0f;              // Зеленый

    radar_sweep_speed = DEG_TO_RAD(30.0f); // Углы в градусах в файле, храним в радианах
    radar_beam_width = DEG_TO_RAD(10.0f);  // Углы в градусах в файле, храним в радианах

    radar_turning_speed = DEG_TO_RAD(180.0f); // Углы в градусах в файле, храним в радианах (не используется)
    radar_acquire_time = 0.2f;             // (не используется)


    std::string line;

    while (std::getline(infile, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string key;
        std::string value_str;

        if (std::getline(iss, key, '=') && std::getline(iss, value_str)) {
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value_str.erase(0, value_str.find_first_not_of(" \t"));
            value_str.erase(value_str.find_last_not_of(" \t") + 1);

            try {
                float value = std::stof(value_str);

                if (key == "missile_speed") missile_speed = value;
                else if (key == "distance_corner_center") distance_corner_center = value;
                // --- <<< ИСПРАВЛЕНИЕ: Чтение угловых параметров в радианах >>> ---
                else if (key == "radar_sweep_speed") radar_sweep_speed = DEG_TO_RAD(value);
                else if (key == "radar_turning_speed") radar_turning_speed = DEG_TO_RAD(value);
                else if (key == "radar_beam_width") radar_beam_width = DEG_TO_RAD(value);
                // --- <<< Конец исправления >>> ---
                else if (key == "radar_acquire_time") radar_acquire_time = value;
                else if (key == "danger_zone_radius") danger_zone_radius = value;
                else if (key == "radar_range") radar_range = value;
                else if (key == "radar_engagement_radius") radar_engagement_radius = value;

            }
            catch (const std::exception&) {
                // Игнорируем некорректные строки.
            }
        }
    }
    infile.close();

    // --- Валидация ---
    bool validation_failed = false;
    std::wstring error_msg = L"Ошибка: Некорректные значения в файле конфигурации или дефолтах!\n";

    if (missile_speed <= 0.0f) { error_msg += L"- Скорость ракеты должна быть > 0.\n"; validation_failed = true; }
    if (distance_corner_center <= 0.0f) { error_msg += L"- Дистанция пусковых должна быть > 0.\n"; validation_failed = true; }
    if (radar_sweep_speed <= 0.0f) { error_msg += L"- Скорость сканирования должна быть > 0.\n"; validation_failed = true; }
    if (radar_beam_width <= 0.0f) { error_msg += L"- Ширина луча должна быть > 0.\n"; validation_failed = true; }
    if (danger_zone_radius < 0.0f) { error_msg += L"- Радиус мертвой зоны не может быть отрицательным.\n"; validation_failed = true; }

    // Валидация логичного расположения радиусов зон: 0 <= Красный < Желтый < Зеленый (строго)
    if (radar_engagement_radius <= danger_zone_radius) { error_msg += L"- Радиус поражения должен быть строго больше радиуса мертвой зоны.\n"; validation_failed = true; }
    if (radar_range <= radar_engagement_radius) { error_msg += L"- Радиус внешнего (зеленого) круга должен быть строго больше радиуса поражения.\n"; validation_failed = true; }


    if (validation_failed) {
        MessageBox(NULL, error_msg.c_str(), L"Ошибка конфигурации", MB_OK | MB_ICONERROR);
        return false;
    }

    return true;
}
