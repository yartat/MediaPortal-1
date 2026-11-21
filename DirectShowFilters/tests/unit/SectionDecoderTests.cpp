// CSectionDecoder (DvbCoreUtils): assembles PSI/SI sections from the payload of transport packets of one PID.
#include <gtest/gtest.h>
#include <windows.h>
#include "SectionDecoder.h"
#include "GuardedBuffer.h"
#include "SectionBuilder.h"

namespace
{
  struct Received
  {
    int pid;
    int tableId;
    CSection section;
  };

  class Collector : public ISectionCallback
  {
  public:
    void OnNewSection(int pid, int tableId, CSection& section) override
    {
      Received r;
      r.pid = pid;
      r.tableId = tableId;
      r.section.Copy(section);
      r.section.BufferPos = section.BufferPos;
      sections.push_back(r);
    }
    std::vector<Received> sections;
  };

  // Overrides the virtual hook instead of using the callback
  class Hooked : public CSectionDecoder
  {
  public:
    void OnNewSection(CSection& section) override { tableIds.push_back(section.table_id); }
    std::vector<int> tableIds;
  };

  const int kPid = 0x100;

  Bytes Body(size_t n, uint8_t seed = 0)
  {
    Bytes b(n);
    for (size_t i = 0; i < n; i++)
      b[i] = static_cast<uint8_t>(seed + i * 7);
    return b;
  }

  void Feed(CSectionDecoder& d, const std::vector<TsPacket>& packets)
  {
    for (const TsPacket& p : packets)
      d.OnTsPacket(p.data);
  }

  class SectionDecoder : public testing::Test
  {
  protected:
    void SetUp() override
    {
      decoder.SetPid(kPid);
      decoder.SetCallBack(&collector);
    }
    CSectionDecoder decoder;
    Collector collector;
  };
}

TEST_F(SectionDecoder, PidIsStored)
{
  EXPECT_EQ(decoder.GetPid(), kPid);
  decoder.SetPid(0x1234);
  EXPECT_EQ(decoder.GetPid(), 0x1234);
}

TEST_F(SectionDecoder, NothingHappensWithoutAPid)
{
  CSectionDecoder fresh;
  Collector c;
  fresh.SetCallBack(&c);
  EXPECT_EQ(fresh.GetPid(), -1);
  Feed(fresh, PacketizeSections(kPid, MakeLongSection(0x42, 1, 0, Body(10))));
  EXPECT_TRUE(c.sections.empty());
}

TEST_F(SectionDecoder, NullPacketIsIgnored)
{
  decoder.OnTsPacket(static_cast<const byte*>(nullptr));
  EXPECT_TRUE(collector.sections.empty());
}

TEST_F(SectionDecoder, SectionInOnePacketIsDelivered)
{
  const Bytes body = Body(20);
  Feed(decoder, PacketizeSections(kPid, MakeLongSection(0x42, 0x1234, 7, body, 2, 5)));
  ASSERT_EQ(collector.sections.size(), 1u);
  const Received& r = collector.sections[0];
  EXPECT_EQ(r.pid, kPid);
  EXPECT_EQ(r.tableId, 0x42);
  EXPECT_EQ(r.section.table_id, 0x42);
  EXPECT_EQ(r.section.table_id_extension, 0x1234);
  EXPECT_EQ(r.section.version_number, 7);
  EXPECT_EQ(r.section.section_number, 2);
  EXPECT_EQ(r.section.section_syntax_indicator, 1);
  EXPECT_EQ(r.section.section_length, 5 + 20 + 4);
  EXPECT_EQ(0, memcmp(&r.section.Data[8], body.data(), body.size()));
}

TEST_F(SectionDecoder, VirtualHookGetsTheSectionToo)
{
  Hooked h;
  h.SetPid(kPid);
  Feed(h, PacketizeSections(kPid, MakeLongSection(0x4E, 1, 0, Body(5))));
  ASSERT_EQ(h.tableIds.size(), 1u);
  EXPECT_EQ(h.tableIds[0], 0x4E);
}

TEST_F(SectionDecoder, SectionOverSeveralPacketsIsAssembled)
{
  const Bytes body = Body(1000, 3);
  const auto packets = PacketizeSections(kPid, MakeLongSection(0x50, 9, 1, body));
  ASSERT_GT(packets.size(), 5u);
  Feed(decoder, packets);
  ASSERT_EQ(collector.sections.size(), 1u);
  EXPECT_EQ(collector.sections[0].section.section_length, 5 + 1000 + 4);
  EXPECT_EQ(0, memcmp(&collector.sections[0].section.Data[8], body.data(), body.size()));
}

TEST_F(SectionDecoder, LongestSectionIsAssembled)
{
  // section_length is 12 bits; 4093 is the longest a long syntax section may have
  const Bytes body = Body(4093 - 5 - 4);
  Feed(decoder, PacketizeSections(kPid, MakeLongSection(0x50, 1, 0, body)));
  ASSERT_EQ(collector.sections.size(), 1u);
  EXPECT_EQ(collector.sections[0].section.section_length, 4093);
  EXPECT_EQ(0, memcmp(&collector.sections[0].section.Data[8], body.data(), body.size()));
}

TEST_F(SectionDecoder, TwoSectionsAfterEachOtherAreBothDelivered)
{
  Feed(decoder, PacketizeSections(kPid, MakeLongSection(0x42, 1, 0, Body(30))));
  Feed(decoder, PacketizeSections(kPid, MakeLongSection(0x46, 2, 0, Body(300))));
  ASSERT_EQ(collector.sections.size(), 2u);
  EXPECT_EQ(collector.sections[0].tableId, 0x42);
  EXPECT_EQ(collector.sections[1].tableId, 0x46);
}

TEST_F(SectionDecoder, TwoSectionsInOnePacketAreBothDelivered)
{
  const Bytes both = Concat(MakeLongSection(0x42, 1, 0, Body(20)), MakeLongSection(0x46, 2, 0, Body(25)));
  Feed(decoder, PacketizeSections(kPid, both));
  ASSERT_EQ(collector.sections.size(), 2u);
  EXPECT_EQ(collector.sections[0].tableId, 0x42);
  EXPECT_EQ(collector.sections[1].tableId, 0x46);
  EXPECT_EQ(collector.sections[1].section.table_id_extension, 2);
}

TEST_F(SectionDecoder, SectionWithAWrongCrcIsDropped)
{
  Feed(decoder, PacketizeSections(kPid, MakeLongSection(0x42, 1, 0, Body(20), 0, 0, true)));
  EXPECT_TRUE(collector.sections.empty());
  // and the decoder is ready for the next one
  Feed(decoder, PacketizeSections(kPid, MakeLongSection(0x42, 1, 0, Body(20))));
  EXPECT_EQ(collector.sections.size(), 1u);
}

TEST_F(SectionDecoder, WrongCrcPassesWhenTheCheckIsOff)
{
  decoder.EnableCrcCheck(false);
  Feed(decoder, PacketizeSections(kPid, MakeLongSection(0x42, 1, 0, Body(20), 0, 0, true)));
  EXPECT_EQ(collector.sections.size(), 1u);
}

TEST_F(SectionDecoder, ShortSyntaxSectionHasNoCrcCheck)
{
  Feed(decoder, PacketizeSections(kPid, MakeShortSection(0x70, Body(5))));        // TDT-like: 3 + 5 bytes
  ASSERT_EQ(collector.sections.size(), 1u);
  EXPECT_EQ(collector.sections[0].section.section_syntax_indicator, 0);
  EXPECT_EQ(collector.sections[0].tableId, 0x70);
}

TEST_F(SectionDecoder, PacketsOfOtherPidsAreIgnored)
{
  Feed(decoder, PacketizeSections(kPid + 1, MakeLongSection(0x42, 1, 0, Body(20))));
  EXPECT_TRUE(collector.sections.empty());
}

TEST_F(SectionDecoder, PidOf1FFFIsNeverDecoded)
{
  decoder.SetPid(0x1FFF);
  Feed(decoder, PacketizeSections(0x1FFF, MakeLongSection(0x42, 1, 0, Body(20))));
  EXPECT_TRUE(collector.sections.empty());
}

TEST_F(SectionDecoder, PacketsWithoutPayloadAreIgnored)
{
  TsPacket p = MakePacket(kPid, 0x20, true);          // adaption field only
  p.data[4] = 183;
  decoder.OnTsPacket(p.data);
  EXPECT_TRUE(collector.sections.empty());
}

TEST_F(SectionDecoder, PayloadWithoutAStartIsIgnoredWhileIdle)
{
  const auto packets = PacketizeSections(kPid, MakeLongSection(0x42, 1, 0, Body(500)));
  ASSERT_GT(packets.size(), 2u);
  for (size_t i = 1; i < packets.size(); i++)           // the first packet (the one with the start flag) is lost
    decoder.OnTsPacket(packets[i].data);
  EXPECT_TRUE(collector.sections.empty());
}

TEST_F(SectionDecoder, StuffingAfterTheSectionIsNotASection)
{
  const Bytes s = MakeLongSection(0x42, 1, 0, Body(10));
  TsPacket p = PacketizeSections(kPid, s)[0];
  memset(&p.data[5 + s.size()], 0xFF, 188 - 5 - s.size());
  decoder.OnTsPacket(p.data);
  EXPECT_EQ(collector.sections.size(), 1u);
}

TEST_F(SectionDecoder, TransportErrorDropsTheSectionInProgress)
{
  auto packets = PacketizeSections(kPid, MakeLongSection(0x42, 1, 0, Body(800)));
  ASSERT_GT(packets.size(), 3u);
  decoder.OnTsPacket(packets[0].data);
  packets[1].data[1] |= 0x80;                           // transport_error_indicator
  decoder.OnTsPacket(packets[1].data);
  for (size_t i = 2; i < packets.size(); i++)
    decoder.OnTsPacket(packets[i].data);
  EXPECT_TRUE(collector.sections.empty());
}

TEST_F(SectionDecoder, ResetDropsTheSectionInProgress)
{
  const auto packets = PacketizeSections(kPid, MakeLongSection(0x42, 1, 0, Body(500)));
  decoder.OnTsPacket(packets[0].data);
  decoder.Reset();
  for (size_t i = 1; i < packets.size(); i++)
    decoder.OnTsPacket(packets[i].data);
  EXPECT_TRUE(collector.sections.empty());
}

TEST_F(SectionDecoder, ANewStartWhileASectionIsOpenDoesNotLoseTheNewSection)
{
  // the tail of the first section never arrives; the next section, started with the payload unit start flag, must still come
  const auto first = PacketizeSections(kPid, MakeLongSection(0x42, 1, 0, Body(500)));
  decoder.OnTsPacket(first[0].data);
  Feed(decoder, PacketizeSections(kPid, MakeLongSection(0x46, 2, 0, Body(40))));
  Feed(decoder, PacketizeSections(kPid, MakeLongSection(0x47, 3, 0, Body(40))));
  ASSERT_FALSE(collector.sections.empty());
  EXPECT_EQ(collector.sections.back().tableId, 0x47);
}

TEST_F(SectionDecoder, SectionHeaderSplitOverTwoPacketsIsDecoded)
{
  // the last payload bytes of a packet are the table_id only: section_length and the rest come in the next packet
  const Bytes s = MakeLongSection(0x42, 0x0A0B, 3, Body(40));
  TsPacket a = MakePacket(kPid, 0x10, true);
  a.data[4] = 182;                                      // pointer_field: the section starts at offset 187
  memset(&a.data[5], 0, 182);
  a.data[187] = s[0];                                   // only table_id fits
  decoder.OnTsPacket(a.data);
  TsPacket b = MakePacket(kPid, 0x11, false);
  memset(&b.data[4], 0xFF, 184);
  memcpy(&b.data[4], &s[1], s.size() - 1);
  decoder.OnTsPacket(b.data);
  ASSERT_EQ(collector.sections.size(), 1u);
  EXPECT_EQ(collector.sections[0].section.table_id_extension, 0x0A0B);
}

TEST_F(SectionDecoder, ZeroLengthSectionIsDiscarded)
{
  Bytes s = MakeLongSection(0x42, 1, 0, Body(20));
  s[1] = 0xB0;
  s[2] = 0;                                             // section_length 0
  Feed(decoder, PacketizeSections(kPid, s));
  EXPECT_TRUE(collector.sections.empty());
  Feed(decoder, PacketizeSections(kPid, MakeLongSection(0x42, 1, 0, Body(20))));
  EXPECT_EQ(collector.sections.size(), 1u);
}

// --- damaged input ----------------------------------------------------------------------------------------------------------

TEST_F(SectionDecoder, RandomPayloadOfTheRightPidNeverCrashes)
{
  // Packets of the decoder's PID with random bytes, half of them with the payload unit start flag: no read past the packet
  // (the packet sits right in front of a guard page) and no section that is not valid.
  decoder.EnableCrcCheck(true);
  uint32_t seed = 12345;
  auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
  GuardedBuffer packet(188);
  for (int i = 0; i < 20000; i++)
  {
    for (int k = 0; k < 188; k++)
      packet.data()[k] = static_cast<uint8_t>(next());
    packet.data()[0] = 0x47;
    packet.data()[1] = static_cast<uint8_t>((packet.data()[1] & 0x40) | (kPid >> 8));
    packet.data()[2] = static_cast<uint8_t>(kPid & 0xFF);
    packet.data()[3] = static_cast<uint8_t>(0x10 | (packet.data()[3] & 0x0F));       // payload only
    decoder.OnTsPacket(packet.data());
  }
  for (const Received& r : collector.sections)
    if (r.section.section_syntax_indicator == 1)           // short syntax sections have no CRC
      EXPECT_EQ(crc32(reinterpret_cast<char*>(const_cast<byte*>(r.section.Data)), r.section.section_length + 3), 0u);
}

TEST_F(SectionDecoder, DamagedSectionLengthsNeverCrash)
{
  // every section_length from 0 to 4095 for a section of 40 bytes: too long ones wait for more data, short ones end early
  for (int length = 0; length < 4096; length++)
  {
    Bytes s = MakeLongSection(0x42, 1, 0, Body(31));
    s[1] = static_cast<uint8_t>(0xB0 | (length >> 8));
    s[2] = static_cast<uint8_t>(length);
    GuardedBuffer packet(188, 0xFF);
    const auto packets = PacketizeSections(kPid, s);
    memcpy(packet.data(), packets[0].data, 188);
    decoder.EnableCrcCheck(false);
    decoder.OnTsPacket(packet.data());
    decoder.Reset();
  }
}

TEST_F(SectionDecoder, PayloadStartInTheLastBytesOfAPacketDoesNotReadPastIt)
{
  // adaption field of 182 bytes: a payload of 1 byte at offset 187, and a section header that is waiting for its length byte
  Bytes s = MakeLongSection(0x42, 1, 0, Body(40));
  TsPacket a = MakePacket(kPid, 0x10, true);
  a.data[4] = 182;
  memset(&a.data[5], 0, 182);
  a.data[187] = s[0];
  decoder.OnTsPacket(a.data);

  GuardedBuffer b(188, 0xFF);
  b.data()[0] = 0x47;
  b.data()[1] = static_cast<uint8_t>(kPid >> 8);
  b.data()[2] = static_cast<uint8_t>(kPid & 0xFF);
  b.data()[3] = 0x31;                                   // adaption field and payload
  b.data()[4] = 182;                                    // adaption field length: the payload is the last byte
  b.data()[5] = 0;
  decoder.OnTsPacket(b.data());
}

// --- SECURITY_AUDIT.md: TS-02 (the state machine of the assembly, without fuzzing until now) ----------------------------------

namespace
{
  struct Mux
  {
    std::vector<Bytes> sections;          // what went in
    std::vector<TsPacket> packets;
  };

  // Sections back to back in the payload: a packet that holds the start of a section has the payload unit start flag and a
  // pointer_field with the offset of the first start; the tail of the section before it comes first. The rest is stuffing.
  Mux MuxSections(const std::vector<Bytes>& sections)
  {
    Mux m;
    m.sections = sections;
    Bytes stream;
    std::vector<size_t> starts;
    for (const Bytes& s : sections)
    {
      starts.push_back(stream.size());
      stream.insert(stream.end(), s.begin(), s.end());
    }
    size_t pos = 0, next = 0;
    unsigned counter = 0;
    while (pos < stream.size())
    {
      while (next < starts.size() && starts[next] < pos)
        next++;
      const bool start = next < starts.size() && starts[next] < pos + 183;
      TsPacket p = MakePacket(kPid, 0x10 | (counter++ & 0x0F), start);
      size_t at = 4;
      if (start)
        p.data[at++] = static_cast<uint8_t>(starts[next] - pos);
      size_t n = (std::min<size_t>)(188 - at, stream.size() - pos);
      if (!start && next < starts.size() && starts[next] - pos == 183)
        n = 183;                                        // the next section cannot start in a packet without the flag: stuffing
      memcpy(&p.data[at], &stream[pos], n);
      memset(&p.data[at + n], 0xFF, 188 - at - n);
      pos += n;
      m.packets.push_back(p);
    }
    return m;
  }

  std::vector<Bytes> RandomSections(uint32_t& seed, int count, bool longOnly)
  {
    auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    std::vector<Bytes> sections;
    for (int i = 0; i < count; i++)
    {
      const size_t size = (next() % 4) == 0 ? 1 + next() % 3000 : 1 + next() % 300;     // many short ones, some long
      Bytes body(size);
      for (uint8_t& b : body)
        b = static_cast<uint8_t>(next());
      if (!longOnly && next() % 5 == 0)
      {
        body.resize((std::max<size_t>)(size, 5));
        sections.push_back(MakeShortSection(0x70, body));
      }
      else
        sections.push_back(MakeLongSection(0x40 + next() % 16, next() & 0xFFFF, next() & 0x1F, body));
    }
    return sections;
  }

  Bytes Content(const CSection& s)
  {
    return Bytes(s.Data, s.Data + s.section_length + 3);
  }
}

TEST_F(SectionDecoder, ManySectionsOfAnySizeBackToBackAreAllDeliveredIntact)
{
  uint32_t seed = 777;
  for (int round = 0; round < 40; round++)
  {
    collector.sections.clear();
    decoder.Reset();
    const Mux mux = MuxSections(RandomSections(seed, 30, false));
    Feed(decoder, mux.packets);
    ASSERT_EQ(collector.sections.size(), mux.sections.size()) << "round " << round;
    for (size_t i = 0; i < mux.sections.size(); i++)
      EXPECT_EQ(Content(collector.sections[i].section), mux.sections[i]) << "round " << round << " section " << i;
  }
}

TEST_F(SectionDecoder, LostAndDamagedPacketsNeverDeliverAnythingThatWasNotSent)
{
  // Long-syntax sections have a CRC: whatever is delivered after packets were dropped, repeated or damaged must be a section
  // that went in, byte for byte (no tail of one section glued to the head of another, no stale bytes).
  uint32_t seed = 4242;
  auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
  for (int round = 0; round < 60; round++)
  {
    collector.sections.clear();
    decoder.Reset();
    const Mux mux = MuxSections(RandomSections(seed, 25, true));
    for (const TsPacket& original : mux.packets)
    {
      const unsigned what = next() % 20;
      if (what == 0)
        continue;                                       // lost
      TsPacket p = original;
      if (what == 1)
        p.data[4 + next() % 184] ^= static_cast<uint8_t>(1 + next() % 255);   // damaged
      decoder.OnTsPacket(p.data);
      if (what == 2)
        decoder.OnTsPacket(p.data);                     // repeated
    }
    for (const Received& r : collector.sections)
    {
      bool known = false;
      for (const Bytes& s : mux.sections)
        if (s == Content(r.section))
        {
          known = true;
          break;
        }
      EXPECT_TRUE(known) << "round " << round << ": a section of " << r.section.section_length + 3 << " bytes that was never sent";
    }
  }
}

TEST_F(SectionDecoder, ATinySectionNeverShowsTheBytesOfThePreviousSection)
{
  // A section of fewer than 8 bytes has no complete header. Wherever it starts in a packet (also split over two packets), what
  // is delivered of it must not contain what the previous, long section left in the buffer behind its last byte.
  decoder.EnableCrcCheck(false);
  for (int total = 4; total <= 7; total++)
  {
    for (int offset = 0; offset < 183; offset++)
    {
      collector.sections.clear();
      decoder.Reset();
      Feed(decoder, PacketizeSections(kPid, MakeLongSection(0x42, 1, 0, Body(150, 0xAA))));   // leaves bytes in the buffer
      collector.sections.clear();

      Bytes tiny = MakeShortSection(0x70, Bytes(total - 3, 0x11));
      TsPacket a = MakePacket(kPid, 0x10, true);
      a.data[4] = static_cast<uint8_t>(offset);                                     // pointer_field
      memset(&a.data[5], 0xFF, 183);
      const size_t room = 183 - offset;
      memcpy(&a.data[5 + offset], tiny.data(), (std::min<size_t>)(room, tiny.size()));
      decoder.OnTsPacket(a.data);
      if (room < tiny.size())
      {
        TsPacket b = MakePacket(kPid, 0x11, false);
        memset(&b.data[4], 0xFF, 184);
        memcpy(&b.data[4], &tiny[room], tiny.size() - room);
        decoder.OnTsPacket(b.data);
      }
      for (const Received& r : collector.sections)
        for (int i = total; i < 8; i++)
          EXPECT_EQ(r.section.Data[i], 0) << "tiny section of " << total << " bytes at offset " << offset << ": byte " << i
                                          << " is left over from the previous section";
    }
  }
}
