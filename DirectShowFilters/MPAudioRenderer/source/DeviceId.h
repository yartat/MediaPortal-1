#pragma once

#include <cwchar>
#include <windows.h>

// AR-01: copies a device id into a buffer of dstChars characters and always ends it with a terminator (wcsncpy does not when the
// source is as long as the buffer or longer). A source that is too long is cut. Used for the id read from the registry.
inline void CopyDeviceId(WCHAR* dst, size_t dstChars, const wchar_t* src)
{
  if (!dst || dstChars == 0)
    return;
  const size_t n = src ? wcsnlen(src, dstChars - 1) : 0;
  if (n)
    wmemcpy(dst, src, n);
  dst[n] = 0;
}
