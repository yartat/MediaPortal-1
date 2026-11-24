// TsWriter: the EPG decoder, the events of an EIT section (SECURITY_AUDIT.md: EP-01).
#include <gtest/gtest.h>
#include <windows.h>
#include <bdatypes.h>
#include <map>
#include <string>
#include <vector>
using namespace std;
#include "epgDecoder.h"
#include "GuardedBuffer.h"

namespace
{
  // An EIT section (table 0x50) with one event and no descriptors. 'sectionNumber' (byte 6) and 'transportLow' (byte 9) are bytes
  // of the section header, not of the event.
  vector<byte> EitSection(byte sectionNumber, byte transportLow)
  {
    vector<byte> s(26, 0);
    s[0] = 0x50;
    s[1] = 0xB0;
    s[2] = 23;                                       // bytes after the length field, with the CRC (not checked by the decoder)
    s[3] = 0x04; s[4] = 0x01;                        // service id 0x0401
    s[5] = 0xC1;                                     // version 0, current
    s[6] = sectionNumber;
    s[7] = sectionNumber;
    s[8] = 0x00; s[9] = transportLow;                // transport stream id
    s[10] = 0x20; s[11] = 0x3A;                      // original network id
    s[12] = sectionNumber;
    s[13] = 0x50;
    // the event: id 0x1234, date MJD 0xE789, start 12:34:56, duration 01:23:45 (BCD), running, no CA, no descriptors
    s[14] = 0x12; s[15] = 0x34;
    s[16] = 0xE7; s[17] = 0x89;
    s[18] = 0x12; s[19] = 0x34; s[20] = 0x56;
    s[21] = 0x01; s[22] = 0x23; s[23] = 0x45;
    s[24] = 0x80;                                    // running status 4, free_CA_mode 0, descriptors_loop_length high bits
    s[25] = 0x00;
    return s;
  }

  struct EventData
  {
    ULONG dateMJD = 0, timeUTC = 0, duration = 0;
    unsigned int eventId = 0;
  };

  // decodes the section and reads the one event back
  bool Decode(vector<byte> section, EventData& out)
  {
    CEpgDecoder decoder;
    decoder.GrabEPG();
    decoder.DecodeEPG(section.data(), static_cast<int>(section.size()), 0x12);
    if (decoder.GetEPGChannelCount() != 1 || decoder.GetEPGEventCount(0) != 1)
      return false;
    ULONG languages = 0;
    char* genre = nullptr;
    char* classification = nullptr;
    int stars = 0;
    decoder.GetEPGEvent(0, 0, &languages, &out.dateMJD, &out.timeUTC, &out.duration, &genre, &stars, &classification, &out.eventId);
    return true;
  }
}

// The reference: the bytes of the section header that the old code takes for the low bytes happen to be those of the event.
TEST(EpgDecoder, EventTimesAreReadWhenTheHeaderBytesHappenToMatch)
{
  EventData e;
  ASSERT_TRUE(Decode(EitSection(0x56, 0x45), e));
  EXPECT_EQ(e.eventId, 0x1234u);
  EXPECT_EQ(e.dateMJD, 0xE789u);
  EXPECT_EQ(e.timeUTC, 0x123456u);
  EXPECT_EQ(e.duration, 0x012345u);
}

// EP-01: the low byte of the start time comes from byte 6 of the section (the section number) and the low byte of the duration
// from byte 9 (the transport stream id), not from the event.
TEST(EpgDecoder, EventTimesDoNotDependOnTheSectionHeader)
{
  EventData e;
  ASSERT_TRUE(Decode(EitSection(0x77, 0x88), e));
  EXPECT_EQ(e.timeUTC, 0x123456u);
  EXPECT_EQ(e.duration, 0x012345u);
}

// ---------------------------------------------------------------------------
// EP-03: reads behind the descriptor
// ---------------------------------------------------------------------------

// An extended event descriptor whose length (5) holds the number byte, the language and length_of_items (0), but not the byte
// text_length that the decoder reads next: that byte is behind the descriptor. The descriptor is the last thing in the section, and the
// section is in a GuardedBuffer, so the byte behind it is an access violation.
TEST(EpgDecoder, ExtendedEventDescriptorWithoutTextLengthDoesNotReadBehindTheSection)
{
  vector<byte> section = EitSection(0x56, 0x45);
  section[24] = 0x80;
  section[25] = 7;                                   // descriptors_loop_length: the descriptor of 2 + 5 bytes
  const byte descriptor[7] = { 0x4E, 0x05, 0x00, 'e', 'n', 'g', 0x00 };
  section.insert(section.end(), descriptor, descriptor + sizeof(descriptor));
  GuardedBuffer guarded(section);
  CEpgDecoder decoder;
  decoder.GrabEPG();
  EXPECT_NO_THROW(decoder.DecodeEPG(guarded.data(), static_cast<int>(guarded.size()), 0x12));
}

// The reference: a complete extended event descriptor (number 0 of 0, "eng", no items, text "Hi").
TEST(EpgDecoder, CompleteExtendedEventDescriptorIsDecoded)
{
  vector<byte> section = EitSection(0x56, 0x45);
  section[24] = 0x80;
  section[25] = 10;
  const byte descriptor[10] = { 0x4E, 0x08, 0x00, 'e', 'n', 'g', 0x00, 0x02, 'H', 'i' };
  section.insert(section.end(), descriptor, descriptor + sizeof(descriptor));
  GuardedBuffer guarded(section);
  CEpgDecoder decoder;
  decoder.GrabEPG();
  decoder.DecodeEPG(guarded.data(), static_cast<int>(guarded.size()), 0x12);
  ASSERT_EQ(decoder.GetEPGChannelCount(), 1u);
  ASSERT_EQ(decoder.GetEPGEventCount(0), 1u);
  ULONG language = 0, mjd = 0, time = 0, duration = 0;
  char* genre = nullptr;
  char* classification = nullptr;
  int stars = 0;
  unsigned int id = 0;
  decoder.GetEPGEvent(0, 0, &language, &mjd, &time, &duration, &genre, &stars, &classification, &id);
  EXPECT_EQ(language, 1u);                           // the number of languages of the event
}

// ---------------------------------------------------------------------------
// EP-02: the tables of the decoder grow with the stream
// ---------------------------------------------------------------------------

namespace
{
  // sections of an EIT with one event each; 'network' and 'service' choose the channel, 'event' the event id
  vector<byte> EitFor(unsigned network, unsigned service, unsigned event)
  {
    vector<byte> s = EitSection(0x00, 0x00);
    s[3] = static_cast<byte>(service >> 8); s[4] = static_cast<byte>(service);
    s[10] = static_cast<byte>(network >> 8); s[11] = static_cast<byte>(network);
    s[14] = static_cast<byte>(event >> 8); s[15] = static_cast<byte>(event);
    s[6] = static_cast<byte>(event); s[7] = static_cast<byte>(event);          // the section number varies as well
    return s;
  }
}

// EP-02: a stream with one section for each of 20 000 different channels (original network ids). A real multiplex has a few
// hundred services, a whole satellite some thousands. The decoder keeps all of them.
TEST(EpgDecoder, NumberOfChannelsIsLimited)
{
  CEpgDecoder decoder;
  decoder.GrabEPG();
  for (unsigned i = 0; i < 20000; i++)
  {
    vector<byte> s = EitFor(i, 1, 1);
    decoder.DecodeEPG(s.data(), static_cast<int>(s.size()), 0x12);
  }
  EXPECT_LE(decoder.GetEPGChannelCount(), 8192u);
}

// EP-02: the Premiere content transmission descriptor makes channels too: a section for each of 20 000 different services.
TEST(EpgDecoder, NumberOfChannelsOfThePremierePathIsLimited)
{
  CEpgDecoder decoder;
  decoder.GrabEPG();
  for (unsigned i = 0; i < 20000; i++)
  {
    vector<byte> s(8 + 9 + 14, 0);
    s[1] = 0xB0; s[2] = 28;
    s[8 + 7] = 0x00; s[8 + 8] = 14;                  // the event: no id, no duration, 14 bytes of descriptors
    byte* d = &s[8 + 9];
    d[0] = 0xF2; d[1] = 12;                          // content transmission descriptor
    d[2] = 0x00; d[3] = 0x01;                        // transport stream id
    d[4] = static_cast<byte>(i >> 8); d[5] = static_cast<byte>(i);   // original network id: a channel of its own
    d[6] = 0x04; d[7] = 0x01;                        // service id
    d[8] = 0xE7; d[9] = 0x89;                        // date
    d[10] = 3;                                       // one start time (3 bytes)
    d[11] = 0x12; d[12] = 0x34; d[13] = 0x56;
    decoder.DecodePremierePrivateEPG(s.data(), static_cast<int>(s.size()));
  }
  EXPECT_GT(decoder.GetEPGChannelCount(), 0u);
  EXPECT_LE(decoder.GetEPGChannelCount(), 8192u);
}

// EP-02: 20 000 different events of one channel (a week of schedule is some thousands).
TEST(EpgDecoder, NumberOfEventsOfAChannelIsLimited)
{
  CEpgDecoder decoder;
  decoder.GrabEPG();
  for (unsigned i = 0; i < 20000; i++)
  {
    vector<byte> s = EitFor(1, 1, i);
    decoder.DecodeEPG(s.data(), static_cast<int>(s.size()), 0x12);
  }
  ASSERT_EQ(decoder.GetEPGChannelCount(), 1u);
  EXPECT_LE(decoder.GetEPGEventCount(0), 16384u);
}

// Ordinary amounts are kept whole: 300 channels with 50 events each.
TEST(EpgDecoder, OrdinaryAmountOfEpgIsKeptWhole)
{
  CEpgDecoder decoder;
  decoder.GrabEPG();
  for (unsigned service = 1; service <= 300; service++)
    for (unsigned event = 1; event <= 50; event++)
    {
      vector<byte> s = EitFor(0x3A, service, event);
      decoder.DecodeEPG(s.data(), static_cast<int>(s.size()), 0x12);
    }
  EXPECT_EQ(decoder.GetEPGChannelCount(), 300u);
  EXPECT_EQ(decoder.GetEPGEventCount(0), 50u);
}
