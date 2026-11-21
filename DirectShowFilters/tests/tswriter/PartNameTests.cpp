// TsWriter: the name of the next part of a recording (SECURITY_AUDIT.md: CD-03).
// When a file reaches the size limit of FAT, Recorder and FileWriterThreaded continue in "name_pN.ext". The new name is built in
// buffers of MAX_PATH characters, and the name of a recording can be as long as such a buffer (MAX_PATH is also the limit of the
// path of the file that was already open), so the suffix "_pN" does not fit.
// The result buffer is placed in a GuardedBuffer: a write behind its end is an access violation.
#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include "PartName.h"
#include "GuardedBuffer.h"

namespace
{
  // returns the new name, or "<failed>" when the function reports an error
  std::string Next(const std::string& name, int part, HRESULT* hrOut = nullptr)
  {
    GuardedBuffer out(MAX_PATH, 0xCD);
    HRESULT hr = BuildNextPartName(name.c_str(), part, reinterpret_cast<char*>(out.data()), MAX_PATH);
    if (hrOut)
      *hrOut = hr;
    return FAILED(hr) ? "<failed>" : std::string(reinterpret_cast<char*>(out.data()));
  }

  std::wstring NextW(const std::wstring& name, int part, HRESULT* hrOut = nullptr)
  {
    GuardedBuffer out(MAX_PATH * sizeof(wchar_t), 0xCD);
    HRESULT hr = BuildNextPartName(name.c_str(), part, reinterpret_cast<wchar_t*>(out.data()), MAX_PATH);
    if (hrOut)
      *hrOut = hr;
    return FAILED(hr) ? L"<failed>" : std::wstring(reinterpret_cast<wchar_t*>(out.data()));
  }

  // a path of 'total' characters that ends with ".ts"
  std::string PathOf(size_t total) { return std::string(total - 3, 'a') + ".ts"; }
}

TEST(PartName, OrdinaryNameGetsThePartBeforeTheExtension)
{
  EXPECT_EQ(Next("C:\\Recordings\\show.ts", 1), "C:\\Recordings\\show_p1.ts");
  EXPECT_EQ(Next("C:\\Recordings\\show.ts", 12345), "C:\\Recordings\\show_p12345.ts");
  EXPECT_EQ(NextW(L"C:\\Recordings\\show.ts", 2), L"C:\\Recordings\\show_p2.ts");
}

TEST(PartName, OnlyTheLastDotIsTheExtension)
{
  EXPECT_EQ(Next("C:\\Recordings\\news.2026.11.14.ts", 3), "C:\\Recordings\\news.2026.11.14_p3.ts");
}

TEST(PartName, LongestNameThatFitsIsAccepted)
{
  HRESULT hr = E_FAIL;
  // 256 characters and "_p1": 259 characters, the longest that fits in MAX_PATH with the terminator
  const std::string name = PathOf(256);
  const std::string next = Next(name, 1, &hr);
  ASSERT_EQ(hr, S_OK);
  EXPECT_EQ(next.size(), 259u);
  EXPECT_EQ(next.substr(next.size() - 6), "_p1.ts");
}

// CD-03: a name of 255 characters and part 12345: the result is 262 characters.
TEST(PartName, ResultLongerThanTheBufferIsRejected)
{
  HRESULT hr = S_OK;
  Next(PathOf(255), 12345, &hr);
  EXPECT_TRUE(FAILED(hr));
  HRESULT hrWide = S_OK;
  const std::string narrow = PathOf(255);
  NextW(std::wstring(narrow.begin(), narrow.end()), 12345, &hrWide);
  EXPECT_TRUE(FAILED(hrWide));
}

TEST(PartName, NameOf259CharactersIsRejected)
{
  HRESULT hr = S_OK;
  Next(PathOf(259), 1, &hr);
  EXPECT_TRUE(FAILED(hr));
}

// CD-03 (same code): a name without a dot. The search for the dot ends at the start, and the whole name is taken as the extension.
TEST(PartName, NameWithoutDotGetsThePartAtTheEnd)
{
  EXPECT_EQ(Next("C:\\Recordings\\show", 1), "C:\\Recordings\\show_p1");
}

// CD-03 (same code): the only dot is in a folder name.
TEST(PartName, DotInAFolderNameIsNotTheExtension)
{
  EXPECT_EQ(Next("C:\\my.recordings\\show", 1), "C:\\my.recordings\\show_p1");
}

// CD-03, long paths: the helper copies the name into buffers of MAX_PATH characters on its own stack, so a path of more than 259
// characters (a path with the prefix \?\, or a name from a buffer of 2 048 characters like m_szFileName) overwrites the stack.
// That ends the process, so these tests run in a child process (a death test): the child must answer with an error and exit normally.
TEST(PartNameDeathTest, VeryLongNameIsRejectedWithoutCrashing)
{
  EXPECT_EXIT(
    {
      HRESULT hr = S_OK;
      Next(PathOf(300), 1, &hr);
      HRESULT hr2 = S_OK;
      Next(PathOf(2000), 1, &hr2);
      exit((FAILED(hr) && FAILED(hr2)) ? 0 : 2);
    },
    ::testing::ExitedWithCode(0), "");
}

TEST(PartNameDeathTest, VeryLongWideNameIsRejectedWithoutCrashing)
{
  EXPECT_EXIT(
    {
      const std::string narrow = PathOf(2000);
      HRESULT hr = S_OK;
      NextW(std::wstring(narrow.begin(), narrow.end()), 1, &hr);
      exit(FAILED(hr) ? 0 : 2);
    },
    ::testing::ExitedWithCode(0), "");
}
