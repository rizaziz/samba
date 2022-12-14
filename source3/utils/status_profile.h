/*
 * Samba Unix/Linux SMB client library
 * Dump profiles
 * Copyright (C) Volker Lendecke 2014
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __STATUS_PROFILE_H__
#define __STATUS_PROFILE_H__

#include "replace.h"
#include "status.h"

bool status_profile_dump(bool be_verbose,
			 struct traverse_state *state);
bool status_profile_rates(bool be_verbose);

#endif
