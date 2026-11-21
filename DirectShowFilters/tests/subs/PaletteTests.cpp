// mpc-hc_subs: palettes of the embedded PGS and DVB subtitles (SECURITY_AUDIT.md: MS-01, MS-02).
// The segments come from a track of an MKV, M2TS or TS file, so every size in them is untrusted.
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
  // PGS: segment type, 16 bit size, data
  std::vector<BYTE> PgsPaletteSegment(BYTE paletteId, size_t entries, BYTE fill)
  {
    const size_t size = 2 + 5 * entries;
    std::vector<BYTE> s = { 0x14, static_cast<BYTE>(size >> 8), static_cast<BYTE>(size), paletteId, 0 };
    for (size_t i = 0; i < entries; i++)
      for (BYTE field : { static_cast<BYTE>(i), fill, fill, fill, fill })
        s.push_back(field);
    return s;
  }

  // DVB: data_identifier, stream id, then segments 0x0F, type, page id, length
  void DvbSegment(std::vector<BYTE>& out, BYTE type, const std::vector<BYTE>& payload)
  {
    out.insert(out.end(), { 0x0F, type, 0x00, 0x01, static_cast<BYTE>(payload.size() >> 8), static_cast<BYTE>(payload.size()) });
    out.insert(out.end(), payload.begin(), payload.end());
  }

  std::vector<BYTE> DvbPageThenClut(const std::vector<BYTE>& clutPayload)
  {
    std::vector<BYTE> s = { 0x20, 0x00 };
    DvbSegment(s, 0x10, { 0x05, 0x04 });             // page: time out 5 s, state "acquisition"
    DvbSegment(s, 0x12, clutPayload);
    return s;
  }

  // CLUT with 'entries' short entries (4 bytes each: id, flags, Y/Cr/Cb/T in 16 bits)
  std::vector<BYTE> DvbClutPayload(size_t entries, BYTE clutId = 0)
  {
    std::vector<BYTE> p = { clutId, 0x00 };
    for (size_t i = 0; i < entries; i++)
      p.insert(p.end(), { static_cast<BYTE>(i), 0x00, 0xFF, 0xFF });
    return p;
  }

  const LCID kLcid = 0;
}

// ---------------------------------------------------------------------------
// MS-01: PGS palette
// ---------------------------------------------------------------------------

class PgsPalette : public ::testing::Test
{
protected:
  CCritSec m_lock;
  PageHeap m_pageHeap;
  CPGSSub* m_sub = new CPGSSub(&m_lock, _T("test"), kLcid);

  void SetUp() override
  {
    for (size_t i = 0; i < m_sub->m_compositionObjects.size(); i++)
      m_sub->m_compositionObjects[i].m_width = static_cast<LONG>(0x1234 + i);   // markers: nothing may change them
  }

  void TearDown() override
  {
    // a parser whose memory was overwritten is not destroyed (its destructor would free garbage pointers)
    if (!HasFailure())
      delete m_sub;
  }

  void ExpectMarkersIntact()
  {
    for (size_t i = 0; i < m_sub->m_compositionObjects.size(); i++)
      ASSERT_EQ(m_sub->m_compositionObjects[i].m_width, static_cast<LONG>(0x1234 + i)) << "composition object " << i << " was overwritten";
  }

  HRESULT Parse(std::vector<BYTE> segment)
  {
    return m_sub->ParseSample(0, 0, segment.data(), segment.size());
  }
};

// The reference: a correct palette of 256 entries gives 256 colours, and only that palette is touched.
TEST_F(PgsPalette, ValidPaletteGives256Entries)
{
  ASSERT_EQ(Parse(PgsPaletteSegment(3, 256, 0x55)), S_OK);
  EXPECT_EQ(m_sub->m_CLUTs[3].id, 3);
  EXPECT_EQ(m_sub->m_CLUTs[3].size, 256);
  EXPECT_EQ(m_sub->m_CLUTs[3].palette[0].entry_id, 0);
  EXPECT_EQ(m_sub->m_CLUTs[3].palette[255].entry_id, 255);
  EXPECT_EQ(m_sub->m_CLUTs[3].palette[255].T, 0x55);
  EXPECT_EQ(m_sub->m_CLUTs[4].size, 0);
  ExpectMarkersIntact();
}

TEST_F(PgsPalette, ShortPaletteKeepsItsSize)
{
  ASSERT_EQ(Parse(PgsPaletteSegment(7, 2, 0x66)), S_OK);
  EXPECT_EQ(m_sub->m_CLUTs[7].size, 2);
  EXPECT_EQ(m_sub->m_CLUTs[7].palette[1].Cb, 0x66);
}

// MS-01: 13 106 entries of 5 bytes (the segment size is 16 bit) into an array of 256. With palette_id 255 the write goes over
// the next member of the parser, the composition objects.
TEST_F(PgsPalette, PaletteOfThousandsOfEntriesDoesNotWritePastTheArray)
{
  Parse(PgsPaletteSegment(255, 13106, 0xAB));
  EXPECT_LE(m_sub->m_CLUTs[255].size, 256);
  ExpectMarkersIntact();
}

// MS-01: the same with a palette in the middle of the array: the write goes over the following palettes.
TEST_F(PgsPalette, PaletteOfThousandsOfEntriesDoesNotWriteIntoTheNextPalette)
{
  Parse(PgsPaletteSegment(10, 13106, 0xAB));
  EXPECT_LE(m_sub->m_CLUTs[10].size, 256);
  EXPECT_EQ(m_sub->m_CLUTs[11].size, 0);
  EXPECT_EQ(m_sub->m_CLUTs[11].id, 0);
}

// MS-01, second way in: a segment of one byte. (nSize - 2) wraps to SIZE_MAX, which is a multiple of 5, so the check passes and
// 13 107 entries are "read" from past the end of the segment (the reader gives zeros) and written.
TEST_F(PgsPalette, OneByteSegmentIsRejected)
{
  Parse({ 0x14, 0x00, 0x01, 0xFF });
  EXPECT_EQ(m_sub->m_CLUTs[255].size, 0);
  ExpectMarkersIntact();
}

// After a rejected segment the parser still works.
TEST_F(PgsPalette, ParserWorksAfterARejectedPalette)
{
  Parse(PgsPaletteSegment(255, 13106, 0xAB));
  EXPECT_LE(m_sub->m_CLUTs[255].size, 256);
  ExpectMarkersIntact();
  ASSERT_FALSE(HasFailure());
  ASSERT_EQ(Parse(PgsPaletteSegment(3, 256, 0x55)), S_OK);
  EXPECT_EQ(m_sub->m_CLUTs[3].size, 256);
}

// ---------------------------------------------------------------------------
// MS-02: DVB CLUT
// ---------------------------------------------------------------------------

class DvbClut : public ::testing::Test
{
protected:
  CCritSec m_lock;
  PageHeap m_pageHeap;
  CDVBSub* m_sub = new CDVBSub(&m_lock, _T("test"), kLcid);

  void TearDown() override
  {
    if (!HasFailure())
      delete m_sub;
  }

  HRESULT Parse(std::vector<BYTE> sample)
  {
    return m_sub->ParseSample(0, 0, sample.data(), sample.size());
  }

  // the entries of the first CLUT of the current page
  const CDVBSub::DVB_CLUT* FirstClut()
  {
    if (!m_sub->m_pCurrentPage || m_sub->m_pCurrentPage->CLUTs.empty())
      return nullptr;
    return m_sub->m_pCurrentPage->CLUTs.front().get();
  }
};

// The reference: a correct CLUT of 256 short entries.
TEST_F(DvbClut, ValidClutIsParsed)
{
  Parse(DvbPageThenClut(DvbClutPayload(256)));
  const auto* clut = FirstClut();
  ASSERT_NE(clut, nullptr);
  EXPECT_EQ(clut->size, 256);
  EXPECT_EQ(clut->palette[255].entry_id, 255);
  EXPECT_EQ(clut->palette[255].Y, 0xFC);            // 6 bits of 0xFF, shifted left by 2
}

// An entry with the full range flag: id, flags (last bit set), Y, Cr, Cb, T (the transparency is inverted).
TEST_F(DvbClut, FullRangeEntryIsParsed)
{
  std::vector<BYTE> payload = { 4, 0x00, 9, 0x01, 0x10, 0x20, 0x30, 0x40 };
  Parse(DvbPageThenClut(payload));
  const auto* clut = FirstClut();
  ASSERT_NE(clut, nullptr);
  ASSERT_EQ(clut->size, 1);
  EXPECT_EQ(clut->palette[0].entry_id, 9);
  EXPECT_EQ(clut->palette[0].Y, 0x10);
  EXPECT_EQ(clut->palette[0].Cr, 0x20);
  EXPECT_EQ(clut->palette[0].Cb, 0x30);
  EXPECT_EQ(clut->palette[0].T, 0xFF - 0x40);
}

// MS-02: 16 383 short entries (the segment length is 16 bit) are written into an array of 256 on the heap.
TEST_F(DvbClut, ClutOfThousandsOfEntriesDoesNotWritePastTheArray)
{
  Parse(DvbPageThenClut(DvbClutPayload(16383)));
  const auto* clut = FirstClut();
  if (clut)                                          // a rejected CLUT may be missing, a kept one must be limited
    EXPECT_LE(clut->size, 256);
}

// After a rejected CLUT the parser still works.
TEST_F(DvbClut, ParserWorksAfterARejectedClut)
{
  Parse(DvbPageThenClut(DvbClutPayload(16383, 1)));
  ASSERT_FALSE(HasFailure());
  std::vector<BYTE> next = { 0x20, 0x00 };
  DvbSegment(next, 0x12, DvbClutPayload(3, 2));
  Parse(next);
  bool found = false;
  for (const auto& clut : m_sub->m_pCurrentPage->CLUTs)
    if (clut->id == 2)
    {
      found = true;
      EXPECT_EQ(clut->size, 3);
    }
  EXPECT_TRUE(found);
}
