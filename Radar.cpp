#define NOMINMAX
#include <windows.h>
#include "Radar.h"
#include "SimulationState.h"
#include <algorithm> 
#include <limits>    
#include <chrono>    
#include <cmath>    
#include <utility> 
#include <string> 
#include <objbase.h>
#include <windows.h> 

// Объявление extern глобальной критической секции g_cs
// Радар (через свой указатель m_pCs) использует ее для синхронизации доступа к разделяемому состоянию m_state.
extern CRITICAL_SECTION g_cs;


// --- Объявление extern глобального объекта состояния симуляции ---

// Этот класс (в методе draw()) обращается к g_simulationState (которая синхронизирована SimulationState-ом),
//но прямой доступ к вектору ракет в Radar::draw() небезопасен без внешней блокировки,
// которая и выполняется здесь через m_pCs (указывающий на g_cs).
extern SimulationState g_simulationState; 



// --- Конструктор класса Radar ---
// Инициализирует члены класса начальными значениями.
// Инициализирует ВНУТРЕННЮЮ Critical Section снимка (m_snapshotCs).
Radar::Radar() :
    pos({ 0.0f, 0.0f }), // Позиция радара фиксирована в центре игровых координат (0,0).
                         // Эта позиция не меняется в данной симуляции.

    m_state({ 0.0f,     // currentAngle - Текущий угол сканирования (нач. с 0).
             true,      // isOperational - Флаг работы (изначально работает).
             -1,        // detectedMissileId - ID обнаруженной цели (-1: нет цели).
             0.0f,      // detectionTime - Время обнаружения (нач. 0).

             // Параметры радара из GameConfig (будут перезаписаны в initialize).
             0.0f,      // sweepSpeed - Скорость сканирования.
             0.0f,      // beamWidth - Ширина луча.
             0.0f,      // radar_range - Радиус внешнего (ЗЕЛЕНОГО) круга.
             0.0f,      // engagementRadius - Радиус среднего (ЖЕЛТОГО) круга.
             0.0f }),    // deadZoneRadius - Радиус внутреннего (КРАСНОГО) круга.

    m_pCs(nullptr), // Указатель на ГЛОБАЛЬНУЮ CS (будет присвоен в initialize).
    m_hThread(NULL), // Дескриптор потока логики радара (будет создан в initialize). NULL = 0.
    m_stopThread(false), // Атомарный флаг для остановки потока (false: не остановлен).
    m_hWnd(NULL), // Дескриптор окна (для MessageBox из потока, будет присвоен в initialize).
    m_pMissileLog(nullptr), // Указатель на лог (для записи об обнаружении, присвоен в initialize).
    m_latestGameTimeSnapshot(0.0f) // Время последнего снимка ракет (нач. 0.0f).
{
    // Инициализация внутренней Critical Section для защиты m_missileSnapshot и m_latestGameTimeSnapshot.
    InitializeCriticalSection(&m_snapshotCs);
    // Инициализация m_lastUpdateTime здесь или в initialize
    m_lastUpdateTime = std::chrono::high_resolution_clock::now();
} 


// --- Деструктор класса Radar ---
// Отвечает за корректную очистку управляемых ресурсов: остановка потока и удаление внутренней CS.
Radar::~Radar() {
    shutdown(); // Сигнализируем потоку об остановке и ждем его завершения.
    DeleteCriticalSection(&m_snapshotCs); // Удаляем внутреннюю Critical Section снимка (после завершения потока!).
} 


// --- Метод инициализации объекта Radar ---
// Вызывается из SimulationState::initialize при старте или перезапуске игры.
// Настраивает состояние радара, сохраняет внешние зависимости (CS, HWND, Log) и запускает поток логики.
void Radar::initialize(const GameConfig& config, CRITICAL_SECTION* pCs, HWND hWnd, MissileLog* pLog) {
    // Если радар уже работает (т.е. hThread не NULL), корректно завершаем предыдущую работу.
    shutdown(); // Это установит m_stopThread и дождется завершения старого потока run().

    // --- Важно: Сохраняем указатель на ГЛОБАЛЬНУЮ критическую секцию ---
    // m_pCs будет использоваться во всех потокобезопасных методах доступа к m_state.
    // pCs указывает на g_cs, которая должна быть инициализирована в main.cpp ДО вызова этого метода.
    m_pCs = pCs;

    // Сохраняем дескриптор окна и указатель на журнал событий.
    m_hWnd = hWnd;
    m_pMissileLog = pLog;

    // Сбрасываем флаг остановки потока - новый поток должен начать работать.
    m_stopThread = false;

    // --- Инициализация m_state (состояние радара) под защитой ГЛОБАЛЬНОЙ CS ---
    EnterCriticalSection(m_pCs); // Захватываем глобальную CS g_cs для безопасного доступа к m_state.

    // Сброс состояния при новой игре/симуляции.
    m_state.currentAngle = 0.0f; // Начинаем сканирование с 0 радиан.
    m_state.isOperational = true; // Радар включается.
    m_state.detectedMissileId = -1; // Нет обнаруженной цели.
    m_state.detectionTime = 0.0f; // Время обнаружения 0.

    // Копируем актуальные параметры из GameConfig в m_state.
    m_state.sweepSpeed = config.radar_sweep_speed;     // Скорость сканирования (в радианах/с).
    m_state.beamWidth = config.radar_beam_width;         // Ширина луча (в радианах).
    m_state.radar_range = config.radar_range;               // Внешний ЗЕЛЕНЫЙ радиус.
    m_state.engagementRadius = config.radar_engagement_radius; // Средний ЖЕЛТЫЙ радиус.
    m_state.deadZoneRadius = config.danger_zone_radius;     // Внутренний КРАСНЫЙ радиус.

    LeaveCriticalSection(m_pCs); // Освобождаем глобальную CS.


    // --- Очищаем данные снимка активных ракет ---
    EnterCriticalSection(&m_snapshotCs); // Захватываем внутреннюю CS снимка для безопасного доступа к m_missileSnapshot.
    m_missileSnapshot.clear(); // Очищаем снимок.
    m_latestGameTimeSnapshot = 0.0f; // Сбрасываем время снимка.
    LeaveCriticalSection(&m_snapshotCs); // Освобождаем внутреннюю CS снимка.


    // Инициализируем время последнего обновления для расчета dt в потоке run().
    m_lastUpdateTime = std::chrono::high_resolution_clock::now();

    // --- Запускаем новый поток для выполнения логики радара (метода run()) ---
    // Создаем поток, передавая адрес статической функции RadarThreadProc и указатель на этот объект (this).
    m_hThread = CreateThread(
        NULL,          // Атрибуты защиты по умолчанию.
        0,             // Размер стека по умолчанию.
        RadarThreadProc, // Указатель на функцию потока (статическая).
        this,          // Параметр, передаваемый в функцию потока (указатель на текущий объект Radar).
        0,             // Флаги создания (0: поток сразу начинает работать).
        NULL           // Идентификатор потока (не нужен нам).
    );
    // Проверяем, успешно ли создан поток. CreateThread возвращает NULL при сбое.
    if (m_hThread == NULL) {
        // Если не удалось создать поток, выводим сообщение об ошибке и радар не будет работать.
        MessageBox(m_hWnd, L"Не удалось создать поток радара!", L"Ошибка", MB_OK | MB_ICONERROR);
        setOperational(false); // Устанавливаем статус радар как нерабочий (потокобезопасно).
    }
} // Конец initialize()


// --- Метод завершения работы объекта Radar ---
// Вызывается из SimulationState::shutdown. Сигнализирует потоку run() об остановке
// и ЖДЕТ его завершения, затем закрывает дескриптор потока.
void Radar::shutdown() {
    m_stopThread = true; // Устанавливаем атомарный флаг остановки потока run().
                        // Поток run() проверяет этот флаг в условии своего основного цикла.

    // Если дескриптор потока существует и он еще не NULL (поток был успешно создан).
    if (m_hThread != NULL) {
        // Ждем завершения потока (WaitForSingleObject). Таймаут 500мс.
        // Если поток не завершится за это время, продолжим (это лучше, чем вечная блокировка).
        DWORD waitResult = WaitForSingleObject(m_hThread, 500); // WAIT_TIMEOUT = 258 (если превышен таймаут)
        // if (waitResult == WAIT_TIMEOUT) { /* Optional: Log a timeout warning */ }

        CloseHandle(m_hThread); // Закрываем дескриптор потока, освобождая ресурс ядра.
        m_hThread = NULL; // Сбрасываем дескриптор, чтобы не пытаться закрыть/ждать несуществующий поток снова.
    }
} // Конец shutdown()


// --- Статическая точка входа для потока логики радара (RadarThreadProc) ---
// Эта функция должна иметь специфическую сигнатуру DWORD WINAPI function_name(LPVOID parameter)
// для использования с функцией CreateThread().
// Она просто принимает указатель на объект Radar (переданный в CreateThread), преобразует его
// и вызывает нестатический метод run() для этого объекта.
DWORD WINAPI Radar::RadarThreadProc(LPVOID lpParam) {
    Radar* pRadar = static_cast<Radar*>(lpParam);
    try {
        if (pRadar) {
            pRadar->run(); 
        }
    }
    catch (const std::exception& ex) {
        

        return 1; 
    }
    catch (...) 
    {
        return 0; 
    }
   
    return 0; 
}


void Radar::run() {
    
    float currentAngle_local; 
    float sweepSpeed_local;     // Скорость вращения луча (рад/с).
    float beamWidth_local;      // Ширина луча (радианы).
    float radar_range_local;        // Радиус внешнего ЗЕЛЕНОГО круга (Внешняя граница Обнаружения).
    float engagementRadius_local;   // Радиус среднего ЖЕЛТОГО круга (Зона Поражения).
    float deadZoneRadius_local;     // Радиус внутреннего КРАСНОГО круга (Мертвая Зона / Внутр. граница Обнаружения).


    EnterCriticalSection(m_pCs); // Захватываем глобальную критическую секцию g_cs для доступа к m_state.

    currentAngle_local = m_state.currentAngle; // Читаем начальный угол.
    // Копируем параметры из m_state в локальные переменные потока.
    sweepSpeed_local = m_state.sweepSpeed;
    beamWidth_local = m_state.beamWidth;
    radar_range_local = m_state.radar_range;          // Копируем радиус Зеленой зоны.
    engagementRadius_local = m_state.engagementRadius; // Копируем радиус Желтой зоны.
    deadZoneRadius_local = m_state.deadZoneRadius;    // Копируем радиус Красной зоны.

    LeaveCriticalSection(m_pCs); 
    while (!m_stopThread.load()) { // Используем load() для чтения атомарной переменной.
        // --- Расчет времени кадра (dt) ---
        auto currentTime = std::chrono::high_resolution_clock::now(); // Текущее точное время.
        std::chrono::duration<float> elapsed = currentTime - m_lastUpdateTime; // Время с прошлого кадра.
        float dt = elapsed.count(); // Дельта времени в секундах.
        m_lastUpdateTime = currentTime; // Обновляем время последнего кадра для следующего шага.


        // --- Проверка операционного статуса радара ---
        bool isOperationalStatus = isOperational(); // Используем публичный геттер; он потокобезопасен (использует m_pCs).
        if (!isOperationalStatus) 
        {
            // Если радар не работает, спим некоторое время и пропускаем логику сканирования/поиска в этом цикле.
            Sleep(100); // Короткий сон, чтобы не грузить CPU пустым циклом.
            continue; // Переходим к следующей итерации цикла while.
        }


        // --- ОБНОВЛЕНИЕ угла сканирования ---
        // Увеличиваем локальный угол сканирования на sweepSpeed * dt.
        currentAngle_local = normalizeAngle(currentAngle_local + sweepSpeed_local * dt); // normalizeAngle из Point.h.


        // --- Получаем актуальный ЛОКАЛЬНЫЙ СНИМОК ракет и игровое время ---
        // Этот снимок был сделан основным потоком (SimulationState::update) и используется здесь ТОЛЬКО ДЛЯ ЧТЕНИЯ.
        // Доступ к снимку защищен внутренней CS m_snapshotCs.
        std::vector<Missile> missilesSnapshotCopy; // Создаем вектор для копирования снимка.
        float currentGameTime;                   // Переменная для времени снимка.
        EnterCriticalSection(&m_snapshotCs); // Захватываем ВНУТРЕННЮЮ Critical Section снимка.
        missilesSnapshotCopy = m_missileSnapshot; // Копируем весь вектор снимка (эффективно для небольшого кол-ва ракет).
        currentGameTime = m_latestGameTimeSnapshot; // Читаем время, соответствующее этому снимку.
        LeaveCriticalSection(&m_snapshotCs); // Освобождаем ВНУТРЕННЮЮ Critical Section снимка.


        // --- Логика: Поиск НОВОЙ цели для ПЕРВИЧНОГО ОБНАРУЖЕНИЯ ---
        // Радар сканирует пространство в поисках НОВОЙ цели ТОЛЬКО если в данный момент он НЕ отслеживает никакую другую цель.
        int currentDetectedId = getDetectedMissileId(); // Получаем ID цели, которую радар уже отслеживает (потокобезопасно).
        std::pair<int, int> foundTargetInfo = { -1, -1 };
        foundTargetInfo = findTarget
        (
            missilesSnapshotCopy,   // Снимок активных ракет.
            currentAngle_local,     // Текущий угол сканирования луча.
            beamWidth_local,        // Ширина луча.
            radar_range_local,      // Внешний радиус ЗОНЫ ОБНАРУЖЕНИЯ (ЗЕЛЕНЫЙ круг).
            deadZoneRadius_local    // Внутренний радиус ЗОНЫ ОБНАРУЖЕНИЯ (КРАСНЫЙ/МЕРТВАЯ зона).
        );
        // Если currentDetectedId != -1, радар УЖЕ ОТСЛЕЖИВАЕТ ЦЕЛЬ, findTarget НЕ ВЫЗЫВАЕТСЯ в этом цикле run().
        // Логика отслеживания и сбития/потери цели в этом случае находится в SimulationState::update.


        // --- Синхронизация ОБНОВЛЕННОГО ЛОКАЛЬНОГО состояния с общим m_state (под защитой ГЛОБАЛЬНОЙ CS m_pCs) ---
        // В этом блоке обновляем: 1. Текущий угол сканирования в m_state. 2. Информацию о НОВОЙ ОБНАРУЖЕННОЙ цели, если найдена.
        EnterCriticalSection(m_pCs); // Захватываем глобальную критическую секцию g_cs для доступа к m_state.

        // 1. Обновляем текущий угол сканирования в общем состоянии радара m_state.
        m_state.currentAngle = currentAngle_local;

        // 2. Логика ПЕРВОГО ОБНАРУЖЕНИЯ и сохранения информации о цели:
        // Если findTarget НАШЕЛ потенциальную цель (его ID != -1), И в общем состоянии радара НЕ БЫЛО отслеживаемой цели (m_state.detectedMissileId == -1, это условие было проверено выше, поэтому можно просто if (foundTargetInfo.first != -1)).
        if (foundTargetInfo.first != -1 && m_state.detectedMissileId == -1) 
        {
            // Это НОВАЯ цель, которая впервые попала в СКАНИРУЮЩИЙ луч радара ВНУТРИ ЗОНЫ ОБНАРУЖЕНИЯ.
            m_state.detectedMissileId = foundTargetInfo.first; // Запоминаем ее уникальный ID.
            m_state.detectionTime = currentGameTime;           // Запоминаем игровое время, когда цель была обнаружена.

            // --- Логирование СОБЫТИЯ ОБНАРУЖЕНИЯ ---
            // Добавляем запись в журнал событий MissileLog (он потокобезопасен внутри).
            if (m_pMissileLog) 
            { // Проверяем, что указатель на лог валиден.
                // addEntry ожидает (missileId, launcherId, timestamp, status string).
                m_pMissileLog->addEntry(m_state.detectedMissileId, foundTargetInfo.second, m_state.detectionTime, L"Обнаружена");
            }
        }
        // Если findTarget ничего не нашел (или не был вызван), И радар что-то отслеживал (detectedMissileId != -1),
        // то либо цель вне текущего луча (но радар ее все еще отслеживает), либо она уже сбита/потеряна (тогда SimulationState::update сбросит detectedMissileId).
        // Этот поток НЕ СБРАСЫВАЕТ detectedMissileId! Это делает SimulationState::update через вызов clearDetectedMissile().

        LeaveCriticalSection(m_pCs); // Освобождаем глобальную критическую секцию.


        Sleep(10); // Короткая пауза для потока (10 мс) для снижения нагрузки на CPU.
    } // Конец цикла while (!m_stopThread.load()). Поток завершается, когда m_stopThread становится true.

    // Поток закончил свою работу.
} // Конец метода run()


// --- Реализация метода findTarget ---
// Этот метод вызывается из run(). Ищет ближайшую АКТИВНУЮ ракету в ПЕРЕДАННОМ снимке
// (не меняет оригинал) и проверяет, находится ли она:
// 1. В дальностном кольце ОБНАРУЖЕНИЯ (СТРОГО > deadZoneRadius, <= range - где range = radar_range_local).
// 2. В текущем СКАНИРУЮЩЕМ луче (ширина beamWidth вокруг угла currentScanAngle).
// Возвращает std::pair{ID ракеты, ID пусковой установки}, если найдена, или {-1, -1}, если нет.
std::pair<int, int> Radar::findTarget(const std::vector<Missile>& missilesSnapshot, float currentScanAngle, float beamWidth, float range, float deadZoneRadius) {
    // Используем квадрат расстояния для сравнения - это быстрее, чем std::sqrt().
    float closestDistSq = std::numeric_limits<float>::max();

    int foundMissileId = -1;         // Переменная для хранения ID найденной ракеты. Изначально -1 (не найдена).
    int foundMissileLauncherId = -1; // Переменная для хранения Launcher ID найденной ракеты.

    // Итерируем по каждой ракете в переданном снимке. Снимок - это копия, работаем безопасно.
    for (const auto& missile : missilesSnapshot) {
        if (!missile.isActive) continue; // Проверяем только активные ракеты.

        float missileDist = missile.getDistanceToCenter(); // Получаем расстояние ракеты до центра (позиции радара).

        // 1. Проверка нахождения ракеты в дальностном кольце ОБНАРУЖЕНИЯ: (Красный, Зеленый].
        // Ракета должна быть ЗА пределами КРАСНОГО круга (Мертвая зона)
        // И ВНУТРИ или НА границе ЗЕЛЕНОГО внешнего круга (граница Radar::range).
        // isMissileInRangeRingInternal() - наш приватный метод, который выполняет эту проверку.
        bool isInRangeRing = isMissileInRangeRingInternal(missileDist, range, deadZoneRadius);

        // 2. Проверка нахождения ракеты в пределах углов ТЕКУЩЕГО СКАНИРУЮЩЕГО луча:
        // Угол ракеты должен попадать в сектор, определяемый currentScanAngle и beamWidth.
        // isMissileInBeam() - наш СТАТИЧЕСКИЙ метод, выполняет проверку углового положения.
        bool isInScanBeam = Radar::isMissileInBeam(missile.pos, currentScanAngle, beamWidth);

        // Ракета считается ПОДХОДЯЩЕЙ ДЛЯ ОБНАРУЖЕНИЯ, если она активна И находится в ПРАВИЛЬНОЙ дальности И в текущем секторе сканирования.
        if (isInRangeRing && isInScanBeam) {
            // Если ракета подходит, вычисляем ее квадрат расстояния до центра
            float distSq = missile.pos.x * missile.pos.x + missile.pos.y * missile.pos.y; // Квадрат расстояния.

            // Ищем ближайшую ракету СРЕДИ ВСЕХ подходящих в этом луче/диапазоне.
            if (distSq < closestDistSq) {
                closestDistSq = distSq;          // Обновляем минимальный квадрат расстояния.
                foundMissileId = missile.id;       // Сохраняем ID этой ближайшей ракеты.
                foundMissileLauncherId = missile.launcherId; // Сохраняем Launcher ID этой ракеты (Missile имеет член launcherId).
            }
        }
    } // Конец цикла по снимку ракет.

    // --- Возвращаем результат поиска ---
    // Если foundMissileId все еще -1, подходящая цель не была найдена.
    // Иначе возвращаем ID и LauncherID найденной ближайшей подходящей ракеты.
    if (foundMissileId != -1) {
        return { foundMissileId, foundMissileLauncherId }; // Используем std::pair
    }
    return { -1, -1 }; // Нет подходящей цели в текущем сканирующем луче в зоне обнаружения.
} // Конец реализации findTarget()


// --- Реализация вспомогательной приватной функции: isMissileInRangeRingInternal ---
// Этот метод проверяет ТОЛЬКО ДАЛЬНОСТЬ ракеты. Находится ли она в кольце (deadZoneRadius, range].
// Используется методом findTarget(). Приватный. Const-корректный. Реализация ОДИН РАЗ.
bool Radar::isMissileInRangeRingInternal(float dist, float range, float deadZoneRadius) const { // Реализация ОДИН РАЗ. const в конце.
    // dist > deadZoneRadius   (строго больше радиуса Красной зоны - ЗА ее пределами)
    // dist <= range         (меньше ИЛИ РАВНО радиусу Зеленой зоны - ВНУТРИ или НА ее границе)
    return dist > deadZoneRadius && dist <= range;
} // Конец реализации isMissileInRangeRingInternal()


// --- Реализация вспомогательной СТАТИЧЕСКОЙ геометрической функции: isMissileInBeam ---
// Этот метод проверяет ТОЛЬКО УГОЛ ракеты. Попадает ли точка (позиция ракеты)
// в угловой сектор ("луч"), определенный центром (0,0), углом луча и его шириной.
// Используется findTarget() (в потоке радара, для сканирования)
// и SimulationState::update (для проверки вхождения в луч захвата - хотя сейчас logic in SS handles this based on detected ID not actual beam).
// СТАТИЧЕСКАЯ функция: не имеет доступа к членам конкретного объекта Radar (кроме статических). const не применяется. Реализация ОДИН РАЗ.
bool Radar::isMissileInBeam(const Point& missilePos, float radarAngle, float beamWidth) { // static перед bool. Реализация ОДИН РАЗ.
    // Вычисляем угол ракеты относительно центра (0,0) - позиции радара.
    // Используем std::atan2(y, x) для получения правильного угла во всех 4х квадрантах (-PI, PI].
    float missileAngle = std::atan2(missilePos.y, missilePos.x);

    // Нормализуем вычисленный угол ракеты к диапазону [0, 2*PI) для удобства сравнения.
    missileAngle = normalizeAngle(missileAngle); // normalizeAngle() определена в Point.h.

    // Вычисляем начальный и конечный углы сектора луча сканирования относительно текущего угла радара (radarAngle) и ширины луча (beamWidth).
    // Углы также нормализуются к [0, 2*PI).
    float beamStart = normalizeAngle(radarAngle - beamWidth / 2.0f);
    float beamEnd = normalizeAngle(radarAngle + beamWidth / 2.0f);

    // Проверяем, находится ли угол ракеты ('missileAngle') между начальным и конечным углами луча.
    // isAngleBetween() - вспомогательная функция из Point.h, учитывает "пересечение" угла 0/2*PI.
    return isAngleBetween(missileAngle, beamStart, beamEnd);
} // Конец реализации статической функции isMissileInBeam()


// --- Реализация updateMissileSnapshot ---
// Этот метод вызывается ИЗ основного потока SimulationState::update.
// Его задача - скопировать текущий вектор активных ракет и текущее игровое время
// во внутренние члены Radar (m_missileSnapshot, m_latestGameTimeSnapshot)
// для потока run(). Это должно быть потокобезопасно (под защитой m_snapshotCs).
void Radar::updateMissileSnapshot(const std::vector<Missile>& activeMissiles, float currentGameTime) {
    // Захватываем ВНУТРЕННЮЮ Critical Section снимка для безопасной записи в m_missileSnapshot и m_latestGameTimeSnapshot.
    EnterCriticalSection(&m_snapshotCs); // Захват CS снимка.

    m_missileSnapshot = activeMissiles; // Копируем ВЕСЬ вектор активных ракет из основного потока в вектор снимка радара.
    m_latestGameTimeSnapshot = currentGameTime; // Сохраняем игровое время, соответствующее этому снимку.

    LeaveCriticalSection(&m_snapshotCs); // Освобождение CS снимка.
} // Конец updateMissileSnapshot()


void Radar::draw(HDC hdc, int winCenterX, int winCenterY) const {
    // Читаем актуальное состояние радара потокобезопасно через публичные геттеры.
    bool isOperationalStatus = isOperational();          // Работает ли радар?
    float currentAngle = getCurrentAngle();             // Текущий угол сканирования для луча.
    float beamWidth = getBeamWidth();                     // Ширина луча сканирования.

    // --- Получаем радиусы трех зон ИЗ СОСТОЯНИЯ ЧЕРЕЗ ГЕТТЕРЫ ---
    float outerGreenRadius = getRange();            // Радиус внешнего ЗЕЛЕНОГО круга.
    float middleYellowRadius = getEngagementRadius(); // Радиус среднего ЖЕЛТОГО круга (Зона Поражения).
    float deadZoneRedRadius = getDeadZoneRadius();    // Радиус внутреннего КРАСНОГО круга (Мертвая зона).

    int detectedMissileId = getDetectedMissileId(); // ID ракеты, которую радар отслеживает для сбития (-1 если нет).


    // Вычисляем экранные координаты центра радара (он в мировых (0,0)).
    int screenX = static_cast<int>(pos.x + winCenterX);
    int screenY = static_cast<int>(-pos.y + winCenterY);


    // --- Управление GDI объектами (карандаши для контуров/линий, кисти для заливки) ---
    // Сохраняем текущие выбранные GDI объекты в HDC, чтобы восстановить их В САМОМ КОНЦЕ draw().
    // Выбираем стандартные NULL объекты как базу для SelectObject().
    HPEN hOldPen = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));     // Сохраняем и выбираем NULL перо.
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH)); // Сохраняем и выбираем NULL кисть.


    // --- Определяем цвета для рисования ---
    COLORREF greenColor = RGB(0, 200, 0);         // Зеленый: Внешний круг, Луч.
    COLORREF yellowColor = RGB(255, 255, 0);        // Желтый: Средний круг (Зона Поражения), Маркеры обнаружения лучом.
    COLORREF redColor = RGB(255, 0, 0);           // Красный: Внутренний круг (Мертвая зона), База радара.
    COLORREF baseOutlineColor = RGB(0, 0, 0);     // Черный: Контур базы.
    COLORREF targetLineColor = RGB(255, 255, 255); // Белый: Линия к отслеживаемой цели (пунктир).
    COLORREF baseColorNonOperational = RGB(100, 0, 0); // Темно-красный: База нерабочего радара.


    // --- Создаем GDI Перья (карандаши) и Кисти ---
    // Кисть для заливки базы.
    HBRUSH hBrushBase = CreateSolidBrush(isOperationalStatus ? redColor : baseColorNonOperational); // Кисть для заливки базы.
    // Перья для контуров зон и линий.
    HPEN hPenGreen = CreatePen(PS_SOLID, 1, greenColor);         // Для внешнего ЗЕЛЕНОГО круга.
    HPEN hPenYellow = CreatePen(PS_SOLID, 1, yellowColor);       // Для среднего ЖЕЛТОГО круга.
    HPEN hPenRed = CreatePen(PS_SOLID, 1, redColor);             // Для внутреннего КРАСНОГО круга.
    HPEN hPenBeam = CreatePen(PS_SOLID, 2, greenColor);          // Для ЗЕЛЕНОГО луча (толщина 2).
    HPEN hPenTargetLine = CreatePen(PS_DOT, 1, targetLineColor); // Для БЕЛОЙ ПУНКТИРНОЙ линии к цели.
    HPEN hPenBaseOutline = CreatePen(PS_SOLID, 1, baseOutlineColor); // Для черного контура базы.
    // Временные объекты для маркеров обнаружения лучом (создаются/удаляются ВНУТРИ if(isOperationalStatus)).


    // --- 1. Отрисовка Базы Радара (Красная заливка с черным контуром) ---
    SelectObject(hdc, hPenBaseOutline); // Выбираем черное перо контура базы.
    SelectObject(hdc, hBrushBase);      // Выбираем кисть заливки базы.
    Ellipse(hdc, screenX - 10, screenY - 10, screenX + 10, screenY + 10); // Рисуем круг базы (радиус 10px).
    // !!! ВАЖНО !!!: Удаляем созданные GDI объекты базы СРАЗУ.
    DeleteObject(hBrushBase);      // Удаляем созданную кисть.
    DeleteObject(hPenBaseOutline); // Удаляем созданное перо контура.


    // --- 2. Отрисовка Зон (кругов), Луча и Маркеров ОБНАРУЖЕНИЯ (только если радар работает) ---
    if (isOperationalStatus) { // Рисуем эти элементы только если радар включен.

        SelectObject(hdc, GetStockObject(NULL_BRUSH)); // Круги зон без заливки.

        // 2.1. Отрисовка ВНЕШНЕГО ЗЕЛЕНОГО круга (Радиус = radar_range).
        SelectObject(hdc, hPenGreen); // Выбираем зеленое перо.
        Ellipse(hdc, screenX - (int)outerGreenRadius, screenY - (int)outerGreenRadius,
            screenX + (int)outerGreenRadius, screenY + (int)outerGreenRadius);

        // 2.2. Отрисовка СРЕДНЕГО ЖЕЛТОГО круга (Радиус = engagementRadius, Зона Поражения).
        SelectObject(hdc, hPenYellow); // Выбираем желтое перо.
        Ellipse(hdc, screenX - (int)middleYellowRadius, screenY - (int)middleYellowRadius,
            screenX + (int)middleYellowRadius, screenY + (int)middleYellowRadius);

        // 2.3. Отрисовка ВНУТРЕННЕГО КРАСНОГО круга (Мертвая зона, Радиус = deadZoneRadius).
        SelectObject(hdc, hPenRed); // Выбираем красное перо.
        Ellipse(hdc, screenX - (int)deadZoneRedRadius, screenY - (int)deadZoneRedRadius,
            screenX + (int)deadZoneRedRadius, screenY + (int)deadZoneRedRadius);


        // 2.4. Отрисовка ЛУЧА СКАНИРОВАНИЯ (Зеленый, толщина 2, из центра до внешнего Зеленого круга).
        SelectObject(hdc, hPenBeam); // Выбираем Зеленое перо для луча.
        float beamStartAngle = normalizeAngle(currentAngle - beamWidth / 2.0f);
        float beamEndAngle = normalizeAngle(currentAngle + beamWidth / 2.0f);
        Point p1_outer_world = { pos.x + outerGreenRadius * std::cos(beamStartAngle), pos.y + outerGreenRadius * std::sin(beamStartAngle) };
        Point p2_outer_world = { pos.x + outerGreenRadius * std::cos(beamEndAngle), pos.y + outerGreenRadius * std::sin(beamEndAngle) };
        int p1_outer_screenX = static_cast<int>(p1_outer_world.x + winCenterX);
        int p1_outer_screenY = static_cast<int>(-p1_outer_world.y + winCenterY); // Инверсия Y
        int p2_outer_screenX = static_cast<int>(p2_outer_world.x + winCenterX);
        int p2_outer_screenY = static_cast<int>(-p2_outer_world.y + winCenterY); // Инверсия Y
        MoveToEx(hdc, screenX, screenY, NULL); LineTo(hdc, p1_outer_screenX, p1_outer_screenY);
        MoveToEx(hdc, screenX, screenY, NULL); LineTo(hdc, p2_outer_screenX, p2_outer_screenY);


        // --- <<< НОВОЕ: Отрисовка маркеров на ВСЕХ ракетах, попадающих под ТЕКУЩИЙ ЛУЧ В ЗОНЕ ОБНАРУЖЕНИЯ >>> ---
        // Это визуальное отображение, какие ракеты "видит" сканирующий луч прямо сейчас.
        // Доступ к списку ракет из SimulationState:
        EnterCriticalSection(m_pCs); // *** Захват g_cs для потокобезопасного доступа к данным SimulationState! ***
        const std::vector<Missile>& activeMissilesRef = g_simulationState.getActiveMissilesUnsafe(); // Получаем список активных ракет.

        // Создаем временные GDI объекты для отрисовки маркеров (Желтый цвет, соответствующий средней зоне).
        HBRUSH hBrushMarker = CreateSolidBrush(yellowColor); // Желтая кисть.
        HPEN hPenMarker = CreatePen(PS_SOLID, 1, yellowColor);   // Желтое перо.
        // Сохраняем текущие объекты и выбираем наши временные маркеры.
        HBRUSH hOldBrushMarker = (HBRUSH)SelectObject(hdc, hBrushMarker);
        HPEN hOldPenMarker = (HPEN)SelectObject(hdc, hPenMarker);

        // Итерируем по КАЖДОЙ активной ракете.
        for (const auto& missile : activeMissilesRef) {
            if (missile.isActive) {
                float missileDist = missile.getDistanceToCenter();
                bool isInRangeRingNow = isMissileInRangeRingInternal(missileDist, outerGreenRadius, deadZoneRedRadius);
                bool isInBeamNow = Radar::isMissileInBeam(missile.pos, currentAngle, beamWidth);

                if (isInRangeRingNow && isInBeamNow) {
                    int missileScreenX = static_cast<int>(missile.pos.x + winCenterX);
                    int missileScreenY = static_cast<int>(-missile.pos.y + winCenterY);
                    int markerSize = 3;
                    Ellipse(hdc, missileScreenX - markerSize, missileScreenY - markerSize, missileScreenX + markerSize + 1, missileScreenY + markerSize + 1);
                }
            }
        } // Конец цикла по активным ракетам для отрисовки маркеров.


        // --- Отрисовка линии к ЗАПОМНЕННОЙ (отслеживаемой) цели ---
        // ЭТА ЛИНИЯ рисуется ТОЛЬКО к ОДНОЙ ракете (detectedMissileId), чей ID ЗАПОМНИЛ радар для сбития.
        // Указатель на отслеживаемую ракету pDetectedForDraw был получен внутри этого CS блока (если detectedMissileId != -1).
        const Missile* pDetectedForDraw = nullptr; // Объявление указателя.
        if (detectedMissileId != -1) { // Если есть ID отслеживаемой цели.
            auto itDetected = std::find_if(activeMissilesRef.begin(), activeMissilesRef.end(),
                [&](const Missile& m) { return m.isActive && m.id == detectedMissileId; });
            if (itDetected != activeMissilesRef.end()) { pDetectedForDraw = &(*itDetected); } // Нашли, сохраняем указатель.
        }

        LeaveCriticalSection(m_pCs); // *** ОСВОБОЖДЕНИЕ g_cs ***

        // !!! ВАЖНО !!!: Удаляем временные GDI объекты маркеров ПОСЛЕ освобождения CS и их использования. !!!
        SelectObject(hdc, hOldBrushMarker); // Восстанавливаем кисть.
        SelectObject(hdc, hOldPenMarker);   // Восстанавливаем перо.
        DeleteObject(hBrushMarker);         // Удаляем кисть.
        DeleteObject(hPenMarker);           // Удаляем перо.


        // --- Рисуем линию к ЗАПОМНЕННОЙ цели, если она найдена и активна ---
        // Выполняется ВНЕ блока CS.
        if (detectedMissileId != -1 && pDetectedForDraw) { // Проверяем, что есть ID отслеживания И указатель валиден.
            SelectObject(hdc, hPenTargetLine); // <<< ИСПРАВЛЕНИЕ: ВЫБРАТЬ ПЕРО ЛИНИИ К ЦЕЛИ ***ЗДЕСЬ***. Белый пунктир.
            MoveToEx(hdc, screenX, screenY, NULL); // Начинаем линию из центра радара.
            LineTo(hdc, static_cast<int>(pDetectedForDraw->pos.x + winCenterX), static_cast<int>(-pDetectedForDraw->pos.y + winCenterY));
        }
        // else: Линия не рисуется.

       // ... (Остальной код draw в блоке if (isOperationalStatus) - если есть что-то после отрисовки линии) ...


    } // Конец if (isOperationalStatus). Рисует зоны, луч, маркеры/линии.


    // --- Удаление всех созданных GDI объектов (перьев и кистей) ---
    // Удаляем перья и кисти, созданные Create...().
    // Делается после того, как они использованы И после восстановления старых объектов в HDC.
    DeleteObject(hPenGreen);       // Удаляем зеленое перо.
    DeleteObject(hPenYellow);      // Удаляем желтое перо.
    DeleteObject(hPenRed);         // Удаляем красное перо.
    DeleteObject(hPenBeam);        // Удаляем перо луча.
    DeleteObject(hPenTargetLine);  // Удаляем перо линии к цели.
    // hPenBaseOutline и hBrushBase уже были удалены после отрисовки базы.


    // --- Восстановление исходных GDI объектов контекста устройства ---
    // Восстанавливаем в HDC те перо и кисть, которые были активны ДО ВХОДА в этот метод draw().
    SelectObject(hdc, hOldPen);   // Восстанавливаем оригинальное перо.
    SelectObject(hdc, hOldBrush); // Восстанавливаем оригинальную кисть.
    // Объекты GetStockObject() не удаляются.

}

bool Radar::isOperational() const { // Геттер статуса работы
    CRITICAL_SECTION* pCs_non_const = const_cast<CRITICAL_SECTION*>(m_pCs); // const_cast
    EnterCriticalSection(pCs_non_const); // Захват g_cs
    bool operational = m_state.isOperational; // Чтение значения
    LeaveCriticalSection(pCs_non_const); // Освобождение g_cs
    return operational; // Возвращаем прочитанное значение
}
float Radar::getCurrentAngle() const { // Геттер текущего угла сканирования
    CRITICAL_SECTION* pCs_non_const = const_cast<CRITICAL_SECTION*>(m_pCs); 
    EnterCriticalSection(pCs_non_const); float angle = m_state.currentAngle; LeaveCriticalSection(pCs_non_const); return angle;
}
float Radar::getBeamWidth() const { // Геттер ширины луча
    CRITICAL_SECTION* pCs_non_const = const_cast<CRITICAL_SECTION*>(m_pCs);
    EnterCriticalSection(pCs_non_const); float width = m_state.beamWidth; LeaveCriticalSection(pCs_non_const); return width;
}
// Геттер для радиуса ВНЕШНЕГО ЗЕЛЕНОГО круга (из config.radar_range)
float Radar::getRange() const {
    CRITICAL_SECTION* pCs_non_const = const_cast<CRITICAL_SECTION*>(m_pCs);
    EnterCriticalSection(pCs_non_const); float range_val = m_state.radar_range; LeaveCriticalSection(pCs_non_const); return range_val;
}
// Геттер для радиуса СРЕДНЕГО ЖЕЛТОГО круга (ЗОНА ПОРАЖЕНИЯ, из config.radar_engagement_radius)
// ЭТО ОДНО ИЗ ОПРЕДЕЛЕНИЙ, НА КОТОРЫЕ ЖАЛОВАЛСЯ КОМПИЛЯТОР E0040/C2511. СИНТАКСИС ПРОВЕРЕН.
float Radar::getEngagementRadius() const
{
    CRITICAL_SECTION* pCs_non_const = const_cast<CRITICAL_SECTION*>(m_pCs);
    EnterCriticalSection(pCs_non_const); float radius = m_state.engagementRadius; LeaveCriticalSection(pCs_non_const); return radius;
}
// Геттер для радиуса ВНУТРЕННЕГО КРАСНОГО круга (МЕРТВАЯ ЗОНА, из config.danger_zone_radius)
float Radar::getDeadZoneRadius() const {
    CRITICAL_SECTION* pCs_non_const = const_cast<CRITICAL_SECTION*>(m_pCs); // const_cast
    EnterCriticalSection(pCs_non_const); float radius = m_state.deadZoneRadius; LeaveCriticalSection(pCs_non_const); return radius;
}

// Геттеры для информации об ОБНАРУЖЕННОЙ цели (ID и время обнаружения).
// Эти геттеры используются в SimulationState::update для реализации логики сбития/потери.
int Radar::getDetectedMissileId() const {
    CRITICAL_SECTION* pCs_non_const = const_cast<CRITICAL_SECTION*>(m_pCs); // const_cast
    EnterCriticalSection(pCs_non_const); int id = m_state.detectedMissileId; LeaveCriticalSection(pCs_non_const); return id;
}
float Radar::getDetectionTime() const {
    CRITICAL_SECTION* pCs_non_const = const_cast<CRITICAL_SECTION*>(m_pCs); // const_cast
    EnterCriticalSection(pCs_non_const); float time = m_state.detectionTime; LeaveCriticalSection(pCs_non_const); return time;
}
void Radar::setOperational(bool operational) {
    EnterCriticalSection(m_pCs); // Захват глобальной CS для безопасного изменения m_state.
    m_state.isOperational = operational; // Изменяем статус работы.
    if (!operational) { // Если статус изменился на НЕрабочий
        // Сбрасываем обнаруженную цель - радар не может отслеживать, если не работает.
        m_state.detectedMissileId = -1;
        m_state.detectionTime = 0.0f;
    }
    LeaveCriticalSection(m_pCs); // Освобождение глобальной CS.
} // Конец setOperational()

// clearDetectedMissile: Сбрасывает информацию об обнаруженной цели (устанавливает detectedMissileId в -1).
// Вызывается из SimulationState::update, когда отслеживаемая цель была уничтожена, ушла в мертвую зону, или стала неактивна по другой причине.
void Radar::clearDetectedMissile() {
    EnterCriticalSection(m_pCs); // Захват глобальной CS для безопасного изменения m_state.
    m_state.detectedMissileId = -1; // Сброс ID цели (-1 означает "нет цели").
    m_state.detectionTime = 0.0f; // Сброс времени обнаружения.
    LeaveCriticalSection(m_pCs); // Освобождение глобальной CS.
} 
