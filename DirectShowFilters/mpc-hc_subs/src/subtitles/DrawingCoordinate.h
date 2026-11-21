#pragma once

#include <algorithm>
#include <cmath>
#include <windows.h>

// MS-06: a coordinate of an ASS drawing ({\p1}m 100000000 0) comes from the file. It is limited to +-2^20 pixels, so that the value
// in 1/64 pixel (x 64) fits a LONG. NaN (from "nan") counts as 0: lround() of it is undefined.
inline LONG DrawingCoordinate(double value, double scale)
{
    double v = value * scale;
    if (!(v == v)) {
        return 0;
    }
    v = (std::max)(-1048576.0, (std::min)(1048576.0, v));
    return std::lround(v) * 64;
}
