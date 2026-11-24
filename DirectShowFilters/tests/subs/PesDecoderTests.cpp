// DVBSubtitle3: CPesDecoder and its copy of CTsHeader take packets from the air (SECURITY_AUDIT.md: PES-01, TS-01 copy).
#include <gtest/gtest.h>
#include <windows.h>
#include <vector>
#include "../../DVBSubtitle3/Source/PesDecoder/PesDecoder.h"
#include "../../DVBSubtitle3/Source/PesDecoder/TsHeader.h"
#include "GuardedBuffer.h"

namespace
{
  struct Collector : CPesCallback
  {
    int calls = 0;
    int lastHeaderLen = 0;
    std::vector<byte> lastData;
    int OnNewPesPacket(int, byte*, int headerlen, byte* data, int len, bool) override
    {
      ++calls;
      lastHeaderLen = headerlen;
      lastData.assign(data, data + len);
      return 0;
    }
  };

  const int kPid = 0x100;
  const int kStreamId = 0xBD;

  // 188 bytes: sync, PID, 'control' (byte 3), then 'body' from offset 'start'; the rest is 0xFF
  std::vector<byte> Packet(byte control, bool unitStart, size_t start, const std::vector<byte>& body, byte adaptionLength = 0)
  {
    std::vector<byte> p(188, 0xFF);
    p[0] = 0x47;
    p[1] = static_cast<byte>((unitStart ? 0x40 : 0) | (kPid >> 8));
    p[2] = kPid & 0xFF;
    p[3] = control;
    if (control & 0x20)
      p[4] = adaptionLength;
    for (size_t i = 0; i < body.size() && start + i < 188; ++i)
      p[start + i] = body[i];
    return p;
  }

  // 00 00 01 BD, PES_packet_length, flags, flags, header data length
  std::vector<byte> PesStart(unsigned pesLength, byte headerData)
  {
    return {0, 0, 1, kStreamId, static_cast<byte>(pesLength >> 8), static_cast<byte>(pesLength), 0x80, 0x00, headerData};
  }

  bool Feed(CPesDecoder& d, const std::vector<byte>& packet)
  {
    GuardedBuffer g(packet);     // a read or write past the end is an access violation
    return d.OnTsPacket(g.data());
  }
}

TEST(PesDecoder, ANormalPesPacketIsDelivered)
{
  Collector c;
  CPesDecoder d(&c);
  d.SetPid(kPid);
  d.SetStreamId(kStreamId);
  auto body = PesStart(3 + 4, 0);        // 2 flag bytes, the header length byte and 4 data bytes: the length counts what follows the length field
  body.insert(body.end(), {1, 2, 3, 4});
  Feed(d, Packet(0x10, true, 4, body));
  ASSERT_EQ(c.calls, 1);
  EXPECT_EQ(c.lastHeaderLen, 9);
  EXPECT_EQ(c.lastData, (std::vector<byte>{1, 2, 3, 4}));
}

TEST(PesDecoder, AHeaderLengthPastThePacketIsRefused)
{
  // header data length 255: 264 bytes of header from a 188 byte packet (m_pesHeader holds 256)
  Collector c;
  CPesDecoder d(&c);
  d.SetPid(kPid);
  d.SetStreamId(kStreamId);
  EXPECT_FALSE(Feed(d, Packet(0x10, true, 4, PesStart(100, 255))));
  EXPECT_EQ(c.calls, 0);
  // the decoder still works afterwards
  auto body = PesStart(3 + 2, 0);
  body.insert(body.end(), {7, 8});
  Feed(d, Packet(0x10, true, 4, body));
  ASSERT_EQ(c.calls, 1);
  EXPECT_EQ(c.lastData, (std::vector<byte>{7, 8}));
}

TEST(PesDecoder, AHeaderThatEndsExactlyAtTheEndOfThePacketIsKept)
{
  Collector c;
  CPesDecoder d(&c);
  d.SetPid(kPid);
  d.SetStreamId(kStreamId);
  const byte headerData = 188 - 4 - 9;     // the header takes the whole payload: no data follows
  Feed(d, Packet(0x10, true, 4, PesStart(3 + headerData, headerData)));
  EXPECT_EQ(c.calls, 0);                   // nothing refused by the bounds check and nothing to deliver
}

TEST(PesDecoder, AStartTooNearTheEndOfThePacketIsRefused)
{
  // adaption field of 179 bytes: the payload (5 bytes) starts at 184 and has no room for the 9 byte PES header
  Collector c;
  CPesDecoder d(&c);
  d.SetPid(kPid);
  d.SetStreamId(kStreamId);
  EXPECT_FALSE(Feed(d, Packet(0x30, true, 184, {0, 0, 1, kStreamId}, 179)));
  EXPECT_EQ(c.calls, 0);
}

TEST(PesDecoder, AnAdaptionFieldThatFillsThePacketHasNoPayload)
{
  for (int length : {183, 200, 251, 255})
  {
    Collector c;
    CPesDecoder d(&c);
    d.SetPid(kPid);
    d.SetStreamId(kStreamId);
    EXPECT_FALSE(Feed(d, Packet(0x30, true, 4, PesStart(10, 0), static_cast<byte>(length)))) << length;
    EXPECT_EQ(c.calls, 0) << length;
  }
}

TEST(PesDecoder, AReservedAdaptionControlIsIgnored)
{
  Collector c;
  CPesDecoder d(&c);
  d.SetPid(kPid);
  d.SetStreamId(kStreamId);
  EXPECT_FALSE(Feed(d, Packet(0x00, true, 4, PesStart(10, 0))));
  EXPECT_EQ(c.calls, 0);
}

TEST(TsHeaderOfDvbSubtitle3, NoFieldStaysUninitialised)
{
  std::vector<byte> p(188, 0xFF);
  p[0] = 0x46;                              // wrong sync byte
  CTsHeader bad(p.data());
  EXPECT_TRUE(bad.TransportError);
  EXPECT_EQ(bad.Pid, 0);
  EXPECT_EQ(bad.PayLoadStart, 0);
  EXPECT_FALSE(bad.HasPayload);
  CTsHeader none;
  EXPECT_EQ(none.Pid, 0);
  EXPECT_EQ(none.SyncByte, 0);
  EXPECT_FALSE(none.HasPayload);
}

TEST(TsHeaderOfDvbSubtitle3, AnAdaptionFieldThatFillsThePacketHasNoPayloadAndNoWrappedStart)
{
  for (int length : {183, 251, 255})        // 5 + 251 = 256 would wrap to 0 in the BYTE
  {
    auto p = Packet(0x30, true, 4, {}, static_cast<byte>(length));
    CTsHeader h(p.data());
    EXPECT_FALSE(h.HasPayload) << length;
    EXPECT_FALSE(h.PayloadUnitStart) << length;
    EXPECT_EQ(h.PayLoadStart, 4) << length;
  }
}

TEST(TsHeaderOfDvbSubtitle3, ANormalPacketIsDecoded)
{
  auto p = Packet(0x30, true, 4, {}, 7);
  CTsHeader h(p.data());
  EXPECT_EQ(h.Pid, kPid);
  EXPECT_TRUE(h.HasPayload);
  EXPECT_TRUE(h.PayloadUnitStart);
  EXPECT_EQ(h.PayLoadStart, 12);
}
