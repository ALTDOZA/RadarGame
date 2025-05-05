#pragma once

#include <vector>
#include <windows.h>
#include <map> // Для таймеров
#include "Missile.h"
#include "Launcher.h"
#include "Radar.h"
#include "GameConfig.h"
#include "MissileLog.h"

struct LauncherTimerState {
    float timeSinceLastLaunch = 0.0f;
    float currentLaunchDelay = 2.0f;
};

// Класс SimulationState
class SimulationState {
private:
    std::vector<Missile> m_activeMissiles;
    std::vector<Launcher> m_launchers;
    Radar m_radar;
    MissileLog m_missileLog;
    MissileLog* m_pMissileLog;

    float m_gameTime;
    bool m_isGameOver;
    bool m_playerWon;
    int m_missilesDestroyed;
    int m_missilesLaunched;
    int m_maxMissiles;
    float m_nextLaunchTimer;
    float m_nextLaunchDelay;

    HWND m_hWnd;

    // Приватные методы
    void launchMissile(int launcherIndex); // Индекс в векторе m_launchers
    // void updateLaunchers(float dt, const GameConfig& config); // Убрано
    void updateMissiles(float dt);
    void checkCollisionsAndIntercepts(const GameConfig& config);
    void checkGameOverConditions(const GameConfig& config);
    void cleanupInactiveMissiles();

public:
    SimulationState();
    ~SimulationState();

    void initialize(const GameConfig& config, HWND hWnd);
    void update(float dt, const GameConfig& config);
    void draw(HDC hdc, const RECT* clientRect, const GameConfig& config) const;
    void reset(const GameConfig& config);
    void shutdown();

    // Небезопасный доступ для Radar::draw
    const std::vector<Missile>& getActiveMissilesUnsafe() const {
        return m_activeMissiles;
    }
};

// extern SimulationState g_simulationState; // Объявляется в main.cpp
extern GameConfig g_config; // Объявляется в GameConfig.h/cpp
