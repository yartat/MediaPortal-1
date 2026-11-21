// GetChunkData of the HTTP protocol of MPIPTVSource (chunked transfer encoding), in a header so that the tests can call it.
// The behaviour is that of the function before it was moved here; the allocator of the chunk data is a parameter (by default
// CoTaskMemAlloc, as before) so that a test can see the size that is asked for without allocating it. See SECURITY_AUDIT.md (CD-01).
#pragma once

#include <windows.h>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include "Memory.h"
#include "LinearBuffer.h"

// the largest chunk a server may send
const unsigned int HTTP_MAX_CHUNK_SIZE = 16u << 20;

typedef void *(*ChunkAllocator)(size_t size);

inline void *DefaultChunkAllocator(size_t size)
{
  return CoTaskMemAlloc(size);
}

// gets chunk data from buffer
// data are returned to data variable
// length of data returned to length variable
// the whole chunk length returned to chunkLength variable
// method returns TRUE if chunk correctly get, FALSE otherwise
// 
// necessary space needed for data is created in this method
// this space after using have to be removed
inline bool GetChunkData(LinearBuffer *buffer, char **data, unsigned int *length, unsigned int *chunkLength, ChunkAllocator allocate = DefaultChunkAllocator)
{
  bool result = false;
  unsigned int firstEndLinePosition = buffer->GetFirstPosition(0, '\n');
  if (firstEndLinePosition != UINT_MAX)
  {
    // there is some chance for chunk data header
    unsigned int chunkHeaderDataLength = firstEndLinePosition + 2;
    // chunk hedaer data with terminating null character
    ALLOC_MEM_DEFINE_SET(chunkHeaderData, char, chunkHeaderDataLength, 0);

    if (chunkHeaderData != NULL)
    {
      // copy header data from buffer
      buffer->CopyFromBuffer(chunkHeaderData, chunkHeaderDataLength - 1, 0, 0);
      char *chunkHeaderEndPart = NULL;

      // unsigned 64-bit, no sign: a negative size must not become a huge unsigned one
      const char *sizeText = chunkHeaderData;
      while ((*sizeText == ' ') || (*sizeText == '	')) sizeText++;
      unsigned long long size = 0;
      errno = 0;
      if (*sizeText != '-')
      {
        size = strtoull(sizeText, &chunkHeaderEndPart, 16);
      }
      if ((*sizeText == '-') || (errno != 0) || (size > HTTP_MAX_CHUNK_SIZE))
      {
        // not a size we accept: nothing is allocated, the chunk is not taken
        *length = 0;
        FREE_MEM(chunkHeaderData);
        return false;
      }
      *length = (unsigned int)size;

      //if ((*length > 0) && (strlen(chunkHeaderEndPart) == 2))
      if (*length > 0)
      {
        // there valid length of chunk data and header is ended correctly with CRLF
        // the last characters are always CRLF

        // (chunkHeaderDataLength - 1) is the length of chunk data with terminating CRLF characters
        // the chunk data must be ended with CRLF
        // this size of chunk data is lenght + 2 (CRLF)
        // there is no need to termination NULL character, because in data can be NULL characters
        unsigned int chunkDataLength = (*length) + 2;

        // now is known whole chunk length
        *chunkLength = chunkHeaderDataLength - 1 + chunkDataLength;

        if (buffer->GetBufferOccupiedSpace() >= (*chunkLength))
        {
          // there is enough data to process whole chunk
          // allocate memory for chunk data
          *data = (char *)allocate((*length) * sizeof(char));
          if ((*data) != NULL)
          {
            buffer->CopyFromBuffer((*data), (*length), 0, chunkHeaderDataLength - 1);
            result = TRUE;
          }
        }
      }
      else if (*length == 0)
      {
        // there is valid length
        // zero length is signal for end of transmission
        result = TRUE;
      }
    }

    FREE_MEM(chunkHeaderData);
  }

  return result;
}
