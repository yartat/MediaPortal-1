// The parts of CDeMultiplexer::FillAudio that read the start of an audio PES packet, in functions that do not need the filter
// (so the tests can call them). The behaviour is that of FillAudio before they were moved here; see SECURITY_AUDIT.md (BD-01, BD-02).
#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>

struct AudioPesStart
{
  bool   valid;                // the packet holds a PES header that fits in it
  UINT32 pesHeaderLen;         // PES header length including the 9 fixed bytes
  UINT32 pesLength;            // length of the PES payload still to come (PES_packet_length minus the rest of the header)
  UINT32 firstPacketLength;    // payload bytes in this TS packet after the PES header
  bool   ac3Substream;         // PES extension 2 says: a substream that gets discarded
};

// p points at the PES start code (00 00 01) in the payload of a TS packet; available is the number of bytes from p to the end
// of the packet.
// valid is false when the header does not fit in the bytes available or PES_packet_length is smaller than the rest of the header.
inline AudioPesStart ParseAudioPesStart(const BYTE* p, UINT32 available)
{
  AudioPesStart r = {};
  r.valid = false;
  r.ac3Substream = false;

  if (available < 9) return r;
  r.pesHeaderLen = p[8] + 9;
  if (r.pesHeaderLen > available) return r;
  const UINT32 packetLength = (p[4] << 8) + p[5];
  if (packetLength < r.pesHeaderLen - 6) return r;
  r.pesLength = packetLength - (r.pesHeaderLen - 6);
  r.valid = true;

  unsigned int flags = p[7];
  const BYTE* pos = p + 9;

  if ((flags & 0xc0) == 0x80)
    pos += 5; // PTS
  else if ((flags & 0xc0) == 0xc0)
    pos += 10; // PTS & DTS

  if (flags & 0x01) // PES extension
  {
    if (pos >= p + r.pesHeaderLen) { r.valid = false; return r; }
    unsigned int pes_ext = *pos++;
    // Skip PES private data, program packet sequence counter and P-STD buffer
    unsigned int skip = (pes_ext >> 4) & 0xb;
    skip += skip & 0x9;
    pos += skip;

    if ((pes_ext & 0x41) == 0x01 && (pos + 2) <= (p + r.pesHeaderLen))
    {
      // PES extension 2
      if ((pos[0] & 0x7f) > 0 && (pos[1] & 0x80) == 0)
      {
        if (pos[1] == 0x76)
          r.ac3Substream = true; // this stream will get discarded
      }
    }
  }

  r.firstPacketLength = available - r.pesHeaderLen;
  return r;
}

// The duration of one block of audio, in units of 100 ns.
inline LONGLONG AudioFrameDuration(const WAVEFORMATEX* wfe)
{
  if (wfe->nAvgBytesPerSec == 0) return 1;
  return static_cast<LONGLONG>(wfe->nBlockAlign) * 10000000 / wfe->nAvgBytesPerSec;
}

// The continuation of an audio PES packet over the following TS packets: more data are added while the size is not the expected one,
// and the PES is complete when it is (SECURITY_AUDIT.md: BD-03).
inline bool AudioPesNeedsMoreData(size_t currentSize, UINT32 expectedSize)
{
  return currentSize < expectedSize;
}

inline bool AudioPesComplete(size_t currentSize, UINT32 expectedSize)
{
  return currentSize >= expectedSize;
}

// BD-03: the video PES data collected while no NAL unit start code ends the data. A stream without start codes (damaged or hostile)
// grows the buffer with every packet: the limit is the one of TsReader (4 MB); more is thrown away.
const size_t VIDEO_PES_BUFFER_LIMIT = 4 * 1024 * 1024;

inline bool VideoPesBufferTooLarge(size_t currentSize)
{
  return currentSize > VIDEO_PES_BUFFER_LIMIT;
}
