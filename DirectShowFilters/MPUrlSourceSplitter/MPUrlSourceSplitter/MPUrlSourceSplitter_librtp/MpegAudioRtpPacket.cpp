/*
    Copyright (C) 2007-2010 Team MediaPortal
    http://www.team-mediaportal.com

    This file is part of MediaPortal 2

    MediaPortal 2 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MediaPortal 2 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with MediaPortal 2.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "StdAfx.h"

#include "MpegAudioRtpPacket.h"
#include "BufferHelper.h"

#include <stdint.h>

CMpegAudioRtpPacket::CMpegAudioRtpPacket(void)
  : CRtpPacket()
{
  this->fragOffset = UINT_MAX;
}

CMpegAudioRtpPacket::~CMpegAudioRtpPacket(void)
{
}

/* get methods */

/* set methods */

/* other methods */

const unsigned char *CMpegAudioRtpPacket::GetPayload(void)
{
  return (this->payload + MPEG_AUDIO_PAYLOAD_HEADER_LENGTH);
}

unsigned int CMpegAudioRtpPacket::GetPayloadLength(void)
{
  return (this->payloadLength - MPEG_AUDIO_PAYLOAD_HEADER_LENGTH);
}

void CMpegAudioRtpPacket::Clear(void)
{
  __super::Clear();

  this->fragOffset = UINT_MAX;
}

bool CMpegAudioRtpPacket::Parse(const unsigned char *buffer, unsigned int length)
{
  bool result = __super::Parse(buffer, length);

  if (result)
  {
    // payload type should be MPEG_AUDIO_PAYLOAD_HEADER_LENGTH
    // maybe later will be implemented collection of accepted payload types (if necessary)

    result &= (this->payloadType == MPEG_AUDIO_PAYLOAD_TYPE_DEFAULT);
    result &= (this->payloadLength >= MPEG_AUDIO_PAYLOAD_HEADER_LENGTH);

    if (result)
    {
      // parse first MPEG_AUDIO_PAYLOAD_HEADER_LENGTH bytes

      unsigned int position = 0;
      RBE32INC_DEFINE(this->payload, position, temp, unsigned int);

      this->fragOffset = temp & 0x0000FFFF;
    }
  }

  return result;
}