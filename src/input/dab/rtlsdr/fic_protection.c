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

protection_t* fic_protection_init (void) {
        protection_t* res = protection_init(32, 1);
        int16_t i, k;
        int16_t local = 0;
        
        /**
        *       a block of 2304 bits is considered to be a codeword
        *       In the first step we have 21 blocks with puncturing according to PI_16
        *       each 128 bit block contains 4 subblocks of 32 bits
        *       on which the given puncturing is applied
        */
        memset(res->indexTable, 0, (3072 + 24) * sizeof(uint8_t));

        for (i = 0; i < 21; i++) {
                for (k = 0; k < 32 * 4; k++) {
                        if (get_PCodes(16 - 1)[k % 32] == 1)
                                res->indexTable[local] = 1;
                        local++;
                }
        }
        /**
        *       In the second step
        *       we have 3 blocks with puncturing according to PI_15
        *       each 128 bit block contains 4 subblocks of 32 bits
        *       on which the given puncturing is applied
        */
        for (i = 0; i < 3; i++) {
                for (k = 0; k < 32 * 4; k++) {
                        if (get_PCodes(15 - 1)[k % 32] == 1)
                                res->indexTable[local] = 1;
                        local++;
                }
        }

        /**
        *       we have a final block of 24 bits  with puncturing according to PI_X
        *       This block constitues the 6 * 4 bits of the register itself.
        */
        for (k = 0; k < 24; k++) {
                if (get_PCodes(8 - 1)[k] == 1)
                        res->indexTable[local] = 1;
                local++;
        }
        
        return res;
}
