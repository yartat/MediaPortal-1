#include "AllocLimit.h"

#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace
{
  size_t g_limit = static_cast<size_t>(-1);
  size_t g_largest = 0;
  int g_refused = 0;

  bool g_pageHeap = false;
  int g_fill = -1;
  int g_pageBlocks = 0;
  const size_t kSlots = 1 << 16;
  struct Slot { void* user; void* base; } g_slots[kSlots];

  size_t SlotOf(void* p) { return (reinterpret_cast<uintptr_t>(p) >> 4) * 2654435761u % kSlots; }

  void* PageAllocate(size_t size)
  {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const size_t page = si.dwPageSize;
    const size_t pages = (size + page - 1) / page;
    uint8_t* base = static_cast<uint8_t*>(VirtualAlloc(NULL, (pages + 1) * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!base)
      throw std::bad_alloc();
    DWORD old = 0;
    VirtualProtect(base + pages * page, page, PAGE_NOACCESS, &old);
    void* user = reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(base + pages * page) - size) & ~static_cast<uintptr_t>(15));
    size_t i = SlotOf(user);
    while (g_slots[i].user)
      i = (i + 1) % kSlots;
    g_slots[i].user = user;
    g_slots[i].base = base;
    g_pageBlocks++;
    return user;
  }

  // true when p was a page heap block (and is released)
  bool PageFree(void* p)
  {
    if (!g_pageBlocks || !p)
      return false;
    for (size_t i = SlotOf(p); g_slots[i].user; i = (i + 1) % kSlots)
    {
      if (g_slots[i].user == p)
      {
        VirtualFree(g_slots[i].base, 0, MEM_RELEASE);
        // keep the probe chain intact: mark as a tombstone that never matches
        g_slots[i].user = reinterpret_cast<void*>(1);
        g_slots[i].base = NULL;
        g_pageBlocks--;
        return true;
      }
    }
    return false;
  }

  void* Allocate(size_t size)
  {
    if (g_limit != static_cast<size_t>(-1))
    {
      if (size > g_largest)
        g_largest = size;
      if (size > g_limit)
      {
        g_refused++;
        throw std::bad_alloc();
      }
    }
    void* p = g_pageHeap ? PageAllocate(size ? size : 1) : std::malloc(size ? size : 1);
    if (!p)
      throw std::bad_alloc();
    if (g_fill >= 0)
      std::memset(p, g_fill, size);
    return p;
  }
}

AllocLimit::AllocLimit(size_t limit)
{
  g_limit = limit;
  g_largest = 0;
  g_refused = 0;
}

AllocLimit::~AllocLimit()
{
  g_limit = static_cast<size_t>(-1);
}

size_t AllocLimit::LargestRequest() const { return g_largest; }
int AllocLimit::Refused() const { return g_refused; }

void* operator new(size_t size) { return Allocate(size); }
void* operator new[](size_t size) { return Allocate(size); }
HeapFill::HeapFill(unsigned char value) { g_fill = value; }
HeapFill::~HeapFill() { g_fill = -1; }
PageHeap::PageHeap() { g_pageHeap = true; }
PageHeap::~PageHeap() { g_pageHeap = false; }

namespace
{
  void Release(void* p)
  {
    if (!PageFree(p))
      std::free(p);
  }
}

void operator delete(void* p) noexcept { Release(p); }
void operator delete[](void* p) noexcept { Release(p); }
void operator delete(void* p, size_t) noexcept { Release(p); }
void operator delete[](void* p, size_t) noexcept { Release(p); }
