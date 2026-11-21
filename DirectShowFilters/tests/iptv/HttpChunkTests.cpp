// MPIPTVSource, MPIPTV_HTTP: the size of a chunk of the chunked transfer encoding (SECURITY_AUDIT.md: CD-01).
// The server (or someone between the player and the server: the protocol is plain HTTP) chooses the size line of every chunk.
// The allocator of the chunk data is replaced by one that remembers the largest request and refuses what is too large,
// so the tests never allocate the gigabytes the old code asks for.
#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <vector>
#include "HttpChunk.h"

namespace
{
  const size_t kLimit = 16u << 20;                   // the size of a chunk that a test may get
  size_t g_largestRequest = 0;

  void* LimitedAllocator(size_t size)
  {
    if (size > g_largestRequest)
      g_largestRequest = size;
    return size > kLimit ? NULL : CoTaskMemAlloc(size);
  }

  struct Result
  {
    bool ok;
    unsigned length;
    unsigned chunkLength;
    std::string data;
  };

  Result Get(const std::string& received)
  {
    g_largestRequest = 0;
    LinearBuffer buffer(64 * 1024);
    std::string copy = received;
    buffer.AddToBuffer(&copy[0], static_cast<unsigned>(copy.size()));
    Result r = { false, 0, 0, std::string() };
    char* data = NULL;
    r.ok = GetChunkData(&buffer, &data, &r.length, &r.chunkLength, LimitedAllocator);
    if (data)
    {
      r.data.assign(data, r.length <= kLimit ? r.length : 0);
      CoTaskMemFree(data);
    }
    return r;
  }
}

// The reference: a chunk of 26 bytes ("1A" in hexadecimal).
TEST(HttpChunk, ValidChunkGivesItsData)
{
  const std::string payload = "abcdefghijklmnopqrstuvwxyz";
  Result r = Get("1A\r\n" + payload + "\r\n");
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.length, 26u);
  EXPECT_EQ(r.chunkLength, 4u + 26u + 2u);
  EXPECT_EQ(r.data, payload);
}

TEST(HttpChunk, LowerCaseHexadecimalIsAccepted)
{
  Result r = Get("1a\r\n" + std::string(26, 'x') + "\r\n");
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.length, 26u);
}

// A chunk of size 0 is the end of the transmission.
TEST(HttpChunk, ZeroSizeIsTheEndOfTheTransmission)
{
  Result r = Get("0\r\n\r\n");
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.length, 0u);
}

// The data of the chunk have not arrived yet: nothing is returned.
TEST(HttpChunk, IncompleteChunkWaitsForTheRest)
{
  Result r = Get("1A\r\nabcdefghij");
  EXPECT_FALSE(r.ok);
  EXPECT_LE(g_largestRequest, kLimit);
}

// No line end yet: no size to read.
TEST(HttpChunk, SizeLineWithoutLineEndWaits)
{
  Result r = Get("1A");
  EXPECT_FALSE(r.ok);
}

// A size of 2 GB or more: strtol stops at 0x7FFFFFFF, and the client waits for that much data (no allocation, no overflow).
TEST(HttpChunk, HugePositiveSizeWaitsAndAllocatesNothing)
{
  for (const char* line : { "7FFFFFFF\r\n", "80000000\r\n", "FFFFFFFF\r\n" })
  {
    SCOPED_TRACE(line);
    Result r = Get(line);
    EXPECT_FALSE(r.ok);
    EXPECT_LE(g_largestRequest, kLimit);
  }
}

// CD-01: "-1" and "-2": strtol gives a negative number, which becomes 0xFFFFFFFF and 0xFFFFFFFE in the unsigned size, and the
// size + 2 wraps around to 1 and 0, so the check "enough data in the buffer" passes and 4 GB are asked for.
TEST(HttpChunk, NegativeSizeIsRejected)
{
  for (const char* line : { "-1\r\n", "-2\r\n", "-10\r\n", "-80000000\r\n" })
  {
    SCOPED_TRACE(line);
    Result r = Get(line);
    EXPECT_FALSE(r.ok);
    EXPECT_LE(g_largestRequest, kLimit) << "asked for " << g_largestRequest << " bytes";
  }
}

// An empty line or text instead of the size: whatever the answer is, no big allocation
TEST(HttpChunk, EmptyOrGarbageSizeLineAllocatesNothing)
{
  for (const char* line : { "\r\n", "xyz\r\n", " \r\n" })
  {
    SCOPED_TRACE(line);
    Get(line);
    EXPECT_LE(g_largestRequest, kLimit);
  }
}
