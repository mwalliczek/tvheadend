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
