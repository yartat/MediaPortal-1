// mpc-hc_subs: embedded fonts of SSA/ASS files, UUE text from the subtitle file (SECURITY_AUDIT.md: CD-04).
// The decoder is in EmbeddedFont.h; the old code decoded in place and had no limit on the size of the text.
#include "stdafx.h"
#include <gtest/gtest.h>
#include "EmbeddedFont.h"

namespace
{
  // The encoder of SSA/ASS (6 bits per character, offset 33), only to produce the input
  std::wstring Encode(const std::vector<BYTE>& data)
  {
    std::wstring s;
    size_t i = 0;
    for (; i + 3 <= data.size(); i += 3)
    {
      s += wchar_t(33 + (data[i] >> 2));
      s += wchar_t(33 + (((data[i] & 3) << 4) | (data[i + 1] >> 4)));
      s += wchar_t(33 + (((data[i + 1] & 15) << 2) | (data[i + 2] >> 6)));
      s += wchar_t(33 + (data[i + 2] & 63));
    }
    if (data.size() - i == 1)
    {
      s += wchar_t(33 + (data[i] >> 2));
      s += wchar_t(33 + ((data[i] & 3) << 4));
    }
    else if (data.size() - i == 2)
    {
      s += wchar_t(33 + (data[i] >> 2));
      s += wchar_t(33 + (((data[i] & 3) << 4) | (data[i + 1] >> 4)));
      s += wchar_t(33 + ((data[i + 1] & 15) << 2));
    }
    return s;
  }
}

TEST(EmbeddedFont, EveryLengthRoundTrips)
{
  for (size_t size = 1; size <= 64; size++)
  {
    std::vector<BYTE> data(size);
    for (size_t i = 0; i < size; i++)
      data[i] = BYTE(i * 37 + size);
    std::vector<BYTE> out;
    ASSERT_TRUE(DecodeEmbeddedFont(Encode(data), out)) << size;
    EXPECT_EQ(out, data) << size;
  }
}

TEST(EmbeddedFont, EmptyTextAndImpossibleLengthAreRefused)
{
  std::vector<BYTE> out;
  EXPECT_FALSE(DecodeEmbeddedFont(L"", out));
  EXPECT_FALSE(DecodeEmbeddedFont(L"A", out));        // 4n + 1 characters never come from an encoder
  EXPECT_FALSE(DecodeEmbeddedFont(L"ABCDE", out));
  EXPECT_TRUE(out.empty());
}

TEST(EmbeddedFont, TheTextOverTheLimitIsRefused)
{
  std::vector<BYTE> out;
  EXPECT_TRUE(DecodeEmbeddedFont(std::wstring(EMBEDDED_FONT_MAX_TEXT, L'A'), out));
  EXPECT_FALSE(DecodeEmbeddedFont(std::wstring(EMBEDDED_FONT_MAX_TEXT + 4, L'A'), out));
  EXPECT_TRUE(out.empty());
}

TEST(EmbeddedFont, CharactersOutsideTheAlphabetDoNotCrash)
{
  std::wstring text;
  for (int i = 0; i < 4096; i++)
    text += wchar_t(i * 7919);          // any 16-bit value, including 0 and values below 33
  std::vector<BYTE> out;
  DecodeEmbeddedFont(text, out);
  EXPECT_EQ(out.size(), text.size() / 4 * 3 + ((text.size() & 3) ? (text.size() & 3) - 1 : 0));
}

namespace
{
  // A real font: private fonts are accepted only when Windows can parse them. Windows always has Arial.
  bool ReadSystemFont(std::vector<BYTE>& out)
  {
    wchar_t dir[MAX_PATH];
    if (GetWindowsDirectoryW(dir, MAX_PATH) == 0)
      return false;
    HANDLE f = CreateFileW((std::wstring(dir) + L"\\Fonts\\arial.ttf").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
      return false;
    out.resize(GetFileSize(f, nullptr));
    DWORD got = 0;
    const BOOL ok = ReadFile(f, out.data(), (DWORD)out.size(), &got, nullptr);
    CloseHandle(f);
    return ok && got == out.size() && !out.empty();
  }

  // The font is installed again for each call: the result is a handle the caller must remove
  HANDLE InstallPrivately(const std::vector<BYTE>& font)
  {
    DWORD count = 0;
    return AddFontMemResourceEx(const_cast<BYTE*>(font.data()), (DWORD)font.size(), nullptr, &count);
  }
}

// A font that stays installed after its subtitle is closed is kept until the process ends: a player that opens a file with a
// font in it every evening for a month keeps all of them.
TEST(EmbeddedFont, TheHandlesAreRemovedWhenTheHolderIsDestroyed)
{
  std::vector<BYTE> font;
  if (!ReadSystemFont(font))
    GTEST_SKIP() << "no arial.ttf";
  HANDLE h = InstallPrivately(font);
  ASSERT_NE(h, nullptr);
  {
    EmbeddedFontHandles holder;
    holder.Add(h);
    holder.Add(nullptr);                         // a refused font is not counted
    EXPECT_EQ(holder.Count(), 1u);
  }
  EXPECT_FALSE(RemoveFontMemResourceEx(h));      // the holder has removed it: a handle that is gone cannot be removed again
}

TEST(EmbeddedFont, ACopyOwnsNoFont)
{
  std::vector<BYTE> font;
  if (!ReadSystemFont(font))
    GTEST_SKIP() << "no arial.ttf";
  HANDLE h = InstallPrivately(font);
  ASSERT_NE(h, nullptr);
  EmbeddedFontHandles holder;
  holder.Add(h);
  {
    EmbeddedFontHandles copy(holder);
    EXPECT_EQ(copy.Count(), 0u);
    EmbeddedFontHandles assigned;
    assigned = holder;
    EXPECT_EQ(assigned.Count(), 0u);
  }
  EXPECT_EQ(holder.Count(), 1u);
  holder.Release();
  EXPECT_EQ(holder.Count(), 0u);
  EXPECT_FALSE(RemoveFontMemResourceEx(h));
}
