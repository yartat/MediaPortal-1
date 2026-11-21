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
