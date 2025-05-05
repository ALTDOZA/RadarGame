#include "SimulationState.h"
#include <cmath>    
#include <string>   
#include <vector>
#include <sstream> 
#include <iomanip>  
#include <algorithm>                     
#include <map> 
#include <random>
#include <ctime> 

extern CRITICAL_SECTION g_cs; 
extern GameConfig g_config; 

SimulationState::SimulationState() :
    m_gameTime(0.0f),
    m_isGameOver(false),
    m_playerWon(false),
    m_missilesDestroyed(0),    // Начальное кол-во сбитых ракет: 0.
    m_missilesLaunched(0),     // Начальное общее кол-во запущенных ракет: 0.
    m_maxMissiles(20),         // Максимальное кол-во ракет по умолчанию (будет заменено из конфига в initialize).
    m_hWnd(NULL),              // Дескриптор окна: изначально NULL (устанавливается в initialize).
    m_pMissileLog(&m_missileLog),
    m_nextLaunchDelay(1.0f),
    m_nextLaunchTimer(1.0f)
{

}

SimulationState::~SimulationState() {
    shutdown(); 
}
void SimulationState::initialize(const GameConfig& config, HWND hWnd) {
    m_hWnd = hWnd; 
    m_gameTime = 0.0f;         // Игровое время сбрасывается.
    m_isGameOver = false;       
    m_playerWon = false;         
    m_missilesDestroyed = 0;   // Счет сбитых ракет: 0.
    m_missilesLaunched = 0;   
    m_maxMissiles = static_cast<int>(config.distance_corner_center / 10.0f); 
    if (m_maxMissiles < 5) m_maxMissiles = 5;    // Гарантируем минимум 5 ракет.
    if (m_maxMissiles > 50) m_maxMissiles = 50; // Ограничиваем максимум, чтобы не перегружать симуляцию.


    m_activeMissiles.clear(); 
    m_launchers.clear();
    m_missileLog.clear();
    m_missileLog.initialize(&g_cs);

    float d = config.distance_corner_center;
    m_launchers.emplace_back(Point{ -d, d }, 0); // Пусковая 0: верхняя левая мировые (-d, +d).
    m_launchers.emplace_back(Point{ d, d }, 1);  // Пусковая 1: верхняя правая (+d, +d).
    m_launchers.emplace_back(Point{ -d, -d }, 2);// Пусковая 2: нижняя левая (-d, -d).
    m_launchers.emplace_back(Point{ d, -d }, 3); // Пусковая 3: нижняя правая (+d, -d).


    float initialDelay = 1.0f + static_cast<float>(rand() % 30) / 10.0f; // Пример: первый запуск через 1.0 - 4.0 сек.
    m_nextLaunchDelay = initialDelay; // Устанавливаем эту случайную задержку как текущую задержку до следующего запуска.
    m_nextLaunchTimer = m_nextLaunchDelay;
    m_radar.initialize(config, &g_cs, m_hWnd, m_pMissileLog);

} 


void SimulationState::shutdown() {
    m_radar.shutdown();
    m_activeMissiles.clear(); // Удаляем все объекты Missile из списка активных ракет.
    m_launchers.clear();      // Удаляем все объекты Launcher из списка пусковых установок.

    m_missileLog.clear();


    
}



void SimulationState::reset(const GameConfig& config) {
    shutdown();
    
    initialize(config, m_hWnd);
} 
void SimulationState::update(float dt, const GameConfig& config) {

    if (m_isGameOver) {
        m_radar.setOperational(false);
        return; // Выходим из метода update().
    }

    // --- Продвигаем общее игровое время ---
    m_gameTime += dt; 
    if (m_missilesLaunched < m_maxMissiles && !m_playerWon) {
        // Уменьшаем время до следующего ОБЩЕГО запуска на величину dt.
        m_nextLaunchTimer -= dt;

        // Если таймер достиг или опустился ниже нуля, это значит, что пришло время запустить новую ракету.
        if (m_nextLaunchTimer <= 0) {
            m_nextLaunchDelay = 2.0f + static_cast<float>(rand() % 40) / 10.0f; // Генерируем новую случайную задержку в секундах (2.0 - 6.0).
            // Вызов rand() требует, чтобы std::srand() был вызван ОДИН РАЗ в начале программы (напр., в WinMain) для хорошей случайности.
            m_nextLaunchTimer = m_nextLaunchDelay; 
            if (!m_launchers.empty()) {
                
                int randomLauncherIndex = rand() % m_launchers.size();

                launchMissile(randomLauncherIndex); // Передаем случайный индекс пусковой.
            } 
            if (m_missilesLaunched >= m_maxMissiles) {
            }

        } 
    }
    updateMissiles(dt);
    // относительно зон радара для обнаружения, уничтожения, потери цели и поражения базы.
    checkCollisionsAndIntercepts(config);
    checkGameOverConditions(config);
    cleanupInactiveMissiles();
    std::vector<Missile> activeSnapshot; // Создаем новый вектор для копирования снимка.

    for (const auto& missile : m_activeMissiles) { // Итерируем по всем ракетам в m_activeMissiles.
        if (missile.isActive) { // Копируем в снимок только те, которые помечены как активные.
            activeSnapshot.push_back(missile); // Копируем объект Missile.
        }
    }

    m_radar.updateMissileSnapshot(activeSnapshot, m_gameTime); // Обновляем снимок в радаре.

}


void SimulationState::launchMissile(int launcherIndex) {

    if (launcherIndex < 0 || static_cast<size_t>(launcherIndex) >= m_launchers.size()) {
        // Если индекс некорректный, выходим из метода без запуска.
        return;
    }

    Launcher& launcher = m_launchers[launcherIndex];

    int newMissileId = m_missilesLaunched++; 
    Point targetPosition = { 0.0f, 0.0f };
    // Получаем скорость полета ракеты из глобального объекта конфигурации.
    float missileSpeed = g_config.missile_speed;

    // Создаем новый объект Missile в локальной переменной.
    // Конструктор по умолчанию инициализирует ракету как неактивную.
    Missile newMissile;
    // Вызываем метод launch() объекта newMissile для ее полной инициализации для полета.
    // Передаем ID ракеты, ID пусковой, стартовую позицию (позиция выбранной пусковой установки),
    // целевую позицию и скорость.
    newMissile.launch(newMissileId, launcher.launcherId, launcher.pos, targetPosition, missileSpeed);

    // --- Добавляем новую активированную ракету в список активных ракет ---
    // m_activeMissiles - это std::vector<Missile>. push_back создает КОПИЮ объекта newMissile в векторе.
    m_activeMissiles.push_back(newMissile);

    // --- Логируем событие запуска ракеты ---
    // Проверяем, что указатель на объект журнала событий (MissileLog) действителен.
    if (m_pMissileLog) {
        // Добавляем запись в лог, используя метод addEntry() MissileLog.
        // addEntry() является потокобезопасным внутри, используя g_cs.
        // Передаем ID ракеты, ID запустившей пусковой, игровое время запуска и строку статуса.
        m_pMissileLog->addEntry(newMissileId, launcher.launcherId, m_gameTime, L"Запущена"); // L"..." для wchar_t строк (в Unicode сборке).
    }
    if (m_pMissileLog) { // Проверяем указатель на лог.
          std::wstringstream ss; // Создаем поток для форматирования строки лога.
          // Форматируем детали: стартовая позиция, целевая позиция, скорость.
          ss << L"Start=(" << launcher.pos.x << L"," << launcher.pos.y << L"), Target=(" << targetPosition.x << L"," << targetPosition.y << L"), Speed=" << missileSpeed;
          // Добавляем форматированную строку в лог как статус.
          m_pMissileLog->addEntry(newMissileId, launcher.launcherId, m_gameTime, ss.str()); 
     }

} 



void SimulationState::updateMissiles(float dt) {
    
    for (auto& missile : m_activeMissiles) {
        // Проверяем флаг активности ракеты.
        if (missile.isActive) {
            missile.update(dt);
        }
        
    }
} 

void SimulationState::checkCollisionsAndIntercepts(const GameConfig& config) {
    if (!m_radar.isOperational() || m_isGameOver) {
        return;
    }
    int detectedMissileId = m_radar.getDetectedMissileId();
    float engagementRadius = m_radar.getEngagementRadius(); // Радиус среднего ЖЕЛТОГО круга (Граница ЗОНЫ ПОРАЖЕНИЯ ПО ДИСТАНЦИИ).
    float deadZoneRadius = m_radar.getDeadZoneRadius();     // Радиус внутреннего КРАСНОГО круга (Граница МЕРТВОЙ ЗОНЫ ПО ДИСТАНЦИИ).


    float currentScanAngle = m_radar.getCurrentAngle();     // Текущий угол центра сканирующего луча (в радианах).
    float beamWidth = m_radar.getBeamWidth();
    if (detectedMissileId != -1) {

        Missile* pTrackedMissile = nullptr;
        for (auto& missile : m_activeMissiles) {
            // Проверяем два условия: 1. Совпадает ли ID этой ракеты с ID отслеживаемой целью detectedMissileId. 2. Активна ли эта ракета ( isActive == true).
            if (missile.id == detectedMissileId && missile.isActive) {
                pTrackedMissile = &missile;  // Если оба условия истинны, мы нашли нужную ракету. Сохраняем указатель на этот объект в векторе.
                break;
            }
        }
        if (pTrackedMissile) {

            float missileDist = pTrackedMissile->getDistanceToCenter();
            if (missileDist <= deadZoneRadius) {

                if (m_pMissileLog) { 
                    m_pMissileLog->addEntry(pTrackedMissile->id, pTrackedMissile->launcherId, m_gameTime, L"Потеряна (мертв.зона)"); // Русский текст L"..."
                }
                m_radar.clearDetectedMissile();  
            }

            else if (missileDist <= engagementRadius &&
                Radar::isMissileInBeam(pTrackedMissile->pos, currentScanAngle, beamWidth))
            {

                pTrackedMissile->isActive = false;  // <<< ИСПРАВЛЕНИЕ: Используем оператор '->'. Устанавливаем флаг активности ракеты в false. Она больше не двигается и не рисуется как активная.
                m_missilesDestroyed++;
                if (m_pMissileLog) {
                    m_pMissileLog->addEntry(pTrackedMissile->id, pTrackedMissile->launcherId, m_gameTime, L"Уничтожена"); // Русский текст L"..."
                }

                m_radar.clearDetectedMissile();
            }

        }
        else {

            m_radar.clearDetectedMissile(); // Радар становится свободен для поиска новой цели в следующем цикле Radar::run().
        }
    }
    for (auto& missile : m_activeMissiles) {
        float missileDist = missile.getDistanceToCenter();
        if (missile.isActive && missileDist <= deadZoneRadius) {

            m_isGameOver = true;    // Устанавливаем флаг: игра окончена. (член класса SimulationState).
            m_playerWon = false;

            m_radar.setOperational(false);
            if (m_pMissileLog) {
                m_pMissileLog->addEntry(missile.id, missile.launcherId, m_gameTime, L"Поражение радара!");
            }

            for (auto& other_missile : m_activeMissiles) { // Итерируем по ВСЕМ ракетам в списке (active и inactive).
                other_missile.isActive = false;
            }


            break;
        }
    }
}



void SimulationState::checkGameOverConditions(const GameConfig& config) {
    if (m_isGameOver) return; 
    if (m_missilesLaunched >= m_maxMissiles) { // Условие 1: Общее количество запущенных ракет достигло или превысило максимальное количество.
        bool anyActiveMissilesLeft = false;
        for (const auto& missile : m_activeMissiles) { // Итерируем по всем ракетам в списке. Используем const& т.к. не меняем ракеты при проверке.
            if (missile.isActive) { // Проверяем флаг активности.
                anyActiveMissilesLeft = true; // Найдена хотя бы одна активная ракета.
                break;
            }
        }

        if (!anyActiveMissilesLeft) {
           
            m_isGameOver = true;
            m_playerWon = true; 
            m_radar.setOperational(false);
            if (m_pMissileLog) { 
                m_pMissileLog->addEntry(-1, -1, m_gameTime, L"ПОБЕДА!"); 
            }

            
        }
    } 


}
void SimulationState::cleanupInactiveMissiles() {
    m_activeMissiles.erase( // Метод erase удаляет элементы вектора в заданном диапазоне.
        std::remove_if(m_activeMissiles.begin(), m_activeMissiles.end(),
           
            [](const Missile& missile) { return !missile.isActive; } 
        ),
        m_activeMissiles.end()
    );
    
}
void SimulationState::draw(HDC hdc, const RECT* clientRect, const GameConfig& config) const {
    int width = clientRect->right - clientRect->left;
    int height = clientRect->bottom - clientRect->top;
    int centerX = width / 2;
    int centerY = height / 2;

    // Для вывода (стаистики можно менять)
    int detailStatsX = 10;
    int detailStatsY = 300; 
    int lineHeight = 18;    

    SetTextColor(hdc, RGB(255, 255, 255)); 
    SetBkMode(hdc, TRANSPARENT);
    for (const auto& launcher : m_launchers) {
        launcher.draw(hdc, centerX, centerY);
    }
    for (const auto& missile : m_activeMissiles) {
        if (missile.isActive) {
            missile.draw(hdc, centerX, centerY);
        }
    }

    m_radar.draw(hdc, centerX, centerY);
    SetTextColor(hdc, RGB(255, 255, 255)); // Белый цвет текста.
    SetBkMode(hdc, TRANSPARENT); // Прозрачный фон.

    // Форматируем и выводим строку статистики
    std::wstringstream ss_stats;
    ss_stats << L"Время: " << std::fixed << std::setprecision(1) << m_gameTime << L" c | ";
    size_t activeMissileCount = m_activeMissiles.size();
    ss_stats << L"Активно: " << activeMissileCount << L" | ";
    ss_stats << L"Запущено: " << m_missilesLaunched << L"/" << m_maxMissiles << L" | ";
    ss_stats << L"Уничтожено: " << m_missilesDestroyed;
    TextOut(hdc, 10, 10, ss_stats.str().c_str(), static_cast<int>(ss_stats.str().length()));


    size_t countToDisplay = 15;
    if (countToDisplay > m_missilesLaunched) countToDisplay = m_missilesLaunched; // Не больше, чем запущено.

    // Итерируем с конца по количеству последних ракет, которые хотим отобразить.
    // Ракеты нумеруются от 0 до m_missilesLaunched - 1. ID = порядку запуска.
    for (int i = 0; i < countToDisplay; ++i) {
        // ID текущей ракеты в этой итерации: m_missilesLaunched - 1 - i
        int currentMissileId = m_missilesLaunched - 1 - i;

        // Получаем последнюю запись лога для этой ракеты (используем MissileLog метод).
        // Этот метод должен быть потокобезопасен (он внутри захватывает CS).
        MissileLogEntry lastEntry = m_pMissileLog->getLastEntryForMissile(currentMissileId); // Предполагает, что getLastEntryForMissile реализован в MissileLog.


        // Форматируем строку статистики для текущей ракеты.
        std::wstringstream ss_detail;
        ss_detail << L"Ракета " << currentMissileId; // ID ракеты.
        if (lastEntry.missileId != -1) { // Проверяем, что запись в логе была найдена для этого ID.
            ss_detail << L" (П" << lastEntry.launcherId << L"): "; // ID пусковой.
            ss_detail << lastEntry.status; // Статус ("Запущена", "Уничтожена", "Потеряна", etc.).
            // Можно добавить время последнего события:
            ss_detail << L" [" << std::fixed << std::setprecision(1) << lastEntry.timestamp << L"с]";
        }
        else {
            ss_detail << L": Лог пуст или не найден."; 
        }
        std::wstring detailString = ss_detail.str(); // Получаем std::wstring.


        // Выводим строку статистики для текущей ракеты.
        TextOut(hdc, detailStatsX, detailStatsY, detailString.c_str(), static_cast<int>(detailString.length()));

        detailStatsY += lineHeight; // Увеличиваем координату Y для следующей строки детализации.
    }


    if (m_isGameOver) {
        HFONT hFont = CreateFont(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, TEXT("Arial"));
        
        HFONT hOldFont_main; // Для восстановления после основного сообщения
        HFONT hOldFont_restart;
        std::wstring endMessage = m_playerWon ? L"ПОБЕДА!" : L"ПОРАЖЕНИЕ!";
        std::wstring restartMsg = L"Нажмите 'Начать заново'";
        SIZE textSize;
        int textX; // Будут переиспользоваться для обоих сообщений
        int textY;

        hOldFont_main = (HFONT)SelectObject(hdc, hFont);

        GetTextExtentPoint32(hdc, endMessage.c_str(), static_cast<int>(endMessage.length()), &textSize);
        textX = centerX - textSize.cx / 2; 
        textY = centerY - textSize.cy / 2 - 50; 

        SetTextColor(hdc, m_playerWon ? RGB(0, 255, 0) : RGB(255, 0, 0));
        TextOut(hdc, textX, textY, endMessage.c_str(), static_cast<int>(endMessage.length()));
        SelectObject(hdc, hOldFont_main); // Восстанавливаем
        DeleteObject(hFont);
        HFONT hFontSmall = CreateFont(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, TEXT("Arial"));

        hOldFont_restart = (HFONT)SelectObject(hdc, hFontSmall);
        GetTextExtentPoint32(hdc, restartMsg.c_str(), static_cast<int>(restartMsg.length()), &textSize);
        textX = centerX - textSize.cx / 2; 
        textY = centerY + textSize.cy / 2; 

        SetTextColor(hdc, RGB(200, 200, 200)); // Серый цвет.
        TextOut(hdc, textX, textY, restartMsg.c_str(), static_cast<int>(restartMsg.length()));
        SelectObject(hdc, hOldFont_restart); // Восстанавливаем шрифт, который был активен ДО выбора hFontSmall.
        DeleteObject(hFontSmall);  
    }

}
