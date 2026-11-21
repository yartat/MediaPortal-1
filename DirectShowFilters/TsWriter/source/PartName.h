// The name of the next part of a recording: "name.ext" becomes "name_pN.ext". Recorder and FileWriterThreaded start a new part when a
// file reaches the size limit of FAT. In a header so that the tests can call it; see SECURITY_AUDIT.md (CD-03).
#pragma once

#include <windows.h>
#include <string>

// The extension is what follows the last dot of the file name (a dot in a folder name does not count); a name without a dot gets
// "_pN" at the end. newFileName is the buffer of the caller (newSize characters): when the result does not fit, it is set to an
// empty string and E_INVALIDARG is returned.
template <typename C>
inline HRESULT BuildNextPartNameT(const C* currentName, int part, C* newFileName, size_t newSize)
{
  std::basic_string<C> name(currentName);
  size_t dot = std::basic_string<C>::npos;
  for (size_t i = name.size(); i > 0; i--)
  {
    const C c = name[i - 1];
    if (c == static_cast<C>(0x5C) || c == static_cast<C>('/'))
      break;
    if (c == static_cast<C>('.'))
    {
      dot = i - 1;
      break;
    }
  }

  const std::string partText = "_p" + std::to_string(part);
  std::basic_string<C> result = (dot == std::basic_string<C>::npos) ? name : name.substr(0, dot);
  result.append(partText.begin(), partText.end());
  if (dot != std::basic_string<C>::npos)
    result.append(name, dot, std::basic_string<C>::npos);

  if (newSize == 0)
    return E_INVALIDARG;
  if (result.size() >= newSize)
  {
    newFileName[0] = 0;
    return E_INVALIDARG;
  }
  result.copy(newFileName, result.size());
  newFileName[result.size()] = 0;
  return S_OK;
}

inline HRESULT BuildNextPartName(const char* currentName, int part, char* newFileName, size_t newSize)
{
  return BuildNextPartNameT(currentName, part, newFileName, newSize);
}

inline HRESULT BuildNextPartName(const wchar_t* currentName, int part, wchar_t* newFileName, size_t newSize)
{
  return BuildNextPartNameT(currentName, part, newFileName, newSize);
}
