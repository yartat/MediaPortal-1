// A rasterizer whose path is given by the test (the path is allocated with malloc: the rasterizer frees it with free).
// Shared by DrawingOverlayTests.cpp (CI) and DrawingOverlayHeavyTests.cpp (local only).
#pragma once
#include <cstdlib>
#include <cstring>
#include "Rasterizer.h"

class DrawingRasterizer : public Rasterizer
{
public:
  // a rectangle of width x height pixels, in 1/64 pixel as the coordinates of a drawing are
  void SetRectangle(long widthPixels, long heightPixels)
  {
    const long w = widthPixels * 64;
    const long h = heightPixels * 64;
    const POINT points[4] = { { 0, 0 }, { w, 0 }, { w, h }, { 0, h } };
    const BYTE types[4] = { PT_MOVETO, PT_LINETO, PT_LINETO, static_cast<BYTE>(PT_LINETO | PT_CLOSEFIGURE) };
    mpPathTypes = static_cast<BYTE*>(malloc(sizeof(types)));
    mpPathPoints = static_cast<POINT*>(malloc(sizeof(points)));
    memcpy(mpPathTypes, types, sizeof(types));
    memcpy(mpPathPoints, points, sizeof(points));
    mPathPoints = 4;
  }
};
