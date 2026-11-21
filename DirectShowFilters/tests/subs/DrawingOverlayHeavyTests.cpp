// mpc-hc_subs: huge vector drawings of an ASS subtitle (SECURITY_AUDIT.md: MS-06). LOCAL ONLY (target mp_subs_heavy_tests, label
// "local", built with -DMP_BUILD_LOCAL_TESTS=ON): the cases need several GB of memory and take a while, so they never run in CI.
//
// Rasterizer::Rasterize() computes the size of the overlay as mOverlayPitch * mOverlayHeight in int, allocates that many bytes with
// _aligned_malloc and then writes the spans of the outline at pitch * row + column: when the product wraps around to a small
// positive number, the buffer is small and the writes are not.
//
// Each case runs in a child process (a death test): if the code survives (it refuses the drawing, or works with a size that is
// right) the child exits with 0; a write outside the buffer ends it.
#include "stdafx.h"
#include <gtest/gtest.h>
#include <cstdio>
#include "DrawingRasterizer.h"

namespace
{
  // converts and rasterizes a rectangle of the given size in a child; exit(0) when it comes back
  void ConvertAndRasterize(long widthPixels, long heightPixels)
  {
    DrawingRasterizer r;
    r.SetRectangle(widthPixels, heightPixels);
    const bool converted = r.ScanConvert();
    fprintf(stderr, "[child] converted=%d\n", converted);
    if (converted)
      fprintf(stderr, "[child] rasterized=%d\n", r.Rasterize(0, 0, 0, 0.0));
    exit(0);
  }
}

// MS-06: 65 552 x 65 536 pixels: the overlay pitch is 65 568 and the height 65 539, the product is 4 297 000 000 or so, which wraps
// around to a size of about a megabyte (the exact numbers do not matter: they are over 2^32). The rows written are the real ones.
TEST(DrawingOverlayHeavyDeathTest, SizeThatWrapsAroundDoesNotWriteOutsideTheBuffer)
{
  EXPECT_EXIT(ConvertAndRasterize(65552, 65536), ::testing::ExitedWithCode(0), "");
}

// 40 000 x 40 000 pixels: the drawing must be refused, or rasterized without a write outside the buffer; it must not take the
// machine down. Needs a 64-bit build (result on the unfixed code: the child comes back in about a second).
TEST(DrawingOverlayHeavyDeathTest, DrawingOfThreeGigabytesIsRefusedOrRasterizedSafely)
{
  EXPECT_EXIT(ConvertAndRasterize(40000, 40000), ::testing::ExitedWithCode(0), "");
}

// 100 000 x 100 000 pixels: coordinates of a drawing in 1/64 pixel are 6.4e6, far from overflowing a LONG, and the overlay size
// overflows int. ScanConvert accepts it; Rasterize must refuse it or fail cleanly (result on the unfixed code: the child ends with
// exit status 1 inside Rasterize, the cause is not established yet).
TEST(DrawingOverlayHeavyDeathTest, DrawingTooBigForMemoryDoesNotWriteThroughNull)
{
  EXPECT_EXIT(ConvertAndRasterize(100000, 100000), ::testing::ExitedWithCode(0), "");
}
