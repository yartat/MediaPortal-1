// BDReader/source/ModuleDirectory.h: the directory of bluray.dll (SECURITY_AUDIT.md: CD-06).
#include <gtest/gtest.h>
#include "ModuleDirectory.h"

TEST(ModuleDirectory, ParentDirectoryOfAPath)
{
  ModuleString dir;
  ASSERT_TRUE(ParentDirectory(_T("C:\\Program Files\\Team MediaPortal\\BDReader.ax"), dir));
  EXPECT_EQ(dir, ModuleString(_T("C:\\Program Files\\Team MediaPortal")));
  ASSERT_TRUE(ParentDirectory(_T("C:\\a.ax"), dir));
  EXPECT_EQ(dir, ModuleString(_T("C:")));
}

TEST(ModuleDirectory, APathWithoutADirectoryGivesNothing)
{
  ModuleString dir = _T("old");
  EXPECT_FALSE(ParentDirectory(_T("BDReader.ax"), dir));   // the old code: _tcsrchr is NULL, the length is a huge number
  EXPECT_TRUE(dir.empty());
  EXPECT_FALSE(ParentDirectory(_T(""), dir));
  EXPECT_FALSE(ParentDirectory(_T("\\BDReader.ax"), dir));
  EXPECT_FALSE(ParentDirectory(NULL, dir));
}

TEST(ModuleDirectory, ALongPathIsNotCut)
{
  ModuleString path = _T("C:\\");
  for (int i = 0; i < 40; i++)
    path += _T("directory\\");   // far over MAX_PATH: the old code worked in buffers of MAX_PATH
  path += _T("BDReader.ax");
  ModuleString dir;
  ASSERT_TRUE(ParentDirectory(path.c_str(), dir));
  EXPECT_EQ(dir.size(), path.size() - ModuleString(_T("\\BDReader.ax")).size());
  EXPECT_EQ(dir.back(), _T('y'));
}

TEST(ModuleDirectory, DirectoryOfTheModuleOfTheTestProgram)
{
  ModuleString dir;
  ASSERT_TRUE(DirectoryOfModuleAt((const void*)&ParentDirectory, dir));
  TCHAR exe[MAX_PATH];
  GetModuleFileName(NULL, exe, MAX_PATH);
  ModuleString expected;
  ASSERT_TRUE(ParentDirectory(exe, expected));
  EXPECT_EQ(dir, expected);
}

TEST(ModuleDirectory, AnAddressOfNoModuleGivesNothing)
{
  ModuleString dir;
  EXPECT_FALSE(DirectoryOfModuleAt(NULL, dir));
}
