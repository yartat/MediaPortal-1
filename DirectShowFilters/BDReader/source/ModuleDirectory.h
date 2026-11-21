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
