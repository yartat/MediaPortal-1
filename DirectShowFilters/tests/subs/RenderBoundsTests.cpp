// mpc-hc_subs: drawing of RLE subtitle objects (PGS "HDMV" and DVB) into a bitmap (SECURITY_AUDIT.md: MS-03).
// The position of an object and the lengths of the runs come from the stream; Rasterizer::FillSolidRect() checks the rectangle
// only with an ASSERT, which does nothing in a Release build.
// The bitmap is placed in a GuardedBuffer: a write past its end (or, with Guard::Before, before its start) is an access violation.
#include "stdafx.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include "CompositionObject.h"
#include "../SubPic/ISubPic.h"
#include "GuardedBuffer.h"

namespace
{
  const int kWidth = 64;
  const int kHeight = 16;
  const uint32_t kSentinel = 0x11111111;

  struct Frame
  {
    GuardedBuffer buffer;
    SubPicDesc desc;

    explicit Frame(GuardedBuffer::Guard guard = GuardedBuffer::Guard::After)
      : buffer(static_cast<size_t>(kWidth) * kHeight * 4, 0x11, guard)
    {
      desc.type = 0;
      desc.w = kWidth;
      desc.h = kHeight;
      desc.bpp = 32;
      desc.pitch = kWidth * 4;
      desc.bits = buffer.data();
    }

    uint32_t Pixel(int x, int y) const
    {
      return reinterpret_cast<const uint32_t*>(buffer.data())[y * kWidth + x];
    }

    // true when every pixel outside the rectangle still has the value of the start
    bool OutsideUntouched(int left, int top, int right, int bottom) const
    {
      for (int y = 0; y < kHeight; y++)
        for (int x = 0; x < kWidth; x++)
          if (!(x >= left && x < right && y >= top && y < bottom) && Pixel(x, y) != kSentinel)
            return false;
      return true;
    }
  };

  // an object with the entries 1 and 2 of its palette opaque
  struct Object
  {
    CompositionObject object;

    explicit Object(const std::vector<BYTE>& rle)
    {
      HDMV_PALETTE palette[2] = { { 1, 0x80, 0x80, 0x80, 0xFF }, { 2, 0xB0, 0x60, 0xA0, 0xFF } };
      object.SetPalette(2, palette, ColorConvTable::BT601);
      object.SetRLEData(rle.data(), rle.size(), rle.size());
    }
  };

  // HDMV run-length coding: "00 11cccccc cccccccc ii" is a run of 14 bit length with the colour ii, "00 00" ends the line
  std::vector<BYTE> HdmvRun(unsigned count, BYTE colour)
  {
    return { 0x00, static_cast<BYTE>(0xC0 | (count >> 8)), static_cast<BYTE>(count), colour, 0x00, 0x00 };
  }

  // one pixel per code (a code other than 0 is a colour), 'width' pixels per line, 'lines' lines
  std::vector<BYTE> HdmvPixels(int width, int lines, BYTE colour)
  {
    std::vector<BYTE> rle;
    for (int line = 0; line < lines; line++)
    {
      rle.insert(rle.end(), static_cast<size_t>(width), colour);
      rle.push_back(0x00);
      rle.push_back(0x00);
    }
    return rle;
  }

  // DVB object data: top field length, bottom field length, then the fields
  std::vector<BYTE> DvbObject(const std::vector<BYTE>& topField)
  {
    std::vector<BYTE> d = { static_cast<BYTE>(topField.size() >> 8), static_cast<BYTE>(topField.size()), 0x00, 0x00 };
    d.insert(d.end(), topField.begin(), topField.end());
    return d;
  }

  // 8 bit code string: "12", then "00 1rrrrrrr ii" is a run of r pixels with the colour ii, "00 00" ends the string
  std::vector<BYTE> Dvb8BitRun(unsigned count, BYTE colour)
  {
    return { 0x12, 0x00, static_cast<BYTE>(0x80 | count), colour, 0x00, 0x00 };
  }
}

// ---------------------------------------------------------------------------
// PGS (HDMV)
// ---------------------------------------------------------------------------

// The reference: an object inside the frame is drawn, and only there.
TEST(RleRenderBounds, HdmvObjectInsideTheFrameIsDrawnWhereItShouldBe)
{
  Frame frame;
  Object o(HdmvPixels(16, 2, 1));
  o.object.m_horizontal_position = 8;
  o.object.m_vertical_position = 2;
  o.object.m_width = 16;
  o.object.m_height = 2;
  o.object.RenderHdmv(frame.desc);
  EXPECT_NE(frame.Pixel(8, 2), kSentinel);
  EXPECT_NE(frame.Pixel(23, 3), kSentinel);
  EXPECT_TRUE(frame.OutsideUntouched(8, 2, 24, 4));
}

// The reference for the last line: an object that ends exactly at the right edge of the last line is drawn completely.
TEST(RleRenderBounds, HdmvObjectInTheLastLineIsDrawn)
{
  Frame frame;
  Object o(HdmvPixels(16, 1, 1));
  o.object.m_horizontal_position = kWidth - 16;
  o.object.m_vertical_position = kHeight - 1;
  o.object.m_width = 16;
  o.object.m_height = 1;
  o.object.RenderHdmv(frame.desc);
  EXPECT_NE(frame.Pixel(kWidth - 16, kHeight - 1), kSentinel);
  EXPECT_NE(frame.Pixel(kWidth - 1, kHeight - 1), kSentinel);
  EXPECT_TRUE(frame.OutsideUntouched(kWidth - 16, kHeight - 1, kWidth, kHeight));
}

// MS-03: the object is in the last line and its run is 16 383 pixels: 65 532 bytes are written behind the bitmap.
TEST(RleRenderBounds, HdmvRunLongerThanTheLineStaysInsideTheBitmap)
{
  Frame frame;
  Object o(HdmvRun(16383, 1));
  o.object.m_horizontal_position = 0;
  o.object.m_vertical_position = kHeight - 1;
  o.object.m_width = kWidth;
  o.object.m_height = 1;
  o.object.RenderHdmv(frame.desc);
  EXPECT_TRUE(frame.OutsideUntouched(0, kHeight - 1, kWidth, kHeight));
}

// MS-03: the same in a line in the middle: the run goes on into the following lines of the bitmap (no access violation, but the
// object overwrites what it does not own: the lines below it).
TEST(RleRenderBounds, HdmvRunLongerThanTheLineDoesNotSpillIntoTheNextLines)
{
  Frame frame;
  Object o(HdmvRun(200, 1));
  o.object.m_horizontal_position = 0;
  o.object.m_vertical_position = 2;
  o.object.m_width = kWidth;
  o.object.m_height = 1;
  o.object.RenderHdmv(frame.desc);
  EXPECT_TRUE(frame.OutsideUntouched(0, 2, kWidth, 3));
}

// MS-03: the position is a signed 16 bit number; a negative one passes the check of Render() (only the upper bounds are compared).
TEST(RleRenderBounds, HdmvObjectLeftOfTheBitmapStaysInsideTheBitmap)
{
  Frame frame(GuardedBuffer::Guard::Before);
  Object o(HdmvRun(200, 1));
  o.object.m_horizontal_position = -100;
  o.object.m_vertical_position = 0;
  o.object.m_width = 200;
  o.object.m_height = 1;
  o.object.RenderHdmv(frame.desc);
  EXPECT_TRUE(frame.OutsideUntouched(0, 0, kWidth, 1));
}

TEST(RleRenderBounds, HdmvObjectAboveTheBitmapStaysInsideTheBitmap)
{
  Frame frame(GuardedBuffer::Guard::Before);
  Object o(HdmvRun(10, 1));
  o.object.m_horizontal_position = 0;
  o.object.m_vertical_position = -3;
  o.object.m_width = 10;
  o.object.m_height = 1;
  o.object.RenderHdmv(frame.desc);
  EXPECT_TRUE(frame.OutsideUntouched(0, 0, kWidth, 0));
}

// MS-03: below the bitmap
TEST(RleRenderBounds, HdmvObjectBelowTheBitmapStaysInsideTheBitmap)
{
  Frame frame;
  Object o(HdmvRun(10, 1));
  o.object.m_horizontal_position = 0;
  o.object.m_vertical_position = kHeight;
  o.object.m_width = 10;
  o.object.m_height = 1;
  o.object.RenderHdmv(frame.desc);
  EXPECT_TRUE(frame.OutsideUntouched(0, 0, kWidth, 0));
}

// ---------------------------------------------------------------------------
// DVB
// ---------------------------------------------------------------------------

TEST(RleRenderBounds, DvbObjectInsideTheFrameIsDrawn)
{
  Frame frame;
  Object o(DvbObject(Dvb8BitRun(10, 1)));
  o.object.RenderDvb(frame.desc, 4, 2);
  EXPECT_NE(frame.Pixel(4, 2), kSentinel);
  EXPECT_NE(frame.Pixel(13, 2), kSentinel);
  EXPECT_TRUE(frame.OutsideUntouched(4, 2, 14, 3));
}

// MS-03: a run of 127 pixels in the last line of a 64 pixel wide bitmap
TEST(RleRenderBounds, DvbRunLongerThanTheLineStaysInsideTheBitmap)
{
  Frame frame;
  Object o(DvbObject(Dvb8BitRun(127, 1)));
  o.object.RenderDvb(frame.desc, 0, kHeight - 2);   // the top field is drawn into line 14, the bottom field into 15
  EXPECT_TRUE(frame.OutsideUntouched(0, kHeight - 2, kWidth, kHeight - 1));
}

TEST(RleRenderBounds, DvbRunInTheLastLineStaysInsideTheBitmap)
{
  Frame frame;
  Object o(DvbObject(Dvb8BitRun(127, 1)));
  o.object.RenderDvb(frame.desc, 0, kHeight - 1);
  EXPECT_TRUE(frame.OutsideUntouched(0, kHeight - 1, kWidth, kHeight));
}

// MS-03: the position of the object is a signed 16 bit number from the stream (region position plus object position)
TEST(RleRenderBounds, DvbObjectBelowTheBitmapStaysInsideTheBitmap)
{
  Frame frame;
  Object o(DvbObject(Dvb8BitRun(10, 1)));
  o.object.RenderDvb(frame.desc, 0, kHeight);
  EXPECT_TRUE(frame.OutsideUntouched(0, 0, kWidth, 0));
}

TEST(RleRenderBounds, DvbObjectLeftOfAndAboveTheBitmapStaysInsideTheBitmap)
{
  Frame frame(GuardedBuffer::Guard::Before);
  Object o(DvbObject(Dvb8BitRun(10, 1)));
  o.object.RenderDvb(frame.desc, -100, 0);
  o.object.RenderDvb(frame.desc, 0, -4);
  EXPECT_TRUE(frame.OutsideUntouched(0, 0, kWidth, 1));
}
