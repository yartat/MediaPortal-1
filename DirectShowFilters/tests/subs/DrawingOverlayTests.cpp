// mpc-hc_subs: the size of the overlay of a vector drawing of an ASS subtitle (SECURITY_AUDIT.md: MS-06).
// The coordinates of a drawing ({\p1}m 0 0 l 4000000 0 ...) come from the file. Rasterizer::Rasterize() computes the size of the
// overlay as mOverlayPitch * mOverlayHeight in int, allocates that many bytes with _aligned_malloc and then writes the spans of the
// outline at pitch * row + column. The huge cases (several GB) are in DrawingOverlayHeavyTests.cpp, which runs locally only.
#include "stdafx.h"
#include <gtest/gtest.h>
#include "DrawingRasterizer.h"

// The reference: a drawing of 100 x 50 pixels gives an overlay of 100 + 1 columns (in pixels) and 50 + 2 rows, and rasterizes.
TEST(DrawingOverlay, SmallDrawingIsRasterized)
{
  DrawingRasterizer r;
  r.SetRectangle(100, 50);
  ASSERT_TRUE(r.ScanConvert());
  ASSERT_TRUE(r.Rasterize(0, 0, 0, 0.0));
  EXPECT_GE(r.getOverlayWidth(), 800);              // in 1/8 pixel: 100 pixels and the border of the overlay
  EXPECT_LE(r.getOverlayWidth(), 960);
}
