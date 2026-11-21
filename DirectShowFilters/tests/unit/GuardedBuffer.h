// A copy of some bytes placed so that the last byte is the last byte before a page without access rights:
// any read or write past the end of the buffer is an access violation (and so a failing test).
// With Guard::Before the first byte is the first byte after such a page instead: a read or write before the start is caught.
#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

class GuardedBuffer
{
public:
  enum class Guard { After, Before };

  explicit GuardedBuffer(size_t size, uint8_t fill = 0, Guard guard = Guard::After) : m_size(size)
  {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const size_t page = si.dwPageSize;
    const size_t pages = (size + page - 1) / page;
    m_base = static_cast<uint8_t*>(VirtualAlloc(NULL, (pages + 1) * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    DWORD old = 0;
    if (guard == Guard::After)
    {
      VirtualProtect(m_base + pages * page, page, PAGE_NOACCESS, &old);
      m_data = m_base + pages * page - size;
    }
    else
    {
      VirtualProtect(m_base, page, PAGE_NOACCESS, &old);
      m_data = m_base + page;
    }
    memset(m_data, fill, size);
  }

  explicit GuardedBuffer(const std::vector<uint8_t>& bytes, Guard guard = Guard::After) : GuardedBuffer(bytes.size(), 0, guard)
  {
    if (!bytes.empty())
      memcpy(m_data, bytes.data(), bytes.size());
  }

  ~GuardedBuffer() { VirtualFree(m_base, 0, MEM_RELEASE); }
  GuardedBuffer(const GuardedBuffer&) = delete;
  GuardedBuffer& operator=(const GuardedBuffer&) = delete;

  uint8_t* data() { return m_data; }
  const uint8_t* data() const { return m_data; }
  size_t size() const { return m_size; }

private:
  uint8_t* m_base;
  uint8_t* m_data;
  size_t m_size;
};
