// BDReader/source/ModuleDirectory.h: the directory of bluray.dll (SECURITY_AUDIT.md: CD-06).
#include <gtest/gtest.h>
#include "ModuleDirectory.h"
#include "TempDir.h"

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

// --- the BD-J jar: libbluray 1.5.1 names it libbluray-j2se-<version>.jar, the old code always set LIBBLURAY_CP to libbluray.jar ---

TEST(BdjJar, TheVersionedJarOfLibbluray15IsFound)
{
  TempDir dir;
  ASSERT_TRUE(dir.Write(L"libbluray-j2se-1.5.1.jar", {1}));
  ASSERT_TRUE(dir.Write(L"libbluray-awt-j2se-1.5.1.jar", {1}));
  ModuleString jar;
  ASSERT_TRUE(FindBdjJar(dir.Path(), jar));
  EXPECT_EQ(jar, dir.File(L"libbluray-j2se-1.5.1.jar"));    // not the AWT jar, and not "libbluray.jar", which is not there
}

TEST(BdjJar, TheHighestVersionWins)
{
  TempDir dir;
  ASSERT_TRUE(dir.Write(L"libbluray-j2se-1.3.4.jar", {1}));
  ASSERT_TRUE(dir.Write(L"libbluray-j2se-1.5.1.jar", {1}));
  ModuleString jar;
  ASSERT_TRUE(FindBdjJar(dir.Path(), jar));
  EXPECT_EQ(jar, dir.File(L"libbluray-j2se-1.5.1.jar"));
}

TEST(BdjJar, TheOldNameIsTheFallback)
{
  TempDir dir;
  ASSERT_TRUE(dir.Write(L"libbluray.jar", {1}));
  ModuleString jar;
  ASSERT_TRUE(FindBdjJar(dir.Path(), jar));
  EXPECT_EQ(jar, dir.File(L"libbluray.jar"));
}

TEST(BdjJar, NoJarMeansNoBdj)
{
  TempDir dir;
  ASSERT_TRUE(dir.Write(L"libbluray-awt-j2se-1.5.1.jar", {1}));   // the AWT jar alone is no BD-J library
  ModuleString jar = L"old";
  EXPECT_FALSE(FindBdjJar(dir.Path(), jar));
  EXPECT_TRUE(jar.empty());
  EXPECT_FALSE(FindBdjJar(dir.File(L"no-such-folder"), jar));
}
