// Lets a test see (and survive) a huge allocation request of the code under test without allocating gigabytes:
// while an AllocLimit is alive, operator new / new[] throw std::bad_alloc for a request larger than the limit
// and remember the largest request. AllocLimit.cpp replaces the global operator new of the test program.
#pragma once

#include <cstddef>

class AllocLimit
{
public:
  explicit AllocLimit(size_t limit);
  ~AllocLimit();
  AllocLimit(const AllocLimit&) = delete;
  AllocLimit& operator=(const AllocLimit&) = delete;

  // the largest single request seen since the limit was set (0 if none)
  size_t LargestRequest() const;
  // number of requests above the limit (they all failed with bad_alloc)
  int Refused() const;
};

// While a PageHeap is alive every operator new places its block right in front of a page without access rights (like the page heap of
// Windows): a write or read past the end of a block is an access violation, so a heap overflow fails the test without AddressSanitizer.
// Blocks freed after the PageHeap is gone are still recognised.
class PageHeap
{
public:
  PageHeap();
  ~PageHeap();
  PageHeap(const PageHeap&) = delete;
  PageHeap& operator=(const PageHeap&) = delete;
};

// While a HeapFill is alive the blocks given by operator new are filled with the byte, so a test can tell a read of memory that
// the code under test never wrote (new does not clear its blocks).
class HeapFill
{
public:
  explicit HeapFill(unsigned char value);
  ~HeapFill();
  HeapFill(const HeapFill&) = delete;
  HeapFill& operator=(const HeapFill&) = delete;
};
