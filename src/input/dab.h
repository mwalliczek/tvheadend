/*
 *  Tvheadend - TS file input system
 *
 *  Copyright (C) 2019 Matthias Walliczek
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __TVH_DAB_H__
#define __TVH_DAB_H__

#ifndef __TVH_INPUT_H__
#error "Use header file input.h not input/dab.h"
#endif

/* **************************************************************************
 * Setup / Tear down
 * *************************************************************************/

void dab_init ( void );
void dab_done ( void );

#endif /* __TVH_DAB_H__ */
