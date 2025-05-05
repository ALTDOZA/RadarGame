#pragma once

#include <windows.h>
#include <cmath>
#include <vector>
#include <atomic>
#include <chrono>
#include <utility>
#include <string> 
#include "Point.h"
#include "GameConfig.h"
#include "Missile.h"
#include "MissileLog.h"

extern CRITICAL_SECTION g_cs;
class SimulationState; // Предварительное объявление

struct RadarState {
    float currentAngle;
    bool isOperational;
    int detectedMissileId;
    float detectionTime;
    float sweepSpeed;
    float beamWidth;
    float radar_range;
    float engagementRadius;
    float deadZoneRadius;
};

class Radar {
private:
    Point pos;
    RadarState m_state;
    CRITICAL_SECTION* m_pCs; // Указатель на ГЛОБАЛЬНУЮ CS
    HANDLE m_hThread;
    std::atomic<bool> m_stopThread;
    HWND m_hWnd;
    MissileLog* m_pMissileLog; // Указатель на лог

    CRITICAL_SECTION m_snapshotCs; // CS для снимка
    std::vector<Missile> m_missileSnapshot;
    float m_latestGameTimeSnapshot;

    std::chrono::high_resolution_clock::time_point m_lastUpdateTime;

    // Методы потока
    static DWORD WINAPI RadarThreadProc(LPVOID lpParam);
    void run();

    // Поиск цели (5 аргументов)
    std::pair<int, int> findTarget(const std::vector<Missile>& missilesSnapshot, float currentScanAngle, float beamWidth, float range, float deadZoneRadius);

    // Вспомогательные приватные
    bool isMissileInRangeRingInternal(float dist, float range, float deadZoneRadius) const;

public:
    Radar();
    ~Radar();

    void initialize(const GameConfig& config, CRITICAL_SECTION* pCs, HWND hWnd, MissileLog* pLog);
    void shutdown();
    void draw(HDC hdc, int winCenterX, int winCenterY) const;
    void updateMissileSnapshot(const std::vector<Missile>& activeMissiles, float currentGameTime);

    // Потокобезопасные геттеры
    bool isOperational() const;
    float getCurrentAngle() const;
    float getBeamWidth() const;
    float getRange() const;
    float getDeadZoneRadius() const;
    int getDetectedMissileId() const;
    float getDetectionTime() const;
    float getEngagementRadius() const;

    // Потокобезопасные сеттеры/методы
    void setOperational(bool operational);
    void clearDetectedMissile();
    // void setCurrentAngle(float angle); // Если нужен мгновенный поворот

    Point getPos() const { return pos; }

    // Статическая функция проверки луча
    static bool isMissileInBeam(const Point& missilePos, float radarAngle, float beamWidth);
};
