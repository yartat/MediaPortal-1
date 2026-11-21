// MPIPTVSource: the receive buffers of the UDP based protocols and the redirects of the HTTP protocol
// (SECURITY_AUDIT.md: RT-02, HT-01). The seams are the headers UdpReceive.h and HttpRedirect.h.
#include <gtest/gtest.h>
#include <cstdint>
#include "UdpReceive.h"
#include "HttpRedirect.h"

// ---------------------------------------------------------------------------
// RT-02
// ---------------------------------------------------------------------------

// The reference: the default configuration (IptvBufferSize 32 768, multipliers 8 and 64).
TEST(UdpReceive, DefaultConfigurationGivesTheDefaultSizes)
{
  const UdpBufferSizes sizes = ComputeUdpBufferSizes(32768, 8, 64);
  EXPECT_EQ(sizes.defaultSize, 262144u);
  EXPECT_EQ(sizes.maxSize, 2097152u);
}

// The reference: while the internal buffer has not grown, recvfrom gets what is free.
TEST(UdpReceive, ReceiveLengthIsTheFreeSpaceWhenItFitsTheReceiveBuffer)
{
  EXPECT_EQ(UdpReceiveLength(100, 4096), 100u);
  EXPECT_EQ(UdpReceiveLength(4096, 4096), 4096u);
}

// RT-02: the internal buffer grew to 1 MB (its free space is 1 MB), the receive buffer is 4 096 bytes (IptvBufferSize 4 096,
// multiplier 1): recvfrom may write a datagram of up to 65 507 bytes into it.
TEST(UdpReceive, ReceiveLengthNeverExceedsTheReceiveBuffer)
{
  EXPECT_LE(UdpReceiveLength(1u << 20, 4096), 4096u);
  EXPECT_LE(UdpReceiveLength(0xFFFFFFFFu, 1), 1u);
}

// RT-02: the products of the configuration are computed in 32 bits: 65 536 * 65 537 is 4 295 032 832, which wraps around to 65 536.
// A wrapped size is smaller than the receive buffer needs to be.
TEST(UdpReceive, ProductThatDoesNotFitIsNotWrappedAround)
{
  const UdpBufferSizes sizes = ComputeUdpBufferSizes(65536, 65537, 65537);
  const uint64_t exact = uint64_t(65536) * 65537;
  EXPECT_TRUE(sizes.defaultSize == 0 || sizes.defaultSize == exact)
    << "the size is " << sizes.defaultSize << " instead of " << exact;
}

// ---------------------------------------------------------------------------
// HT-01
// ---------------------------------------------------------------------------

// The reference: a few redirects (a login page, a load balancer) are followed.
TEST(HttpRedirect, FirstRedirectsAreFollowed)
{
  unsigned int count = 0;
  for (int i = 0; i < 5; i++)
    EXPECT_TRUE(HttpRedirectAllowed(count)) << "redirect " << i + 1;
}

// HT-01: a server that answers every request with a redirect (to itself too) keeps the client busy for ever.
TEST(HttpRedirect, RedirectsAreLimited)
{
  unsigned int count = 0;
  bool followed = true;
  int redirects = 0;
  while (followed && redirects < 1000)
  {
    followed = HttpRedirectAllowed(count);
    if (followed)
      redirects++;
  }
  EXPECT_LE(redirects, 10);
}
