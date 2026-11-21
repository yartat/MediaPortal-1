// Loads a DLL of Windows from the system directory only, never from the directory of the application or the current directory
// (SECURITY_AUDIT.md: CD-07). A bare LoadLibrary("avrt.dll") looks in the application directory first.
// name: a file name without a path.
#pragma once
#include <windows.h>
#include <string.h>
#include <wchar.h>

inline HMODULE LoadSystemLibrary(const wchar_t* name)
{
  if (!name || !*name || wcspbrk(name, L"\\/:"))
  {
    SetLastError(ERROR_INVALID_NAME);
    return NULL;
  }
  HMODULE module = LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (module || GetLastError() != ERROR_INVALID_PARAMETER)
  {
    return module;
  }
  // Windows 7 without update KB2533623 does not know the flag: the full path
  wchar_t path[MAX_PATH];
  UINT length = GetSystemDirectoryW(path, MAX_PATH);
  if (length == 0 || length + 1 + wcslen(name) >= MAX_PATH)
  {
    return NULL;
  }
  path[length] = L'\\';
  wcscpy_s(path + length + 1, MAX_PATH - length - 1, name);
  return LoadLibraryW(path);
}

inline HMODULE LoadSystemLibrary(const char* name)
{
  if (!name || !*name || strpbrk(name, "\\/:"))
  {
    SetLastError(ERROR_INVALID_NAME);
    return NULL;
  }
  HMODULE module = LoadLibraryExA(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (module || GetLastError() != ERROR_INVALID_PARAMETER)
  {
    return module;
  }
  char path[MAX_PATH];
  UINT length = GetSystemDirectoryA(path, MAX_PATH);
  if (length == 0 || length + 1 + strlen(name) >= MAX_PATH)
  {
    return NULL;
  }
  path[length] = '\\';
  strcpy_s(path + length + 1, MAX_PATH - length - 1, name);
  return LoadLibraryA(path);
}
