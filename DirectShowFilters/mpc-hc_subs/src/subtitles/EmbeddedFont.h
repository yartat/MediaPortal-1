// SSA/ASS embedded fonts ([Fonts], UUE text) come from the subtitle file (SECURITY_AUDIT.md: CD-04).
// The decoding is here so that it can be tested without the subtitle renderer; STS.cpp installs the result.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

// Encoded characters of one font (about 12 MB of font) and fonts of one file
const size_t EMBEDDED_FONT_MAX_TEXT = 16 * 1024 * 1024;
const int EMBEDDED_FONT_MAX_COUNT = 64;

// false: nothing to decode, a length that no UUE text has (4n + 1), or more than EMBEDDED_FONT_MAX_TEXT characters
inline bool DecodeEmbeddedFont(const std::wstring& text, std::vector<BYTE>& out)
{
  const size_t n = text.size();
  out.clear();
  if (n == 0 || (n & 3) == 1 || n > EMBEDDED_FONT_MAX_TEXT)
  {
    return false;
  }

  std::vector<BYTE> in(n);
  for (size_t i = 0; i < n; i++)
  {
    in[i] = BYTE(text[i] - 33);
  }

  out.reserve(n / 4 * 3 + 2);
  const size_t whole = n & ~size_t(3);
  for (size_t i = 0; i < whole; i += 4)
  {
    out.push_back(BYTE(((in[i + 0] & 63) << 2) | ((in[i + 1] >> 4) & 3)));
    out.push_back(BYTE(((in[i + 1] & 15) << 4) | ((in[i + 2] >> 2) & 15)));
    out.push_back(BYTE(((in[i + 2] & 3) << 6) | ((in[i + 3] >> 0) & 63)));
  }
  if ((n & 3) >= 2)
  {
    out.push_back(BYTE(((in[whole + 0] & 63) << 2) | ((in[whole + 1] >> 4) & 3)));
  }
  if ((n & 3) == 3)
  {
    out.push_back(BYTE(((in[whole + 1] & 15) << 4) | ((in[whole + 2] >> 2) & 15)));
  }
  return true;
}
