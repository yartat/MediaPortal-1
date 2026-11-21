// MPAudioRenderer: the id of the preferred WASAPI device is read from the registry of the current user and copied into a buffer of
// MAX_REG_LENGTH (256) characters (SECURITY_AUDIT.md: AR-01). wcsncpy() does not end the copy with a zero when the source has 256
// characters or more. The copy is in DeviceId.h, so that it can be tested without the registry and the renderer.
#include <windows.h>
#include <gtest/gtest.h>
#include <string>
#include "../../MPAudioRenderer/source/DeviceId.h"

namespace
{
  const size_t kChars = 256;
}

TEST(DeviceId, ShortIdIsCopiedWhole)
{
  WCHAR buffer[kChars];
  wmemset(buffer, L'#', kChars);
  CopyDeviceId(buffer, kChars, L"{0.0.0.00000000}.{abc}");
  EXPECT_STREQ(buffer, L"{0.0.0.00000000}.{abc}");
}

TEST(DeviceId, LongestIdThatFitsKeepsItsTerminator)
{
  const std::wstring id(kChars - 1, L'a');
  WCHAR buffer[kChars];
  wmemset(buffer, L'#', kChars);
  CopyDeviceId(buffer, kChars, id.c_str());
  EXPECT_EQ(std::wstring(buffer), id);
}

TEST(DeviceId, IdOfTheSizeOfTheBufferOrMoreIsCutAndEndsWithZero)
{
  for (size_t length : { kChars, kChars + 1, kChars * 4 })
  {
    SCOPED_TRACE(length);
    const std::wstring id(length, L'b');
    WCHAR buffer[kChars + 8];
    wmemset(buffer, L'#', kChars + 8);
    CopyDeviceId(buffer, kChars, id.c_str());
    EXPECT_EQ(std::wstring(buffer), std::wstring(kChars - 1, L'b'));
    EXPECT_EQ(buffer[kChars], L'#');                  // nothing written behind the buffer
  }
}

TEST(DeviceId, MissingSourceGivesAnEmptyId)
{
  WCHAR buffer[kChars];
  wmemset(buffer, L'#', kChars);
  CopyDeviceId(buffer, kChars, nullptr);
  EXPECT_EQ(buffer[0], 0);
}
