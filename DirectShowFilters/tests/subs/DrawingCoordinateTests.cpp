// mpc-hc_subs: the coordinates of an ASS drawing ({\p1}m 100000000 0 ...) come from the file (SECURITY_AUDIT.md: MS-06).
// CPolygon::GetPOINT() multiplied the rounded value by 64 in a LONG: a large value wrapped around (and "nan" was undefined).
// The calculation is in DrawingCoordinate.h, so that it can be tested without the whole subtitle renderer.
#include "stdafx.h"
#include <gtest/gtest.h>
#include <limits>
#include "DrawingCoordinate.h"

TEST(DrawingCoordinate, OrdinaryCoordinatesAreInSixtyFourthsOfPixel)
{
  EXPECT_EQ(DrawingCoordinate(10, 1.0), 640);
  EXPECT_EQ(DrawingCoordinate(-20, 1.0), -1280);
  EXPECT_EQ(DrawingCoordinate(10, 2.0), 1280);        // the scale of the drawing
  EXPECT_EQ(DrawingCoordinate(0.5, 1.0), 64);         // rounds to the pixel
}

TEST(DrawingCoordinate, HugeCoordinatesAreLimitedAndKeepTheirSign)
{
  EXPECT_EQ(DrawingCoordinate(100000000.0, 1.0), 1048576 * 64);
  EXPECT_EQ(DrawingCoordinate(-100000000.0, 1.0), -1048576 * 64);
  EXPECT_EQ(DrawingCoordinate(1e30, 1.0), 1048576 * 64);
  EXPECT_EQ(DrawingCoordinate(3000000000.0, 1.0), 1048576 * 64);   // would be -1 294 967 296 after the cast
  EXPECT_EQ(DrawingCoordinate(1048576.0, 1.0), 1048576 * 64);      // the limit itself is kept
}

TEST(DrawingCoordinate, NotANumberAndInfinityAreHandled)
{
  const double inf = std::numeric_limits<double>::infinity();
  EXPECT_EQ(DrawingCoordinate(std::numeric_limits<double>::quiet_NaN(), 1.0), 0);
  EXPECT_EQ(DrawingCoordinate(inf, 1.0), 1048576 * 64);
  EXPECT_EQ(DrawingCoordinate(-inf, 1.0), -1048576 * 64);
  EXPECT_EQ(DrawingCoordinate(inf, 0.0), 0);                       // inf * 0 is NaN
}
