// shared/RandomName.h: names of the dummy files of FileReader (SECURITY_AUDIT.md: CD-11).
#include <gtest/gtest.h>
#include <cstdlib>
#include <set>
#include "RandomName.h"

TEST(RandomName, HasTheLengthAndOnlyLettersAndDigits)
{
  for (int length : {0, 1, 12, 100})
  {
    std::wstring name = RandomName(length);
    EXPECT_EQ(name.size(), (size_t)length);
    for (wchar_t c : name)
      EXPECT_TRUE(iswalnum(c) && c < 128) << (int)c;
  }
  EXPECT_TRUE(RandomName(-5).empty());
}

TEST(RandomName, CallsInTheSameMillisecondGiveDifferentNames)
{
  std::set<std::wstring> names;
  for (int i = 0; i < 1000; i++)
    names.insert(RandomName(12));
  EXPECT_EQ(names.size(), 1000u);   // the old generator: one name per millisecond
}

TEST(RandomName, DoesNotTouchTheSequenceOfRand)
{
  srand(12345);
  int expected[4];
  for (int& e : expected)
    e = rand();
  srand(12345);
  RandomName(12);
  for (int e : expected)
    EXPECT_EQ(rand(), e);
}
