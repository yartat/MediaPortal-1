// shared/SafeLogFile.h: a log file is not opened through a link that a user has put in its place (SECURITY_AUDIT.md: LG-01).
#include <gtest/gtest.h>
#include <windows.h>
#include <winioctl.h>
#include <string>
#include <vector>
#include "SafeLogFile.h"
#include "TempDir.h"

namespace
{
  std::wstring Narrow(const wchar_t* s) { return s; }

  std::string ReadAll(const std::wstring& path)
  {
    std::string text;
    if (FILE* f = _wfopen(path.c_str(), L"rb"))
    {
      char buffer[256];
      size_t n;
      while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
        text.append(buffer, n);
      fclose(f);
    }
    return text;
  }

  // A directory junction: needs no privilege (a symbolic link does)
  bool MakeJunction(const std::wstring& junction, const std::wstring& target)
  {
    if (!CreateDirectoryW(junction.c_str(), nullptr))
      return false;
    HANDLE h = CreateFileW(junction.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
      return false;
    const std::wstring substitute = L"\\??\\" + target;
    const USHORT bytes = static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
    std::vector<unsigned char> buffer(8 + 8 + bytes + 2 + 2, 0);
    unsigned char* b = buffer.data();
    *reinterpret_cast<ULONG*>(b) = 0xA0000003;                                  // IO_REPARSE_TAG_MOUNT_POINT
    *reinterpret_cast<USHORT*>(b + 4) = static_cast<USHORT>(8 + bytes + 2 + 2);  // ReparseDataLength
    *reinterpret_cast<USHORT*>(b + 8) = 0;                                      // SubstituteNameOffset
    *reinterpret_cast<USHORT*>(b + 10) = bytes;                                 // SubstituteNameLength
    *reinterpret_cast<USHORT*>(b + 12) = static_cast<USHORT>(bytes + 2);        // PrintNameOffset
    *reinterpret_cast<USHORT*>(b + 14) = 0;                                     // PrintNameLength
    memcpy(b + 16, substitute.c_str(), bytes);
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, b, static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(h);
    return ok != FALSE;
  }
}

TEST(SafeLogFile, OpensAndAppendsToAPlainFile)
{
  TempDir dir;
  const std::wstring path = dir.File(L"a.log");
  for (const char* line : {"one\n", "two\n"})
  {
    FILE* f = OpenLogFile(path.c_str(), _T("a+"));
    ASSERT_NE(f, nullptr);
    fputs(line, f);
    fclose(f);
  }
  EXPECT_EQ(ReadAll(path), "one\r\ntwo\r\n");     // text mode, like fopen("a+")
}

TEST(SafeLogFile, TheModeOfTheStreamServerWorks)
{
  TempDir dir;
  const std::wstring path = dir.File(L"u.log");
  FILE* f = OpenLogFile(path.c_str(), _T("a+, ccs=UTF-8"));
  ASSERT_NE(f, nullptr);
  fwprintf(f, L"café\n");
  fclose(f);
  const std::string text = ReadAll(path);
  EXPECT_NE(text.find("caf\xC3\xA9"), std::string::npos);     // UTF-8
}

TEST(SafeLogFile, AHardLinkToAnotherFileIsRefused)
{
  TempDir dir;
  ASSERT_TRUE(dir.Write(L"victim.txt", {'v', 'i', 'c', 't', 'i', 'm'}));
  const std::wstring link = dir.File(L"service.log");
  ASSERT_TRUE(CreateHardLinkW(link.c_str(), dir.File(L"victim.txt").c_str(), nullptr)) << GetLastError();
  EXPECT_EQ(OpenLogFile(link.c_str(), _T("a+")), nullptr);
  EXPECT_EQ(ReadAll(dir.File(L"victim.txt")), "victim");        // not appended to
}

TEST(SafeLogFile, ASymbolicLinkIsRefused)
{
  TempDir dir;
  ASSERT_TRUE(dir.Write(L"victim.txt", {'v'}));
  const std::wstring link = dir.File(L"service.log");
  if (!CreateSymbolicLinkW(link.c_str(), dir.File(L"victim.txt").c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
    GTEST_SKIP() << "cannot create a symbolic link (no privilege and no developer mode)";
  EXPECT_EQ(OpenLogFile(link.c_str(), _T("a+")), nullptr);
  EXPECT_EQ(ReadAll(dir.File(L"victim.txt")), "v");
  DeleteFileW(link.c_str());
}

TEST(SafeLogFile, AFolderJunctionInTheWayIsRefused)
{
  TempDir elsewhere;
  TempDir dir;
  const std::wstring junction = dir.File(L"log");
  ASSERT_TRUE(MakeJunction(junction, elsewhere.Path())) << GetLastError();
  FILE* f = OpenLogFile((junction + L"\\service.log").c_str(), _T("a+"));
  const bool opened = f != nullptr;
  if (f)
    fclose(f);
  RemoveDirectoryW(junction.c_str());                           // the link only, not what it points to
  EXPECT_FALSE(opened);
  EXPECT_EQ(GetFileAttributesW(elsewhere.File(L"service.log").c_str()), INVALID_FILE_ATTRIBUTES) << "the file was made in the other folder";
}

TEST(SafeLogFile, AFolderIsRefusedAndNothingIsCreatedByARefusal)
{
  TempDir dir;
  EXPECT_EQ(OpenLogFile(dir.Path().c_str(), _T("a+")), nullptr);
  EXPECT_EQ(OpenLogFile(nullptr, _T("a+")), nullptr);
  EXPECT_EQ(OpenLogFile(dir.File(L"x\\y.log").c_str(), _T("a+")), nullptr);      // the folder does not exist
}
