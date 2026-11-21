// MPIPTVSource, MPIPTV_RTP: RtpPacket and RtpSource (SECURITY_AUDIT.md: RT-01).
// Every RTP packet comes from a UDP datagram of the network, so the checks of IsRtpPacket() are the only protection of the chain.
#include <gtest/gtest.h>
#include <windows.h>
#include <climits>
#include <cstring>
#include <vector>
#include "RtpPacket.h"
#include "RtpSource.h"

namespace
{
  // A packet as the sender builds it: version 2, an optional padding (the last byte is the padding length), CSRC identifiers.
  std::vector<char> MakePacket(unsigned seq, size_t payload, unsigned csrcCount = 0, unsigned padding = 0)
  {
    std::vector<char> p(12 + 4 * csrcCount + payload + padding, 0);
    p[0] = static_cast<char>(0x80 | (padding ? 0x20 : 0) | csrcCount);
    p[1] = 0x21;                                     // payload type 33: MPEG-2 transport stream
    p[2] = static_cast<char>(seq >> 8);
    p[3] = static_cast<char>(seq);
    p[11] = 1;                                       // SSRC
    for (size_t i = 0; i < payload; i++)
      p[12 + 4 * csrcCount + i] = static_cast<char>(i);
    if (padding)
      p.back() = static_cast<char>(padding);
    return p;
  }

  bool IsRtp(std::vector<char> bytes)
  {
    RtpPacket packet(bytes.data(), static_cast<unsigned>(bytes.size()), NULL, NULL);
    return packet.IsRtpPacket();
  }
}

TEST(RtpPacket, ValidPacketIsRecognised)
{
  EXPECT_TRUE(IsRtp(MakePacket(1, 188)));
  EXPECT_TRUE(IsRtp(MakePacket(1, 188, 3)));
  EXPECT_TRUE(IsRtp(MakePacket(1, 188, 0, 4)));
  EXPECT_TRUE(IsRtp(MakePacket(1, 0)));
}

TEST(RtpPacket, ValidPacketGivesItsData)
{
  std::vector<char> bytes = MakePacket(7, 188, 2, 4);
  RtpPacket packet(bytes.data(), static_cast<unsigned>(bytes.size()), NULL, NULL);
  ASSERT_TRUE(packet.IsRtpPacket());
  EXPECT_EQ(packet.GetSequenceNumber(), 7u);
  EXPECT_EQ(packet.GetDataLength(), 188u);
  std::vector<char> out(188);
  EXPECT_EQ(packet.GetData(out.data(), 188), 188u);
  EXPECT_EQ(out[0], 0);
  EXPECT_EQ(out[187], static_cast<char>(187));
}

// RT-01: one byte less than the header with its CSRC list. GetDataLength() is UINT_MAX, and 12 + 4 * CC + 0 + UINT_MAX wraps
// around to the length of the packet, so the packet passes.
TEST(RtpPacket, PacketShorterThanItsHeaderIsNotRtp)
{
  for (unsigned csrcCount = 1; csrcCount <= 15; csrcCount++)
  {
    SCOPED_TRACE(csrcCount);
    std::vector<char> bytes(12 + 4 * csrcCount - 1, 0);
    bytes[0] = static_cast<char>(0x80 | csrcCount);
    EXPECT_FALSE(IsRtp(bytes));
  }
}

// RT-01: the same with the padding: the last byte says that 3 (or 255) bytes are padding, but there is no room for them.
TEST(RtpPacket, PaddingLongerThanThePacketIsNotRtp)
{
  for (unsigned padding : { 3u, 100u, 255u })
  {
    SCOPED_TRACE(padding);
    std::vector<char> bytes(12 + padding - 1, 0);
    bytes[0] = static_cast<char>(0x80 | 0x20);
    bytes.back() = static_cast<char>(padding);
    EXPECT_FALSE(IsRtp(bytes));
  }
}

TEST(RtpPacket, WrongVersionIsNotRtp)
{
  std::vector<char> bytes = MakePacket(1, 188);
  bytes[0] = 0x40;
  EXPECT_FALSE(IsRtp(bytes));
}

namespace
{
  // reads everything the source can give with up to 'calls' calls
  std::vector<char> Drain(RtpSource& source, int calls, bool getUncontinuous = false)
  {
    std::vector<char> all;
    for (int i = 0; i < calls; i++)
    {
      std::vector<char> buffer(188 * 256);
      unsigned first = 0, last = 0;
      unsigned n = source.GetAndRemovePacketData(buffer.data(), static_cast<unsigned>(buffer.size()), &first, &last, getUncontinuous);
      if (n != UINT_MAX)
        all.insert(all.end(), buffer.begin(), buffer.begin() + n);
    }
    return all;
  }
}

TEST(RtpSource, PacketsInOrderGiveTheirDataInOrder)
{
  CLogger logger;
  RtpSource source(&logger);
  for (unsigned seq = 10; seq < 20; seq++)
  {
    std::vector<char> p = MakePacket(seq, 4);
    p[12] = static_cast<char>(seq);
    ASSERT_TRUE(source.ProcessPacket(p.data(), static_cast<unsigned>(p.size())));
  }
  std::vector<char> all = Drain(source, 1);
  ASSERT_EQ(all.size(), 40u);
  for (unsigned i = 0; i < 10; i++)
    EXPECT_EQ(all[i * 4], static_cast<char>(10 + i));
}

TEST(RtpSource, PacketsOutOfOrderAreSorted)
{
  CLogger logger;
  RtpSource source(&logger);
  for (unsigned seq : { 12u, 10u, 13u, 11u })
  {
    std::vector<char> p = MakePacket(seq, 4);
    p[12] = static_cast<char>(seq);
    ASSERT_TRUE(source.ProcessPacket(p.data(), static_cast<unsigned>(p.size())));
  }
  std::vector<char> all = Drain(source, 1);
  ASSERT_EQ(all.size(), 16u);
  for (unsigned i = 0; i < 4; i++)
    EXPECT_EQ(all[i * 4], static_cast<char>(10 + i));
}

// RT-01: after the first packet nothing can be read (GetPacketData() answers 0 for a packet whose data length is UINT_MAX),
// GetAndRemovePacketData() removes nothing, and every following packet is only added behind it.
TEST(RtpSource, UnreadablePacketAtTheFrontDoesNotBlockTheStream)
{
  CLogger logger;
  RtpSource source(&logger);

  // the first packet: 15 bytes, CC = 1, sequence number 5 (the packet that passes IsRtpPacket() because of the wrap-around)
  std::vector<char> bad(15, 0);
  bad[0] = static_cast<char>(0x81);
  bad[3] = 5;
  source.ProcessPacket(bad.data(), static_cast<unsigned>(bad.size()));

  for (unsigned seq = 6; seq < 106; seq++)
  {
    std::vector<char> p = MakePacket(seq, 188);
    ASSERT_TRUE(source.ProcessPacket(p.data(), static_cast<unsigned>(p.size()))) << "sequence number " << seq;
  }

  // three calls are allowed: a fix may refuse the bad packet at once, or remove it on the first call
  std::vector<char> all = Drain(source, 3);
  EXPECT_EQ(all.size(), 188u * 100) << "the data of the 100 good packets did not come out";
}

// Not in the audit (found by the test above): a packet older than the first one of the chain is not stored. AddPacket() tests
// "the packet is before the first one" with <= 0, but PacketSequenceNumberDifference(packet, first) is positive for it,
// so the loop ends with both neighbours NULL, no branch inserts it, and ProcessPacket() still answers true (and the packet leaks).
TEST(RtpSource, PacketOlderThanTheFirstOneIsKept)
{
  CLogger logger;
  RtpSource source(&logger);
  for (unsigned seq : { 12u, 11u })
  {
    std::vector<char> p = MakePacket(seq, 4);
    p[12] = static_cast<char>(seq);
    ASSERT_TRUE(source.ProcessPacket(p.data(), static_cast<unsigned>(p.size())));
  }
  std::vector<char> all = Drain(source, 1);
  ASSERT_EQ(all.size(), 8u);
  EXPECT_EQ(all[0], 11);
  EXPECT_EQ(all[4], 12);
}
