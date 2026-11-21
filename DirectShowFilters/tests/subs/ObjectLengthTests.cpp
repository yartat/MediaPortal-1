// mpc-hc_subs: the data length of PGS and DVB objects (SECURITY_AUDIT.md: MS-04).
// "length - 4" (PGS) and "segment length - 3" (DVB) are computed from numbers of the stream without a lower bound: a small value
// becomes a size of about 2^64, and new[] of that size throws std::bad_alloc in the thread that parses the track.
#include "stdafx.h"
#include <gtest/gtest.h>
#include <array>
#include <list>
#include <memory>
#include <thread>
#include <vector>
#include "RLECodedSubtitle.h"
#include "CompositionObject.h"
#include "../DSUtil/GolombBuffer.h"
// The tests look at the members of the parsers; the layout of the classes does not depend on the access.
#define private public
#include "PGSSub.h"
#include "DVBSub.h"
#undef private
#include "AllocLimit.h"

namespace
{
  const size_t kSane = 1u << 20;                     // no object of these tests needs more than 1 MiB

  // PGS object segment (type 0x15) of the first sequence fragment: id, version, sequence descriptor 0xC0 (first and last),
  // object_data_length (24 bit: this is width and height, 4 bytes, and the RLE data), width, height, RLE data
  std::vector<BYTE> PgsObject(unsigned objectDataLength, const std::vector<BYTE>& rle)
  {
    std::vector<BYTE> body = { 0x00, 0x01, 0x00, 0xC0,
                               static_cast<BYTE>(objectDataLength >> 16), static_cast<BYTE>(objectDataLength >> 8), static_cast<BYTE>(objectDataLength),
                               0x00, 0x10, 0x00, 0x04 };
    body.insert(body.end(), rle.begin(), rle.end());
    std::vector<BYTE> s = { 0x15, static_cast<BYTE>(body.size() >> 8), static_cast<BYTE>(body.size()) };
    s.insert(s.end(), body.begin(), body.end());
    return s;
  }

  void DvbSegment(std::vector<BYTE>& out, BYTE type, const std::vector<BYTE>& payload)
  {
    out.insert(out.end(), { 0x0F, type, 0x00, 0x01, static_cast<BYTE>(payload.size() >> 8), static_cast<BYTE>(payload.size()) });
    out.insert(out.end(), payload.begin(), payload.end());
  }

  const LCID kLcid = 0;
}

class PgsObjectLength : public ::testing::Test
{
protected:
  CCritSec m_lock;
  CPGSSub m_sub{ &m_lock, _T("test"), kLcid };

  HRESULT Parse(std::vector<BYTE> segment)
  {
    return m_sub.ParseSample(0, 0, segment.data(), segment.size());
  }
};

// The reference: an object of 3 bytes of RLE data is stored with its size.
TEST_F(PgsObjectLength, ValidObjectIsStored)
{
  ASSERT_EQ(Parse(PgsObject(4 + 3, { 0x01, 0x02, 0x03 })), S_OK);
  const CompositionObject& o = m_sub.m_compositionObjects[1];
  EXPECT_EQ(o.m_width, 16);
  EXPECT_EQ(o.m_height, 4);
  EXPECT_EQ(o.GetRLEDataSize(), 3u);
  EXPECT_NE(o.GetRLEData(), nullptr);
}

// MS-04: object_data_length 0, 1, 2 and 3: "length - 4" is negative, converted to size_t it is about 2^64.
TEST_F(PgsObjectLength, DataLengthBelowTheSizeFieldsDoesNotAllocateTheAddressSpace)
{
  for (unsigned length = 0; length <= 3; length++)
  {
    SCOPED_TRACE(length);
    AllocLimit limit(kSane);
    EXPECT_NO_THROW(Parse(PgsObject(length, { 0x01 })));
    EXPECT_LE(limit.LargestRequest(), kSane);
    EXPECT_EQ(m_sub.m_compositionObjects[1].GetRLEData(), nullptr);
  }
}

class DvbObjectLength : public ::testing::Test
{
protected:
  CCritSec m_lock;
  CDVBSub m_sub{ &m_lock, _T("test"), kLcid };

  HRESULT Parse(std::vector<BYTE> sample)
  {
    return m_sub.ParseSample(0, 0, sample.data(), sample.size());
  }

  std::vector<BYTE> PageThenObject(const std::vector<BYTE>& objectPayload)
  {
    std::vector<BYTE> s = { 0x20, 0x00 };
    DvbSegment(s, 0x10, { 0x05, 0x04 });             // page: time out 5 s, state "acquisition"
    DvbSegment(s, 0x13, objectPayload);
    s.insert(s.end(), 8, 0x00);                      // what the parser reads beyond a segment that is too short
    return s;
  }
};

// The reference: an object with 4 bytes of data (segment length 3 + 4).
TEST_F(DvbObjectLength, ValidObjectIsStored)
{
  Parse(PageThenObject({ 0x00, 0x07, 0x00, 0x01, 0x02, 0x03, 0x04 }));
  ASSERT_NE(m_sub.m_pCurrentPage, nullptr);
  ASSERT_EQ(m_sub.m_pCurrentPage->objects.size(), 1u);
  EXPECT_EQ(m_sub.m_pCurrentPage->objects.front()->GetRLEDataSize(), 4u);
}

// MS-04: a segment of 0, 1 and 2 bytes: "segment length - 3" wraps around.
TEST_F(DvbObjectLength, SegmentShorterThanItsHeaderDoesNotAllocateTheAddressSpace)
{
  const std::vector<std::vector<BYTE>> payloads = { {}, { 0x00 }, { 0x00, 0x07 } };
  for (const auto& payload : payloads)
  {
    SCOPED_TRACE(payload.size());
    AllocLimit limit(kSane);
    EXPECT_NO_THROW(Parse(PageThenObject(payload)));
    EXPECT_LE(limit.LargestRequest(), kSane);
  }
}
