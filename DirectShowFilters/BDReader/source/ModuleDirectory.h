// The directory of a loaded module (SECURITY_AUDIT.md: CD-06). Initialize() of CLibBlurayWrapper built the path of bluray.dll
// from it with _tcsncpy (no terminating zero, a huge length when the path has no backslash) and with _stprintf_s that read and
// wrote the same buffer.
#pragma once
#include <windows.h>
#include <tchar.h>
#include <string>

typedef std::basic_string<TCHAR> ModuleString;

// "C:\dir\file.ax" gives "C:\dir". false: no backslash, or nothing before it (nothing to build a path from).
inline bool ParentDirectory(const TCHAR* path, ModuleString& directory)
{
  directory.clear();
  if (path == NULL)
  {
    return false;
  }
  const TCHAR* slash = _tcsrchr(path, _T('\\'));
  if (slash == NULL || slash == path)
  {
    return false;
  }
  directory.assign(path, slash - path);
  return true;
}

// The directory of the module that contains 'address'. false when Windows does not know the module, the path is cut at
// MAX_PATH (GetModuleFileName fills the buffer and the path would be wrong) or has no directory.
inline bool DirectoryOfModuleAt(const void* address, ModuleString& directory)
{
  HMODULE module = NULL;
  const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
  if (!GetModuleHandleEx(flags, (LPCTSTR)address, &module))
  {
    return false;
  }
  TCHAR path[MAX_PATH];
  DWORD length = GetModuleFileName(module, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH)
  {
    return false;
  }
  return ParentDirectory(path, directory);
}

// The BD-J library of libbluray in 'directory': libbluray-j2se-<version>.jar (libbluray 1.3 and later; the highest version when
// there are several) or libbluray.jar (older layout). LIBBLURAY_CP must name this file: libbluray makes the name of the AWT jar
// (libbluray-awt-j2se-<version>.jar, beside it) from the name of this one, so a name that is not there makes BD-J fail and a
// disc with a BD-J menu cannot start a title. false: no jar, BD-J is not available.
inline bool FindBdjJar(const ModuleString& directory, ModuleString& jar)
{
  jar.clear();
  WIN32_FIND_DATA found;
  HANDLE search = FindFirstFile((directory + _T("\\libbluray-j2se-*.jar")).c_str(), &found);
  if (search != INVALID_HANDLE_VALUE)
  {
    ModuleString best;
    do
    {
      if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && _tcsicmp(found.cFileName, best.c_str()) > 0)
      {
        best = found.cFileName;
      }
    } while (FindNextFile(search, &found));
    FindClose(search);
    if (!best.empty())
    {
      jar = directory + _T("\\") + best;
      return true;
    }
  }
  const ModuleString old = directory + _T("\\libbluray.jar");
  const DWORD attributes = GetFileAttributes(old.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
  {
    jar = old;
    return true;
  }
  return false;
}
