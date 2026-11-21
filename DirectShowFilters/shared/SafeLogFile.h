// Opens a log file without following links (SECURITY_AUDIT.md: LG-01).
// The log folder below %ProgramData% is writable for every local user (the installer gives the folder to "Everyone"), and the
// TV Server services write their logs from there. fopen follows a symbolic link, a junction or a hard link that a user has put
// in place of the log file, and then the service appends to a file of its own account's choice.
// The file is refused when it is a reparse point (symbolic link), has more than one name (hard link), is not a plain file, or
// when the path leads through a junction (the final path of the opened file is not the path that was asked for).
// Returns NULL (no logging) in those cases, like fopen when the file cannot be opened. mode: as for _tfopen ("a+", "a+, ccs=UTF-8").
#pragma once
#include <windows.h>
#include <tchar.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>

#ifdef UNICODE
#define SAFELOG_SUFFIX "W"
#else
#define SAFELOG_SUFFIX "A"
#endif

// The path in the form of GetFinalPathNameByHandle without the "\\?\" prefix, to compare with a full path
inline BOOL SafeLogSamePath(const TCHAR* wanted, HANDLE file)
{
  TCHAR full[MAX_PATH * 2];
  DWORD fullLength = GetFullPathName(wanted, MAX_PATH * 2, full, NULL);
  if (fullLength == 0 || fullLength >= MAX_PATH * 2)
  {
    return FALSE;
  }
  TCHAR longName[MAX_PATH * 2];
  DWORD longLength = GetLongPathName(full, longName, MAX_PATH * 2);     // the final path has no 8.3 names; the path asked for may
  if (longLength > 0 && longLength < MAX_PATH * 2)
  {
    _tcscpy_s(full, longName);
  }
  if (!(((full[0] >= _T('A') && full[0] <= _T('Z')) || (full[0] >= _T('a') && full[0] <= _T('z'))) && full[1] == _T(':')))
  {
    return TRUE;    // a network path or a device path: no drive letter to compare
  }
  // Looked up at run time: the filters are built for _WIN32_WINNT below Vista, where the declaration is missing. Without the
  // function (Windows XP) the path is not compared.
  typedef DWORD(WINAPI * GetFinalPathNameByHandleFn)(HANDLE, LPTSTR, DWORD, DWORD);
  static const DWORD kNormalizedDos = 0;                                     // FILE_NAME_NORMALIZED | VOLUME_NAME_DOS
  HMODULE kernel = GetModuleHandle(_T("kernel32.dll"));
  GetFinalPathNameByHandleFn getFinalPath = kernel ? reinterpret_cast<GetFinalPathNameByHandleFn>(GetProcAddress(kernel, "GetFinalPathNameByHandle" SAFELOG_SUFFIX)) : NULL;
  if (getFinalPath == NULL)
  {
    return TRUE;
  }
  TCHAR final[MAX_PATH * 2];
  DWORD finalLength = getFinalPath(file, final, MAX_PATH * 2, kNormalizedDos);
  if (finalLength == 0 || finalLength >= MAX_PATH * 2)
  {
    return FALSE;
  }
  const TCHAR* f = final;
  if (_tcsncmp(f, _T("\\\\?\\"), 4) == 0)
  {
    f += 4;
  }
  return _tcsicmp(f, full) == 0;
}

inline FILE* OpenLogFile(const TCHAR* path, const TCHAR* mode)
{
  if (path == NULL || mode == NULL)
  {
    return NULL;
  }
  // The folder first: a junction in its place would let CreateFile make the file in another folder before anything can be checked.
  // ponytail: the folder could still be swapped between this check and CreateFile; closing that needs NtCreateFile relative to the folder handle.
  TCHAR folder[MAX_PATH * 2];
  if (_tcslen(path) >= MAX_PATH * 2)
  {
    return NULL;
  }
  _tcscpy_s(folder, path);
  TCHAR* slash = _tcsrchr(folder, _T('\\'));
  if (slash == NULL)
  {
    _tcscpy_s(folder, _T("."));
  }
  else if (slash == folder || *(slash - 1) == _T(':'))
  {
    slash[1] = 0;                                    // the root: keep the backslash
  }
  else
  {
    *slash = 0;
  }
  HANDLE directory = CreateFile(folder, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (directory == INVALID_HANDLE_VALUE)
  {
    return NULL;
  }
  const BOOL folderIsAsAsked = SafeLogSamePath(folder, directory);
  CloseHandle(directory);
  if (!folderIsAsAsked)
  {
    return NULL;
  }

  HANDLE file = CreateFile(path, GENERIC_READ | FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  if (file == INVALID_HANDLE_VALUE)
  {
    return NULL;
  }
  BY_HANDLE_FILE_INFORMATION info;
  if (!GetFileInformationByHandle(file, &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0 ||
      info.nNumberOfLinks != 1 ||
      !SafeLogSamePath(path, file))
  {
    CloseHandle(file);
    return NULL;
  }
  int fd = _open_osfhandle(reinterpret_cast<intptr_t>(file), _O_APPEND | _O_TEXT);
  if (fd < 0)
  {
    CloseHandle(file);
    return NULL;
  }
  FILE* fp = _tfdopen(fd, mode);
  if (fp == NULL)
  {
    _close(fd);     // closes the handle too
  }
  return fp;
}
