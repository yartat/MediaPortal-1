// TsWriter: the ATSC/SCTE channel tables, the names of the channels (SECURITY_AUDIT.md: LV-01, NT-01).
// The names come from the broadcast. DisableCRCCheck() is defined in ParserTests.cpp.
#include <gtest/gtest.h>
#include <windows.h>
#include <bdatypes.h>
#include <map>
#include <string>
#include <vector>
using namespace std;
#include "BaseScteParser.h"
#include "NttParser.h"
#include "AllocLimit.h"

namespace
{
  // ---- LV-01 ----

  // an L-VCT channel with the given name; a terrestrial channel in the service map of the parser
  void ReceiveChannel(CBaseScteParser& parser, const string& name, int sourceId = 7)
  {
    vector<unsigned int> languages;
    std::vector<char> buffer(name.begin(), name.end());
    buffer.push_back(0);
    parser.OnLvctReceived(0xc8, buffer.data(), 5, 1, 4, 575000000, 1, 3, 0, false, false, 0, false, false, 2, sourceId, 1, 2, languages);
  }

  // ---- NT-01 ----

  // keeps what the parser says about each source name: the bytes of the buffer of the name, as long as it was allocated
  struct NameCollector : INttCallBack
  {
    size_t allocatedLength;                          // the length the test gave to the name (the buffer has one more byte)
    vector<string> raw;                              // the buffer, allocatedLength + 1 bytes
    vector<bool> isNull;

    explicit NameCollector(size_t length) : allocatedLength(length) {}

    void OnNttReceived(int /*sourceId*/, bool /*applicationType*/, char* name, unsigned int /*language*/) override
    {
      isNull.push_back(name == NULL);
      raw.push_back(name ? string(name, allocatedLength + 1) : string());
    }
  };

  // An NTT section (table 0xC3) with the source name table: one record whose name is 'name' (multilingual text).
  CSection NttSection(const vector<unsigned char>& name)
  {
    vector<unsigned char> b = { 0xC3, 0x00, 0x00, 0x00, 'e', 'n', 'g', 0x06, 1,    // table, length, version, language, subtype 6, 1 record
                                0x00, 0x00, 0x07,                                  // not an application, source id 7
                                static_cast<unsigned char>(name.size()) };
    b.insert(b.end(), name.begin(), name.end());
    b.push_back(0);                                                                // no descriptors
    b.push_back(0);                                                                // the byte at the end of the section
    b[1] = static_cast<unsigned char>((b.size() >> 8) & 0x0F);
    b[2] = static_cast<unsigned char>(b.size());
    CSection s;
    s.table_id = 0xC3;
    s.section_length = static_cast<int>(b.size());
    memcpy(s.Data, b.data(), b.size());
    return s;
  }

  string Terminated(const string& raw)
  {
    const size_t end = raw.find('\0');
    return end == string::npos ? string("<no terminator>") : raw.substr(0, end);
  }
}

// ---------------------------------------------------------------------------
// LV-01
// ---------------------------------------------------------------------------

TEST(ScteParser, ShortChannelNameIsStored)
{
  CBaseScteParser parser;
  ReceiveChannel(parser, "KABC");
  ASSERT_EQ(parser.GetChannelCount(), 1);
  CChannelInfo* info = NULL;
  ASSERT_TRUE(parser.GetChannel(0, &info));
  EXPECT_STREQ(info->ServiceName, "KABC");
  EXPECT_EQ(info->hasVideo, 1);
}

TEST(ScteParser, NameOf254CharactersIsStoredWhole)
{
  CBaseScteParser parser;
  const string name(254, 'a');
  ReceiveChannel(parser, name);
  CChannelInfo* info = NULL;
  ASSERT_TRUE(parser.GetChannel(0, &info));
  EXPECT_EQ(string(info->ServiceName), name);
}

// LV-01: the extended name of 247 bytes, " (", the short name of 7 and ")": 258 characters, 259 bytes with the terminator, for a
// buffer of 255. A name of 300 characters is the same defect.
TEST(ScteParser, NameLongerThanTheBufferIsCutOrRejected)
{
  for (size_t length : { size_t(255), size_t(258), size_t(300) })
  {
    SCOPED_TRACE(length);
    CBaseScteParser parser;
    ReceiveChannel(parser, string(length, 'a'));
    CChannelInfo* info = NULL;
    if (parser.GetChannelCount() == 0 || !parser.GetChannel(0, &info))
      continue;                                      // rejecting the channel is a possible answer
    EXPECT_NE(memchr(info->ServiceName, 0, sizeof(info->ServiceName)), nullptr) << "ServiceName has no terminator";
  }
}

// ---------------------------------------------------------------------------
// NT-01
// ---------------------------------------------------------------------------

namespace
{
  // runs the parser on one section and returns what it reported
  NameCollector Parse(const vector<unsigned char>& multilingualText)
  {
    HeapFill fill(0xCD);                              // new does not clear: the bytes the parser does not write stay 0xCD
    NameCollector collector(multilingualText.size());
    CNttParser parser;
    parser.SetCallBack(&collector);
    CSection section = NttSection(multilingualText);
    parser.OnNewSection(section);
    return collector;
  }
}

// The reference: one ASCII segment.
TEST(NttName, OneAsciiSegmentGivesItsText)
{
  NameCollector c = Parse({ 0x00, 0x04, 'K', 'A', 'B', 'C' });
  ASSERT_EQ(c.raw.size(), 1u);
  EXPECT_EQ(Terminated(c.raw[0]), "KABC");
}

// NT-01: the only segment is not ASCII (mode 1): the terminator is never written, and the callback gets 0xCD bytes.
TEST(NttName, TextWithoutAnAsciiSegmentIsEmptyOrMissing)
{
  NameCollector c = Parse({ 0x01, 0x02, 'x', 'y' });
  ASSERT_EQ(c.raw.size(), 1u);
  if (!c.isNull[0])
    EXPECT_EQ(Terminated(c.raw[0]), "") << "the name was read from memory that was never written";
}

// NT-01: two ASCII segments are one text. memcpy always writes at the start of the buffer, so the second replaces the first.
TEST(NttName, TwoAsciiSegmentsAreJoined)
{
  NameCollector c = Parse({ 0x00, 0x02, 'A', 'B', 0x00, 0x03, 'C', 'D', 'E' });
  ASSERT_EQ(c.raw.size(), 1u);
  EXPECT_EQ(Terminated(c.raw[0]), "ABCDE");
}
