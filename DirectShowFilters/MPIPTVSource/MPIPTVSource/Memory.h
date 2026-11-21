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

#ifndef __MEMORY_DEFINED
#define __MEMORY_DEFINED

#include "MPIPTVSourceExports.h"

#include <ObjBase.h>

// The size is count * size of the type; a product that does not fit, or a negative count, gives NULL (SECURITY_AUDIT.md: CD-10).
inline void *MpAllocArray(size_t count, size_t elementSize)
{
  return (elementSize != 0 && count > (size_t)-1 / elementSize) ? NULL : CoTaskMemAlloc(count * elementSize);
}

inline void *MpReallocArray(void *former, size_t count, size_t elementSize)
{
  return (elementSize != 0 && count > (size_t)-1 / elementSize) ? NULL : CoTaskMemRealloc(former, count * elementSize);
}

#define ALLOC_MEM(type, length)                               (type *)MpAllocArray((size_t)(length), sizeof(type))
#define REALLOC_MEM(formerVariable, type, length)             (type *)MpReallocArray(formerVariable, (size_t)(length), sizeof(type))
#define ALLOC_MEM_DEFINE(variable, type, length)              type *variable = ALLOC_MEM(type, length)
#define ALLOC_MEM_SET(variable, type, length, value)          ALLOC_MEM(type, length); \
                                                              if (variable != NULL) \
                                                              { \
                                                                memset(variable, value, (size_t)(length) * sizeof(type)); \
                                                              }
#define ALLOC_MEM_DEFINE_SET(variable, type, length, value)   type *variable = ALLOC_MEM_SET(variable, type, length, value)
#define FREE_MEM(variable)                                    if (variable != NULL) \
                                                              { \
                                                                CoTaskMemFree(variable); \
                                                                variable = NULL; \
                                                              }

#endif
