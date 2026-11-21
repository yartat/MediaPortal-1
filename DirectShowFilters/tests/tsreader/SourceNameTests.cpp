// TsReader: the name given to CTsReaderFilter::Load and the .tsp file (SECURITY_AUDIT.md: TR-04).
// The name comes from the client (a path, or an RTSP address from a settings file, a channel list or the answer of TV Server).
#include <gtest/gtest.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "SourceName.h"
#include "GuardedBuffer.h"
#include "../unit/TempDir.h"

namespace
{
  const size_t kFileNameChars = 1024;                // WCHAR m_fileName[1024] of the filter
  const size_t kUrlBytes = MAX_PATH;                 // char url[MAX_PATH] of Load

  bool HasTerminator(const GuardedBuffer& g)
  {
    return memchr(g.data(), 0, g.size()) != nullptr;
  }

  wchar_t* Wide(GuardedBuffer& g) { return reinterpret_cast<wchar_t*>(g.data()); }
  char* Narrow(GuardedBuffer& g) { return reinterpret_cast<char*>(g.data()); }
}

TEST(SourceName, OrdinaryPathIsCopiedAndNarrowed)
{
  GuardedBuffer fileName(kFileNameChars * sizeof(wchar_t));
  GuardedBuffer url(kUrlBytes, 0xCD);
  ASSERT_EQ(PrepareSourceName(L"C:\\Recordings\\show.ts", Wide(fileName), kFileNameChars, Narrow(url), kUrlBytes), S_OK);
  EXPECT_STREQ(Wide(fileName), L"C:\\Recordings\\show.ts");
  EXPECT_STREQ(Narrow(url), "C:\\Recordings\\show.ts");
}

TEST(SourceName, RtspAddressIsCopiedAndNarrowed)
{
  GuardedBuffer fileName(kFileNameChars * sizeof(wchar_t));
  GuardedBuffer url(kUrlBytes, 0xCD);
  ASSERT_EQ(PrepareSourceName(L"rtsp://192.168.0.2:554/stream12.0", Wide(fileName), kFileNameChars, Narrow(url), kUrlBytes), S_OK);
  EXPECT_STREQ(Narrow(url), "rtsp://192.168.0.2:554/stream12.0");
}

// The longest name that fits both buffers: 259 characters.
TEST(SourceName, NameOf259CharactersIsAccepted)
{
  GuardedBuffer fileName(kFileNameChars * sizeof(wchar_t));
  GuardedBuffer url(kUrlBytes, 0xCD);
  const std::wstring name(259, L'a');
  ASSERT_EQ(PrepareSourceName(name.c_str(), Wide(fileName), kFileNameChars, Narrow(url), kUrlBytes), S_OK);
  EXPECT_EQ(strlen(Narrow(url)), 259u);
}

// TR-04: names of 1 024 and 2 000 characters: wcscpy writes behind the 1 024 characters of the filter.
TEST(SourceName, NameLongerThanTheFilterBufferIsRejected)
{
  for (size_t length : { kFileNameChars, size_t(2000) })
  {
    SCOPED_TRACE(length);
    GuardedBuffer fileName(kFileNameChars * sizeof(wchar_t));
    GuardedBuffer url(kUrlBytes, 0xCD);
    const std::wstring name(length, L'a');
    HRESULT hr = S_OK;
    EXPECT_NO_THROW(hr = PrepareSourceName(name.c_str(), Wide(fileName), kFileNameChars, Narrow(url), kUrlBytes));
    EXPECT_TRUE(FAILED(hr));
  }
}

// TR-04: 300 characters fit the filter but not the narrow name: WideCharToMultiByte fails and leaves url without a terminator.
TEST(SourceName, NameLongerThanTheUrlBufferIsRejectedOrTerminated)
{
  GuardedBuffer fileName(kFileNameChars * sizeof(wchar_t));
  GuardedBuffer url(kUrlBytes, 0xCD);
  const std::wstring name(300, L'a');
  HRESULT hr = PrepareSourceName(name.c_str(), Wide(fileName), kFileNameChars, Narrow(url), kUrlBytes);
  EXPECT_TRUE(FAILED(hr));
  EXPECT_TRUE(HasTerminator(url)) << "strlen(url) would read past the buffer";
}

namespace
{
  // a .tsp file: 100 bytes of header, then the address (or 'addressBytes' letters when address is null)
  FILE* OpenTsp(const TempDir& dir, size_t addressBytes, const char* address)
  {
    std::vector<uint8_t> bytes(100, 0x55);
    if (address)
      bytes.insert(bytes.end(), address, address + strlen(address));
    else
      bytes.insert(bytes.end(), addressBytes, static_cast<uint8_t>('a'));
    EXPECT_TRUE(dir.Write(L"a.tsp", bytes));
    return _wfopen(dir.File(L"a.tsp").c_str(), L"rb");
  }
}

TEST(TspUrl, AddressAfterTheHeaderIsRead)
{
  TempDir dir;
  FILE* fd = OpenTsp(dir, 0, "rtsp://192.168.0.2:554/stream12.0");
  ASSERT_NE(fd, nullptr);
  GuardedBuffer url(kUrlBytes, 0xCD);
  ReadTspUrl(fd, Narrow(url), kUrlBytes);
  fclose(fd);
  EXPECT_STREQ(Narrow(url), "rtsp://192.168.0.2:554/stream12.0");
}

// TR-04: an address of 400 bytes: fread fills all 260 bytes of url, and the terminator is written at url[260].
TEST(TspUrl, AddressLongerThanTheBufferKeepsItsTerminatorInside)
{
  TempDir dir;
  FILE* fd = OpenTsp(dir, 400, nullptr);
  ASSERT_NE(fd, nullptr);
  GuardedBuffer url(kUrlBytes, 0xCD);
  ReadTspUrl(fd, Narrow(url), kUrlBytes);
  fclose(fd);
  ASSERT_TRUE(HasTerminator(url));
  EXPECT_EQ(strlen(Narrow(url)), kUrlBytes - 1);
}

// A file with the header only: an empty address, no read behind the file.
TEST(TspUrl, FileWithOnlyTheHeaderGivesAnEmptyAddress)
{
  TempDir dir;
  FILE* fd = OpenTsp(dir, 0, "");
  ASSERT_NE(fd, nullptr);
  GuardedBuffer url(kUrlBytes, 0xCD);
  ReadTspUrl(fd, Narrow(url), kUrlBytes);
  fclose(fd);
  EXPECT_STREQ(Narrow(url), "");
}
