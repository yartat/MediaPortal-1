// shared/SystemLibrary.h: system DLLs are loaded from the system directory only (SECURITY_AUDIT.md: CD-07).
// A file with the name of a system DLL in the directory of the program must not be loaded instead of the real one.
#include <windows.h>
#include <gtest/gtest.h>
#include <string>
#include "SystemLibrary.h"

namespace
{
  std::wstring ModulePath(HMODULE module)
  {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    return path;
  }

  std::wstring SystemDirectory()
  {
    wchar_t path[MAX_PATH] = {};
    GetSystemDirectoryW(path, MAX_PATH);
    return path;
  }

  bool SamePath(const std::wstring& a, const std::wstring& b)
  {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
  }
}

TEST(SystemLibrary, LoadsADllFromTheSystemDirectory)
{
  HMODULE module = LoadSystemLibrary(L"avrt.dll");
  ASSERT_NE(module, nullptr);
  EXPECT_TRUE(SamePath(ModulePath(module), SystemDirectory() + L"\\avrt.dll")) << ModulePath(module).c_str();
  FreeLibrary(module);
}

TEST(SystemLibrary, TheNarrowNameWorksToo)
{
  HMODULE module = LoadSystemLibrary("avrt.dll");
  ASSERT_NE(module, nullptr);
  FreeLibrary(module);
}

TEST(SystemLibrary, ANameWithAPathIsRefused)
{
  EXPECT_EQ(LoadSystemLibrary(L"C:\\Windows\\System32\\avrt.dll"), nullptr);
  EXPECT_EQ(LoadSystemLibrary(L"..\\avrt.dll"), nullptr);
  EXPECT_EQ(LoadSystemLibrary(L"sub/avrt.dll"), nullptr);
  EXPECT_EQ(LoadSystemLibrary(L""), nullptr);
  EXPECT_EQ(LoadSystemLibrary(static_cast<const wchar_t*>(nullptr)), nullptr);
  EXPECT_EQ(LoadSystemLibrary("..\\avrt.dll"), nullptr);
}

TEST(SystemLibrary, ADllThatDoesNotExistGivesNull)
{
  EXPECT_EQ(LoadSystemLibrary(L"no_such_system_library_for_the_test.dll"), nullptr);
}

TEST(SystemLibrary, AFileOfTheSameNameNextToTheProgramIsNotLoaded)
{
  // mscms.dll: a DLL of the system that nothing else in the tests loads, so that the planted file disturbs no other test
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring planted = exe;
  planted.erase(planted.find_last_of(L'\\') + 1);
  planted += L"mscms.dll";
  HANDLE file = CreateFileW(planted.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(file, INVALID_HANDLE_VALUE) << "cannot plant the file next to the program";
  const char junk[] = "this is not a DLL";
  DWORD written = 0;
  WriteFile(file, junk, sizeof(junk), &written, nullptr);
  CloseHandle(file);

  HMODULE module = LoadSystemLibrary(L"mscms.dll");
  std::wstring where = module ? ModulePath(module) : L"";
  if (module)
    FreeLibrary(module);
  DeleteFileW(planted.c_str());

  ASSERT_NE(module, nullptr);
  EXPECT_TRUE(SamePath(where, SystemDirectory() + L"\\mscms.dll")) << where.c_str();
}
