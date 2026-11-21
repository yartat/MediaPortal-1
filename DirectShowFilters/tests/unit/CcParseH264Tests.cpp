// Core-CC-Parser: closed captions in the SEI of H.264 (SECURITY_AUDIT.md: CC-01, CC-02, CC-03).
// The input of the parser is a sample of a file or of a broadcast, so every length in it is untrusted.
#include <gtest/gtest.h>
#include <windows.h>
#include <cstring>
#include <vector>
#include "CcParseH264.h"
#include "GuardedBuffer.h"
#include "AllocLimit.h"

namespace
{
  // One SEI NAL with one cc_data triplet (provider code 0x002F), 15 bytes with the NAL header and the trailing 0x80.
  std::vector<uint8_t> ValidSeiNal()
  {
    return { 0x06, 0x04, 0x0B,
             0xB5, 0x00, 0x2F, 0x03, 0x06, 0x41, 0xFF, 0xFC, 0x80, 0x80, 0xFF,
             0x80 };
  }

  // the same NAL behind a 4 byte big-endian length
  std::vector<uint8_t> ValidSample()
  {
    std::vector<uint8_t> nal = ValidSeiNal();
    std::vector<uint8_t> sample = { 0, 0, 0, static_cast<uint8_t>(nal.size()) };
    sample.insert(sample.end(), nal.begin(), nal.end());
    return sample;
  }

  const size_t kSane = 1u << 20;                    // no sample of these tests needs more than 1 MiB
}

// The reference: a correct sample gives the same cc_data block before and after every fix.
TEST(CcParseH264, ValidSampleGivesTheCcDataBlock)
{
  GuardedBuffer sample(ValidSample());
  CcParseH264 parser;
  ASSERT_EQ(parser.parseAVC1sample(sample.data(), static_cast<DWORD>(sample.size()), 4), 15u);

  const uint8_t expected[15] = { 0x0B, 0x00, 0x00, 0x00,             // length of the CC packet: 3 * 1 + 8
                                 0x47, 0x41, 0x39, 0x34,             // "GA94"
                                 0x03, 0x41, 0x00,                   // user_data_type_code, flags | cc_count, em_data
                                 0xFC, 0x80, 0x80, 0xFF };           // the triplet and the marker
  EXPECT_EQ(0, memcmp(parser.get_cc_buffer_pointer(), expected, sizeof(expected)));
}

// CC-01: a NAL of length 0 followed by a SEI header byte: new BYTE[0xFFFFFFFF] and a memcpy of 4 GiB.
TEST(CcParseH264, ZeroLengthNalDoesNotAskForFourGigabytes)
{
  const std::vector<uint8_t> bytes = { 0, 0, 0, 0, 0x06, 0, 0, 0 };
  GuardedBuffer sample(bytes);
  CcParseH264 parser;
  AllocLimit limit(kSane);
  DWORD result = 1;
  EXPECT_NO_THROW(result = parser.parseAVC1sample(sample.data(), static_cast<DWORD>(sample.size()), 4));
  EXPECT_EQ(result, 0u);
  EXPECT_LE(limit.LargestRequest(), kSane);
}

// CC-01: the zero length is the last thing in the sample, so the byte the code looks at is behind the sample.
TEST(CcParseH264, ZeroLengthNalAtTheEndOfTheSampleReadsNothingBehindIt)
{
  const std::vector<uint8_t> bytes = { 0, 0, 0, 0 };
  GuardedBuffer sample(bytes);
  CcParseH264 parser;
  AllocLimit limit(kSane);
  DWORD result = 1;
  EXPECT_NO_THROW(result = parser.parseAVC1sample(sample.data(), static_cast<DWORD>(sample.size()), 4));
  EXPECT_EQ(result, 0u);
}

// CC-02: the size of a SEI message is read while the byte is 0xFF, without looking at the end of the buffer.
TEST(CcParseH264, SeiOfOnlyFfBytesStaysInsideTheBuffer)
{
  for (size_t size : { size_t(2), size_t(16), size_t(4096) })
  {
    SCOPED_TRACE(size);
    GuardedBuffer sei(size, 0xFF);
    CcParseH264 parser;
    EXPECT_NO_THROW(parser.sei_rbsp(sei.data(), static_cast<int>(sei.size())));
  }
}

// CC-02: the same for the payload size: type 4 (a short, valid one), then only 0xFF.
TEST(CcParseH264, SeiWithRunOfFfInThePayloadSizeStaysInsideTheBuffer)
{
  std::vector<uint8_t> bytes = { 0x04 };
  bytes.insert(bytes.end(), 32, 0xFF);
  GuardedBuffer sei(bytes);
  CcParseH264 parser;
  EXPECT_NO_THROW(parser.sei_rbsp(sei.data(), static_cast<int>(sei.size())));
}

// CC-03: the payload is shorter than the 3 bytes of country and provider code the parser reads first (it reads the byte after the buffer).
TEST(CcParseH264, ShortUserDataSeiReadsNothingBehindTheBuffer)
{
  for (size_t payloadSize = 0; payloadSize <= 7; payloadSize++)
  {
    SCOPED_TRACE(payloadSize);
    std::vector<uint8_t> bytes = { 0x04, static_cast<uint8_t>(payloadSize) };
    bytes.push_back(0xB5);                           // country code first, so the parser goes on to the provider code
    bytes.insert(bytes.end(), payloadSize > 0 ? payloadSize - 1 : 0, 0x00);
    bytes.push_back(0x80);                           // rbsp trailing bits
    GuardedBuffer sei(bytes);
    CcParseH264 parser;
    EXPECT_NO_THROW(parser.sei_rbsp(sei.data(), static_cast<int>(sei.size())));
  }
}

// The valid SEI directly in sei_rbsp (without the NAL header byte): the same block comes out.
TEST(CcParseH264, SeiRbspOfAValidMessageGivesTheCcDataBlock)
{
  std::vector<uint8_t> nal = ValidSeiNal();
  GuardedBuffer sei(std::vector<uint8_t>(nal.begin() + 1, nal.end()));
  CcParseH264 parser;
  parser.sei_rbsp(sei.data(), static_cast<int>(sei.size()));
  // sei_rbsp does not return the size; the block is in the buffer of the parser
  const uint8_t* block = parser.get_cc_buffer_pointer();
  EXPECT_EQ(block[0], 0x0B);
  EXPECT_EQ(block[11], 0xFC);
}
