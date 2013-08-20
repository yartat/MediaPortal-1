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

#pragma once

#ifndef __GOODBYE_RTCP_PACKET_DEFINED
#define __GOODBYE_RTCP_PACKET_DEFINED

/* goodbye RTCP packet

+-----------------------------+---+---+---+---+---+---+---+---+---+---+----+----+----+----+----+----+---------+
|         bit / byte          | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 - 31 |
+-----------------------------+---+---+---+---+---+---+---+---+---+---+----+----+----+----+----+----+---------+
|              0              |  VV   | P |        PV         |                 PT                  | length  |
+-----------------------------+---+---+---+---+---+---+---+---+---+---+----+----+----+----+----+----+---------+
| SSRC/CSRC identifiers, X = 1 .. PV                                                                          |
+-----------------------------+-------------------------------------------------------------------------------+
|       4 + (1 - 1) * 4       |                                   SSRC/CSRC_1                                 |
+-----------------------------+-------------------------------------------------------------------------------+
|                                                                                                             |
+-----------------------------+-------------------------------------------------------------------------------+
|       4 + (N - 1) * 4       |                                   SSRC/CSRC_N                                 |
+-----------------------------+-------------------------------------------------------------------------------+
| optional:                                                                                                   | 
+-----------------------------+-------------------------------+-----------------------------------------------+
|          4 + N * 4          |         reason length         |
+-----------------------------+-------------------------------+-----------------------------------------------+
|     4 + (N - 1) * 4 + 1     |                              reason for leaving                               |
+-----------------------------+-------------------------------+-----------------------------------------------+

VV, P, PV, PT and length : same as for RTCP packet (RtcpPacket.h)

PV: packet value, 5 bits, the number of SSRC/CSRC identifiers contained in this goodbye packet, value of zero is valid but useless
PT: packet type, 8 bits, constant value GOODBYE_RTCP_PACKET_TYPE

SSRC/CSRC: synchronization source identifier or contribution source, 32 bits

  Optionally, the BYE packet MAY include an 8-bit octet count followed by that many octets of text indicating
  the reason for leaving, e.g., "camera malfunction" or "RTP loop detected". The string has the same encoding
  as that described for SDES. If the string fills the packet to the next 32-bit boundary, the string is not null
  terminated. If not, the BYE packet MUST be padded with null octets to the next 32-bit boundary. This padding
  is separate from that indicated by the P bit in the RTCP header.

*/

#include "RtcpPacket.h"
#include "IdentifierCollection.h"

#define GOODBYE_RTCP_PACKET_HEADER_SIZE                                 0               // length of the goodbye RTCP header in bytes
#define GOODBYE_RTCP_PACKET_TYPE                                        0xCB            // goodbye RTCP packet type

#define FLAG_GOODBYE_RTCP_PACKET_REASON                                 0x00000002

class CGoodbyeRtcpPacket : public CRtcpPacket
{
public:
  // initializes a new instance of CGoodbyeRtcpPacket
  CGoodbyeRtcpPacket(void);
  virtual ~CGoodbyeRtcpPacket(void);

  /* get methods */

  // gets identifiers
  // @return : identifiers
  virtual CIdentifierCollection *GetIdentifiers(void);

  // gets reason of goodbye
  // @return : reason or NULL if not specified or error
  virtual const wchar_t *GetReason(void);

  /* set methods */

  /* other methods */

  // gets reason
  // @return : gets reason or NULL if not specified or error
  virtual bool HasReason(void);

  // sets current instance to default state
  virtual void Clear(void);

  // parses data in buffer
  // @param buffer : buffer with packet data for parsing
  // @param length : the length of data in buffer
  // @return : true if successfully parsed, false otherwise
  virtual bool Parse(const unsigned char *buffer, unsigned int length);

protected:

  // holds reason (if specified)
  wchar_t *reason;

  // holds SSRC/CSRC identifiers
  CIdentifierCollection *identifiers;
};

#endif