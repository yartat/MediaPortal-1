// mpc-hc_subs: decoding of a DVD (VobSub) subpicture packet (SECURITY_AUDIT.md: MS-05).
// The offsets of the two fields (interlaced lines) and the position of the control block come from the packet, but the decoder
// checks them against the size of the data, not the size of the packet.
// The packet is placed in a GuardedBuffer: reading the byte after its last byte is an access violation.
#include "stdafx.h"
#include <gtest/gtest.h>
#include <list>
#include <memory>
#include <vector>
#include "VobSubImage.h"
#include "GuardedBuffer.h"

namespace
{
  // A control block for 'dataSize' bytes of data: start time, next block (itself), palette entry 1 = colour 1, transparency 0xF,
  // area, offsets of the two fields. 'rect' is the 6 bytes of command 5.
  std::vector<uint8_t> ControlBlock(unsigned dataSize, const std::vector<uint8_t>& rect, unsigned offset0, unsigned offset1,
                                    uint8_t timeHi = 0, uint8_t timeLo = 0)
  {
    std::vector<uint8_t> c = { timeHi, timeLo, static_cast<uint8_t>(dataSize >> 8), static_cast<uint8_t>(dataSize),
                               0x03, 0x11, 0x11, 0x04, 0x11, 0x11, 0x05 };
    c.insert(c.end(), rect.begin(), rect.end());
    c.insert(c.end(), { 0x06, static_cast<uint8_t>(offset0 >> 8), static_cast<uint8_t>(offset0),
                        static_cast<uint8_t>(offset1 >> 8), static_cast<uint8_t>(offset1), 0xFF });
    return c;
  }

  struct Palettes
  {
    RGBQUAD original[16];
    RGBQUAD custom[4];
    Palettes()
    {
      for (auto& c : original)
        c = RGBQUAD{ 10, 20, 30, 0 };
      for (auto& c : custom)
        c = RGBQUAD{ 1, 2, 3, 4 };
    }
  };
}

// The reference: a 16 x 2 picture. Each line is one run of 16 pixels in colour 1 (nibbles 0 4 1 and a padding nibble). The first
// line is in field 0 (offset 4), the second in field 1 (offset 6); the control block follows the data (offset 8).
TEST(VobSubImage, ValidPacketIsDecoded)
{
  std::vector<uint8_t> packet = { 0x00, 0x28, 0x00, 0x08,  0x04, 0x10,  0x04, 0x10 };
  const std::vector<uint8_t> control = ControlBlock(8, { 0x00, 0x00, 0x0F, 0x00, 0x00, 0x01 }, 4, 6);
  packet.insert(packet.end(), control.begin(), control.end());
  packet.resize(40, 0);
  GuardedBuffer g(packet);
  Palettes palettes;
  CVobSubImage image;
  ASSERT_TRUE(image.Decode(g.data(), 40, 8, INT_MAX, false, 0, palettes.original, palettes.custom, false));
  ASSERT_EQ(image.rect.right - image.rect.left, 16);
  ASSERT_EQ(image.rect.bottom - image.rect.top, 2);
  for (int i = 0; i < 32; i++)
  {
    EXPECT_EQ(image.lpPixels[i].rgbBlue, 10) << "pixel " << i;
    EXPECT_EQ(image.lpPixels[i].rgbReserved, 0x11) << "pixel " << i;
  }
}

// MS-05: the offset of field 1 is 65 535: field 0 is decoded up to it, not up to the end of the data or of the packet. The rest of
// the packet and the area behind it are read as codes of one-pixel runs (the line is 3 700 pixels wide).
TEST(VobSubImage, OffsetOfTheSecondFieldBehindThePacketIsNotFollowed)
{
  const unsigned packetSize = 1200;
  const unsigned dataSize = 0x0444;                  // 1092
  std::vector<uint8_t> packet(packetSize, 0x44);     // nibble 4: a run of one pixel
  const std::vector<uint8_t> control = ControlBlock(dataSize, { 0x01, 0x1F, 0xFF, 0x11, 0x11, 0x11 }, 0x0111, 0xFFFF, 0x44, 0x44);
  std::copy(control.begin(), control.end(), packet.begin() + dataSize);
  GuardedBuffer g(packet);
  Palettes palettes;
  CVobSubImage image;
  EXPECT_NO_THROW(image.Decode(g.data(), packetSize, dataSize, INT_MAX, false, 0, palettes.original, palettes.custom, false));
}

// MS-05: the control block is expected at the end of the data, and the data is as long as the packet: the first read of the
// control block is behind the packet.
TEST(VobSubImage, DataSizeEqualToThePacketSizeIsNotRead)
{
  GuardedBuffer g(20, 0x11);
  Palettes palettes;
  CVobSubImage image;
  EXPECT_NO_THROW(image.Decode(g.data(), 20, 20, INT_MAX, false, 0, palettes.original, palettes.custom, false));
}
