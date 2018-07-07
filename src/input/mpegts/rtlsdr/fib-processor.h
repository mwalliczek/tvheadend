#
/*
 *    Copyright (C) 2013 .. 2017
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Programming
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
 *    You should have received a copy of the GNU General Public License
 *    along with DAB-library; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#
#ifndef	__FIB_PROCESSOR__
#define	__FIB_PROCESSOR__
#
//
#include	<stdint.h>
#include	<stdio.h>
#include "../dvb_charset.h"
#include	"dab_constants.h"

	struct dablabel {
	   char*	label;
	   int		hasName;
	};

	typedef struct dablabel	dabLabel;

	typedef struct subchannelmap channelMap;
//	from FIG1/2
	struct serviceid {
	   int		inUse;
	   uint32_t	serviceId;
	   dabLabel	serviceLabel;
	   int		hasPNum;
	   int		hasLanguage;
	   int16_t	language;
	   int16_t	programType;
	   uint16_t	pNum;
	};

	typedef	struct serviceid serviceId;
//      The service component describes the actual service
//      It really should be a union
        struct servicecomponents {
			int         inUse;          // just administration
           int8_t       TMid;           // the transport mode
           serviceId    *service;       // belongs to the service
           int16_t      componentNr;    // component

           int16_t      ASCTy;          // used for audio
           int16_t      PS_flag;        // use for both audio and packet
           int16_t      subchannelId;   // used in both audio and packet
           uint16_t     SCId;           // used in packet
           uint8_t      CAflag;         // used in packet (or not at all)
           int16_t      DSCTy;          // used in packet
	   uint8_t	DGflag;		// used for TDC
           int16_t      packetAddress;  // used in packet
	   int16_t	appType;	// used in packet
	   int		is_madePublic;
        };

        typedef struct servicecomponents serviceComponent;

	struct subchannelmap {
		int		inUse;
	   int32_t	SubChId;
	   int32_t	StartAddr;
	   int32_t	Length;
	   int		shortForm;
	   int32_t	protLevel;
	   int32_t	BitRate;
	   int16_t	language;
	   int16_t	FEC_scheme;
	};


	void	process_FIB		(struct sdr_state_t *sdr, uint8_t *, uint16_t);

	void	setupforNewFrame	(struct sdr_state_t *sdr);
	void	clearEnsemble		(struct sdr_state_t *sdr);


#endif

