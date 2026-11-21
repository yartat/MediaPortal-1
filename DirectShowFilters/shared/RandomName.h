// A random name for a throw-away file (SECURITY_AUDIT.md: CD-11). The old generator called srand(timeGetTime()) every time:
// two calls in the same millisecond gave the same name, and the seed of rand() of the whole process was reset.
// std::random_device is the operating system generator and nothing is shared between threads.
#pragma once
#include <random>
#include <string>

inline std::wstring RandomName(int length)
{
  static const wchar_t charset[] = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
  std::random_device device;
  std::uniform_int_distribution<size_t> pick(0, sizeof(charset) / sizeof(charset[0]) - 2);
  std::wstring result;
  for (int i = 0; i < length; i++)
  {
    result += charset[pick(device)];
  }
  return result;
}
