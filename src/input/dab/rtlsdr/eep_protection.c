/*
*    Copyright (C) 2013 .. 2017
*    Jan van Katwijk (J.vanKatwijk@gmail.com)
*    Lazy Chair Computing
*
*    This file is part of the DAB-library
*    DAB-library is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    DAB-library is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*       Adapted to tvheadend by Matthias Walliczek (matthias@walliczek.de)
*/

#include "tvheadend.h"
#include "protTables.h"
#include "protection.h"

protection_t* eep_protection_init (int16_t bitRate, int16_t protLevel) {
	protection_t* res = protection_init(bitRate);
	int16_t L1, L2;
	const int8_t  *PI1, *PI2;

	tvhdebug(LS_RTLSDR, "eep_protection_init with bitrate %d and protLevel %d", bitRate, protLevel);
	if ((protLevel & (1 << 2)) == 0) {	// set A profiles
	   switch (protLevel & 03) {
	      case 0:			// actually level 1
	         L1	= 6 * bitRate / 8 - 3;
	         L2	= 3;
	         PI1	= get_PCodes (24 - 1);
	         PI2	= get_PCodes (23 - 1);
	         break;

	      case 1:			// actually level 2
	         if (bitRate == 8) {
	            L1	= 5;
	            L2	= 1;
	            PI1	= get_PCodes (13 - 1);
	            PI2	= get_PCodes (12 - 1);
	         } else {
	            L1	= 2 * bitRate / 8 - 3;
	            L2	= 4 * bitRate / 8 + 3;
	            PI1	= get_PCodes (14 - 1);
	            PI2	= get_PCodes (13 - 1);
	         }
	         break;

	      case 2:			// actually level 3
	         L1	= 6 * bitRate / 8 - 3;
	         L2	= 3;
	         PI1	= get_PCodes (8 - 1);
	         PI2	= get_PCodes (7 - 1);
	         break;

	      case 3:			//actually level 4
	         L1	= 4 * bitRate / 8 - 3;
	         L2	= 2 * bitRate / 8 + 3;
	         PI1	= get_PCodes (3 - 1);
	         PI2	= get_PCodes (2 - 1);
	         break;
	   }
	} else {		// B series
	   switch (protLevel & 03) {
	      case 3:					// actually level 4
	         L1	= 24 * bitRate / 32 - 3;
	         L2	= 3;
	         PI1	= get_PCodes (2 - 1);
	         PI2	= get_PCodes (1 - 1);
	         break;

	      case 2:					// actually level 3
	         L1	= 24 * bitRate / 32 - 3;
	         L2	= 3;
	         PI1	= get_PCodes (4 - 1);
	         PI2	= get_PCodes (3 - 1);
	         break;

	      case 1:					// actually level 2
	         L1	= 24 * bitRate / 32 - 3;
	         L2	= 3;
	         PI1	= get_PCodes (6 - 1);
	         PI2	= get_PCodes (5 - 1);
	         break;

	      case 0:					// actually level 1
	         L1	= 24 * bitRate / 32 - 3;
	         L2	= 3;
	         PI1	= get_PCodes (10 - 1);
	         PI2	= get_PCodes (9 - 1);
	         break;
	   }
	}

    protection_createIndexTable(res, L1, PI1, L2, PI2, 0, NULL, 0, NULL);
	
	return res;
}
