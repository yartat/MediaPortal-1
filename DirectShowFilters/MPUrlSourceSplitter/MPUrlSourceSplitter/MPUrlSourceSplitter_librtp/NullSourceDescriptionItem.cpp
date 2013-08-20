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

#include "NullSourceDescriptionItem.h"
#include "BufferHelper.h"

#include <stdint.h>

CNullSourceDescriptionItem::CNullSourceDescriptionItem(void)
  : CSourceDescriptionItem()
{
}

CNullSourceDescriptionItem::~CNullSourceDescriptionItem(void)
{
}

/* get methods */

/* set methods */

/* other methods */

void CNullSourceDescriptionItem::Clear(void)
{
  __super::Clear();
}

bool CNullSourceDescriptionItem::Parse(const unsigned char *buffer, unsigned int length)
{
  // NULL source description item doesn't have length
  // it is only one byte length and it must be NULL_SOURCE_DESCRIPTION_ITEM_TYPE

  bool result = ((buffer != NULL) && (length >= NULL_SOURCE_DESCRIPTION_ITEM_HEADER));

  if (result)
  {
    this->Clear();

    unsigned int position = 0;

    RBE8INC(buffer, position, this->type);
    result &= (this->type == NULL_SOURCE_DESCRIPTION_ITEM_TYPE);
    this->size  = NULL_SOURCE_DESCRIPTION_ITEM_HEADER;
  }

  if (!result)
  {
    this->Clear();
  }

  return result;
}
