// The sizes of the receive buffers of the UDP based protocols (UDP, RTP) of MPIPTVSource, in a header so that the tests can call
// them. The behaviour is that of the code before it was moved here; see SECURITY_AUDIT.md (RT-02).
#pragma once

struct UdpBufferSizes
{
  unsigned int defaultSize;      // receiveBuffer and the first size of the internal buffer
  unsigned int maxSize;          // the largest size the internal buffer may grow to
};

// The values come from the configuration (IptvBufferSize and the two multipliers).
inline UdpBufferSizes ComputeUdpBufferSizes(long iptvBufferSize, long defaultMultiplier, long maxMultiplier)
{
  UdpBufferSizes sizes = { 0, 0 };   // zero sizes: the callers refuse them ("not valid size of IPTV buffer")
  if (iptvBufferSize <= 0 || defaultMultiplier <= 0 || maxMultiplier <= 0) return sizes;
  const unsigned long long defaultSize = (unsigned long long)defaultMultiplier * iptvBufferSize;
  const unsigned long long maxSize = (unsigned long long)maxMultiplier * iptvBufferSize;
  if (defaultSize > 0xFFFFFFFFull || maxSize > 0xFFFFFFFFull) return sizes;
  sizes.defaultSize = (unsigned int)defaultSize;
  sizes.maxSize = (unsigned int)maxSize;
  return sizes;
}

// The length asked of recvfrom(): freeSpace is the free space of the internal buffer, which can have grown, receiveBufferSize is the
// size of the buffer the datagram is written to.
inline unsigned int UdpReceiveLength(unsigned int freeSpace, unsigned int receiveBufferSize)
{
  return (freeSpace < receiveBufferSize) ? freeSpace : receiveBufferSize;
}
