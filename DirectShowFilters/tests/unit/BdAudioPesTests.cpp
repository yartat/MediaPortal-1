// BDReader: the start of an audio PES packet and the duration of an audio block (SECURITY_AUDIT.md: BD-01, BD-02).
// The data come from a .m2ts file of a disc, so the lengths in the PES header are untrusted.
// The packet is placed in a GuardedBuffer: reading the byte after its last byte is an access violation.
#include <gtest/gtest.h>
#include <windows.h>
#include <cstdint>
#include <vector>
#include "AudioPes.h"
#include "GuardedBuffer.h"

namespace
{
  // A TS packet of 188 bytes with a PES start at 'payloadStart' (the bytes before it are the 4 byte TS header and an adaptation field).
  // flags2 0x80: a PTS follows the 3 flag bytes; 'headerDataLength' is the PES_header_data_length byte.
  std::vector<uint8_t> AudioPacket(size_t payloadStart, unsigned pesPacketLength, unsigned headerDataLength, uint8_t flags2 = 0x80)
  {
    std::vector<uint8_t> t(188, 0xEE);
    t[0] = 0x47;
    t[1] = 0x40;
    t[2] = 0x11;
    t[3] = payloadStart > 4 ? 0x30 : 0x10;
    if (payloadStart > 4 && payloadStart < 188)
      t[4] = static_cast<uint8_t>(payloadStart - 5);
    uint8_t* p = t.data() + payloadStart;
    const size_t room = 188 - payloadStart;
    const uint8_t pes[14] = { 0x00, 0x00, 0x01, 0xBD, static_cast<uint8_t>(pesPacketLength >> 8), static_cast<uint8_t>(pesPacketLength),
                              0x80, flags2, static_cast<uint8_t>(headerDataLength), 0x21, 0x00, 0x01, 0x00, 0x01 };
    for (size_t i = 0; i < sizeof(pes) && i < room; i++)
      p[i] = pes[i];
    return t;
  }

  AudioPesStart Parse(const std::vector<uint8_t>& packet, size_t payloadStart)
  {
    GuardedBuffer g(packet);
    return ParseAudioPesStart(g.data() + payloadStart, static_cast<UINT32>(188 - payloadStart));
  }
}

// The reference: an ordinary packet with a PES header of 14 bytes (9 and a 5 byte PTS), the payload goes on in later packets.
TEST(BdAudioPes, OrdinaryPacketGivesLengthsOfTheHeaderAndThePayload)
{
  AudioPesStart r = Parse(AudioPacket(4, 1000, 5), 4);
  EXPECT_TRUE(r.valid);
  EXPECT_EQ(r.pesHeaderLen, 14u);
  EXPECT_EQ(r.pesLength, 992u);                      // 1000 - (14 - 6)
  EXPECT_EQ(r.firstPacketLength, 170u);              // 188 - 4 - 14
  EXPECT_FALSE(r.ac3Substream);
}

TEST(BdAudioPes, SmallPesFitsInThePacket)
{
  AudioPesStart r = Parse(AudioPacket(4, 100, 5), 4);
  EXPECT_TRUE(r.valid);
  EXPECT_EQ(r.pesLength, 92u);
  EXPECT_EQ(r.firstPacketLength, 170u);
}

// The extension of the PES header that marks a substream to discard (header: PTS, extension flag byte, two bytes).
TEST(BdAudioPes, SubstreamMarkIsRecognised)
{
  std::vector<uint8_t> t = AudioPacket(4, 1000, 8, 0x81);
  uint8_t* p = t.data() + 4;
  p[14] = 0x01;                                      // PES extension flags: extension 2 present
  p[15] = 0x81;
  p[16] = 0x76;
  AudioPesStart r = Parse(t, 4);
  EXPECT_TRUE(r.valid);
  EXPECT_TRUE(r.ac3Substream);
}

// BD-01: the adaptation field takes space of the packet, so there are fewer bytes after the PES header (the old code takes 4).
TEST(BdAudioPes, PacketWithAdaptationFieldCountsFromThePayloadStart)
{
  AudioPesStart r = Parse(AudioPacket(100, 1000, 5), 100);
  EXPECT_TRUE(r.valid);
  EXPECT_EQ(r.firstPacketLength, 74u);               // 188 - 100 - 14
}

// BD-01: PES_header_data_length 255 makes a header of 264 bytes, in a packet that has 184.
TEST(BdAudioPes, HeaderLongerThanThePacketIsRejected)
{
  AudioPesStart r = Parse(AudioPacket(4, 1000, 255), 4);
  EXPECT_FALSE(r.valid);
}

// BD-01: the payload starts in the last 4 bytes of the packet: the fixed part of the PES header (9 bytes) is not even in the packet.
TEST(BdAudioPes, PesStartInTheLastBytesOfThePacketIsRejected)
{
  AudioPesStart r = Parse(AudioPacket(184, 1000, 5), 184);
  EXPECT_FALSE(r.valid);
}

// BD-01: PES_packet_length is smaller than the rest of the header: the unsigned result is about 4 billion.
TEST(BdAudioPes, PesLengthSmallerThanItsHeaderIsRejected)
{
  AudioPesStart r = Parse(AudioPacket(4, 3, 5), 4);
  EXPECT_FALSE(r.valid);
}

// ---------------------------------------------------------------------------
// BD-02
// ---------------------------------------------------------------------------

namespace
{
  WAVEFORMATEX Format(WORD blockAlign, DWORD avgBytesPerSec)
  {
    WAVEFORMATEX w = {};
    w.nBlockAlign = blockAlign;
    w.nAvgBytesPerSec = avgBytesPerSec;
    return w;
  }
}

TEST(BdAudioDuration, SmallBlockGivesItsDuration)
{
  WAVEFORMATEX w = Format(192, 48000);               // 4 ms
  EXPECT_EQ(AudioFrameDuration(&w), 40000);
}

// BD-02: nBlockAlign * 10000000 does not fit in an int from nBlockAlign 215 on.
TEST(BdAudioDuration, LargeBlockGivesItsDuration)
{
  WAVEFORMATEX w = Format(2048, 80000);              // 25.6 ms
  EXPECT_EQ(AudioFrameDuration(&w), 256000);
  WAVEFORMATEX ac3 = Format(1536, 48000);            // 32 ms
  EXPECT_EQ(AudioFrameDuration(&ac3), 320000);
}

// BD-02: a format with no data rate (the parser of a stream found 0): a division by zero.
TEST(BdAudioDuration, FormatWithoutDataRateDoesNotDivideByZero)
{
  WAVEFORMATEX w = Format(1536, 0);
  LONGLONG d = 0;
  EXPECT_NO_THROW(d = AudioFrameDuration(&w));
  EXPECT_EQ(d, 1);                                   // as FillAudio does without a format: the packet lasts 1 unit
}

// ---------------------------------------------------------------------------
// BD-03
// ---------------------------------------------------------------------------

// The way FillAudio uses them: every following TS packet of the PES adds 184 bytes while the PES needs more data. The size of
// the PES is not always a multiple of 184 + the first part, so the size can jump over the expected one and never be equal to it.
namespace
{
  // returns the size after 'packets' packets (or earlier, when the PES is complete)
  size_t Feed(size_t first, UINT32 expected, int packets, bool* complete = nullptr)
  {
    size_t size = first;
    for (int i = 0; i < packets && AudioPesNeedsMoreData(size, expected); i++)
      size += 184;
    if (complete)
      *complete = AudioPesComplete(size, expected);
    return size;
  }
}

// The reference: a PES whose length fits the packets exactly (170 + 4 * 184 = 906) is complete after 4 packets.
TEST(BdAudioPesContinuation, PesThatFitsThePacketsIsComplete)
{
  bool complete = false;
  EXPECT_EQ(Feed(170, 906, 100, &complete), 906u);
  EXPECT_TRUE(complete);
}

// BD-03: a PES of 1 000 bytes: 170, 354, 538, 722, 906, 1 090 ... the size is never 1 000 and the buffer grows with every packet
// of the stream (100 000 packets are 18 MB) until the next PES start replaces it.
TEST(BdAudioPesContinuation, PesThatDoesNotFitThePacketsStopsGrowing)
{
  bool complete = false;
  const size_t size = Feed(170, 1000, 100000, &complete);
  EXPECT_LE(size, 1000u + 184u) << "the buffer grew to " << size << " bytes";
  EXPECT_TRUE(complete);
}

// BD-03: the buffer of video PES data has a limit; a normal access unit (a few hundred KB) is below it.
TEST(BdVideoPesBuffer, LimitIsFourMegabytes)
{
  EXPECT_FALSE(VideoPesBufferTooLarge(0));
  EXPECT_FALSE(VideoPesBufferTooLarge(512 * 1024));
  EXPECT_FALSE(VideoPesBufferTooLarge(4 * 1024 * 1024));
  EXPECT_TRUE(VideoPesBufferTooLarge(4 * 1024 * 1024 + 1));
  EXPECT_TRUE(VideoPesBufferTooLarge(static_cast<size_t>(1) << 32));
}
