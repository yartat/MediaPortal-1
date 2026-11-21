// The parts of CTsReaderFilter::Load that turn the name given by the caller into the narrow name used to open the source, in
// functions that do not need the filter (so the tests can call them). The behaviour is that of Load before they were moved here;
// see SECURITY_AUDIT.md (TR-04).
#pragma once

#include <windows.h>
#include <cstdio>
#include <cwchar>

// fileName: the buffer of the filter (fileNameChars wide characters), url: the narrow name (urlBytes bytes).
// E_INVALIDARG when the name does not fit either buffer (url is then an empty string).
inline HRESULT PrepareSourceName(const wchar_t* name, wchar_t* fileName, size_t fileNameChars, char* url, size_t urlBytes)
{
  url[0] = 0;
  if (wcsnlen(name, fileNameChars) >= fileNameChars) return E_INVALIDARG;
  wcscpy_s(fileName, fileNameChars, name);
  if (WideCharToMultiByte(CP_ACP, 0, fileName, -1, url, static_cast<int>(urlBytes), 0, 0) == 0)
  {
    url[0] = 0;
    return E_INVALIDARG;
  }
  return S_OK;
}

// A .tsp file: 100 bytes of header, then the RTSP address. The address is read into url (urlBytes bytes) and ended with a zero.
inline size_t ReadTspUrl(FILE* fd, char* url, size_t urlBytes)
{
  fread(url, 1, 100, fd);
  size_t bytesRead = fread(url, 1, urlBytes - 1, fd);
  url[bytesRead] = 0;
  return bytesRead;
}
