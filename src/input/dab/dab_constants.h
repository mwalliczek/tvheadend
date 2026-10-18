/*
 *    Copyright (C) 2013 .. 2017
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Computing
 *
 *    Derived from dab-cmdline, https://github.com/JvanKatwijk/dab-cmdline
 *    (library/includes/dab-constants.h)
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    DAB constants
 *
 *    Adapted to tvheadend by Matthias Walliczek (matthias@walliczek.de)
 */

#ifndef _DAB_CONSTANTS_H
#define _DAB_CONSTANTS_H

#define	Hz(x)		(x)
#define	Khz(x)		(x * 1000)
#define	KHz(x)		(x * 1000)
#define	kHz(x)		(x * 1000)
#define	Mhz(x)		(Khz (x) * 1000)
#define	MHz(x)		(KHz (x) * 1000)
#define	mHz(x)		(kHz (x) * 1000)

#define		AUDIO_SERVICE	0101
#define		PACKET_SERVICE	0102
#define		UNKNOWN_SERVICE	0100

#define		INPUT_RATE	2048000

#define T_F 196608
#define T_null 2656
#define T_u 2048
#define T_s 2552
#define T_g 504
#define K 1536
#define L 76
#define carrierDiff 1000

#define		DIFF_LENGTH	42

#define numberofblocksperCIF 18

#define	CUSize	(4 * 16)

#endif
