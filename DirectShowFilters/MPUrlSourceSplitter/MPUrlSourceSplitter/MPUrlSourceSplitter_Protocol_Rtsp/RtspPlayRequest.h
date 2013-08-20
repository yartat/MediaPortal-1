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

#ifndef __RTSP_PLAY_REQUEST_DEFINED
#define __RTSP_PLAY_REQUEST_DEFINED

#include "RtspRequest.h"

#define RTSP_PLAY_METHOD                                              L"PLAY"

class CRtspPlayRequest : public CRtspRequest
{
public:
  CRtspPlayRequest(void);
  virtual ~CRtspPlayRequest(void);

  /* get methods */

  // gets RTSP request method
  // @return : RTSP request method
  virtual const wchar_t *GetMethod(void);

  /* set methods */

  // sets start time in seconds for RTSP PLAY request
  // @param startTime : the start time in seconds to set
  virtual bool SetStartTime(unsigned int startTime);

  // sets end time in seconds for RTSP PLAY request
  // @param endTime : the end time in seconds to set
  virtual bool SetEndTime(unsigned int endTime);

  /* other methods */

  // deep clones of current instance
  // @return : deep clone of current instance or NULL if error
  virtual CRtspPlayRequest *Clone(void);

protected:

  CRtspPlayRequest(bool createDefaultHeaders);

  // deeply clones current instance to cloned RTSP request
  // @param  clonedRequest : cloned RTSP request to hold clone of current instance
  // @return : true if successful, false otherwise
  virtual bool CloneInternal(CRtspRequest *clonedRequest);

  // returns new RTSP request object to be used in cloning
  // @return : RTSP request object or NULL if error
  virtual CRtspRequest *GetNewRequest(void);
};

#endif