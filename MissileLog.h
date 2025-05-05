#pragma once

#include <vector>    
#include <string>    
#include <windows.h> 


// --- Структура для одной записи в журнале событий ---
struct MissileLogEntry {
    int missileId;    // ID ракеты
    int launcherId;   // ID пусковой
    float timestamp;  // Игровое время
    std::wstring status; // Описание события
};


// Объявляем extern глобальную критическую секцию.
// Определена и инициализирована в main.cpp. MissileLog будет использовать указатель на нее.
extern CRITICAL_SECTION g_cs;


// --- Класс Журнала Событий ---
class MissileLog {
private:
    std::vector<MissileLogEntry> m_entries; // Записи лога
    CRITICAL_SECTION* m_pCs; // Указатель на глобальную CS

public:
    MissileLog(); // Конструктор

    // --- Методы ---
    void initialize(CRITICAL_SECTION* pCs); // Инициализация лога с CS
    MissileLogEntry getLastEntryForMissile(int missileId) const;
    // Потокобезопасные методы
    void addEntry(int missileId, int launcherId, float timestamp, const std::wstring& status);
    // Получение последних записей. Значение по умолчанию 10. const-корректный.
    std::vector<MissileLogEntry> getLastEntries(size_t count = 10) const;
    void clear(); // Очистка лога

    // // Если нужен метод отрисовки лога, то добавтье объявление здесь.
    // void draw(HDC hdc, int x, int y, int maxLines) const;
};
