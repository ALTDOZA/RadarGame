#pragma once

#include "Point.h"
#include <windows.h>

class Launcher {
public:
    Point pos;
    int launcherId;

    Launcher();
    Launcher(Point p, int id);
    void draw(HDC hdc, int winCenterX, int winCenterY) const;
};
