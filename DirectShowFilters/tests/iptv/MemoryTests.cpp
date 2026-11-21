// MPIPTVSource, Memory.h: the allocation macros (SECURITY_AUDIT.md: CD-10).
// The old ALLOC_MEM(type, length) was CoTaskMemAlloc(length * sizeof(type)): with an expression as length (n + 1) the size was
// n + sizeof(type), and a product that does not fit in size_t wrapped around.
#include <gtest/gtest.h>
#include <windows.h>
#include <string.h>
#include "Memory.h"

TEST(AllocMem, AnExpressionAsLengthIsMultipliedAsAWhole)
{
  size_t n = 7;
  ALLOC_MEM_DEFINE_SET(p, unsigned int, n + 1, 0xFF);
  ASSERT_NE(p, nullptr);
  for (size_t i = 0; i < n + 1; i++)
    EXPECT_EQ(p[i], 0xFFFFFFFFu);   // 8 elements are writable; the old macro made room for 7 + 4 bytes
  FREE_MEM(p);
}

TEST(AllocMem, AProductThatDoesNotFitGivesNull)
{
  size_t huge = (size_t)-1 / sizeof(unsigned int) + 1;
  ALLOC_MEM_DEFINE(p, unsigned int, huge);
  EXPECT_EQ(p, nullptr);
  ALLOC_MEM_DEFINE_SET(q, unsigned int, huge, 0);
  EXPECT_EQ(q, nullptr);
}

TEST(AllocMem, ANegativeLengthGivesNull)
{
  int negative = -1;
  ALLOC_MEM_DEFINE(p, wchar_t, negative);
  EXPECT_EQ(p, nullptr);
}

TEST(AllocMem, ReallocChecksToo)
{
  unsigned int* p = ALLOC_MEM(unsigned int, 4);
  ASSERT_NE(p, nullptr);
  size_t n = 3;
  unsigned int* bigger = REALLOC_MEM(p, unsigned int, n + 1);
  ASSERT_NE(bigger, nullptr);
  bigger[3] = 1;
  size_t huge = (size_t)-1 / sizeof(unsigned int) + 1;
  EXPECT_EQ(REALLOC_MEM(bigger, unsigned int, huge), nullptr);   // the old block stays valid
  CoTaskMemFree(bigger);
}
