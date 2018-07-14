/*
This file is part of rtl-dab
trl-dab is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Foobar is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with rtl-dab.  If not, see <http://www.gnu.org/licenses/>.


david may 2012
david.may.muc@googlemail.com

*/

#include <math.h>

#include "tvheadend.h"
#include "tvhpoll.h"
#include "rtlsdr_private.h"
#include "dab.h"
#include "dab_tables.h"
#include "phasereference.h"
#include "ofdmDecoder.h"
#include "ficHandler.h"

int readFromDevice(rtlsdr_frontend_t *lfe);

float _Complex oscillatorTable[INPUT_RATE];

int readFromDevice(rtlsdr_frontend_t *lfe) {
	struct sdr_state_t *sdr = &lfe->sdr;
	tvhpoll_event_t ev[2];
	tvhpoll_t *efd;
	char b;
	int nfds;

	/* Setup poll */
	efd = tvhpoll_create(2);
	memset(ev, 0, sizeof(ev));
	ev[0].events = TVHPOLL_IN;
	ev[0].fd = lfe->lfe_control_pipe.rd;
	ev[0].ptr = lfe;
	ev[1].events = TVHPOLL_IN;
	ev[1].fd = lfe->lfe_dvr_pipe.rd;
	ev[1].ptr = &lfe->lfe_dvr_pipe;
	tvhpoll_add(efd, ev, 2);

	while (tvheadend_is_running() && lfe->lfe_dvr_pipe.rd > 0) {
		nfds = tvhpoll_wait(efd, ev, 1, 150);
		if (nfds < 1) continue;
		if (ev[0].ptr == &lfe->lfe_dvr_pipe) {
			if (read(lfe->lfe_dvr_pipe.rd, &b, 1) > 0) {
				return 0;
			}
			continue;
		}
		if (ev[0].ptr != lfe) break;
		if (read(lfe->lfe_control_pipe.rd, &b, 1) > 0 && sdr->fifo.count > 0) {
			tvhtrace(LS_RTLSDR, "fifo count %u", sdr->fifo.count);
			return 1;
		}
	}
	return 0;
}

float jan_abs(float _Complex z) {
	float re = crealf(z);
	float im = cimagf(z);
	if (re < 0) re = -re;
	if (im < 0) im = -im;
	return re + im;
}

float get_db(float x) {
	return 20 * log10f((x + 1) / (float)(256));
}

static
float convTable[] = {
	-128 / 128.0 , -127 / 128.0 , -126 / 128.0 , -125 / 128.0 , -124 / 128.0 , -123 / 128.0 , -122 / 128.0 , -121 / 128.0 , -120 / 128.0 , -119 / 128.0 , -118 / 128.0 , -117 / 128.0 , -116 / 128.0 , -115 / 128.0 , -114 / 128.0 , -113 / 128.0
	, -112 / 128.0 , -111 / 128.0 , -110 / 128.0 , -109 / 128.0 , -108 / 128.0 , -107 / 128.0 , -106 / 128.0 , -105 / 128.0 , -104 / 128.0 , -103 / 128.0 , -102 / 128.0 , -101 / 128.0 , -100 / 128.0 , -99 / 128.0 , -98 / 128.0 , -97 / 128.0
	, -96 / 128.0 , -95 / 128.0 , -94 / 128.0 , -93 / 128.0 , -92 / 128.0 , -91 / 128.0 , -90 / 128.0 , -89 / 128.0 , -88 / 128.0 , -87 / 128.0 , -86 / 128.0 , -85 / 128.0 , -84 / 128.0 , -83 / 128.0 , -82 / 128.0 , -81 / 128.0
	, -80 / 128.0 , -79 / 128.0 , -78 / 128.0 , -77 / 128.0 , -76 / 128.0 , -75 / 128.0 , -74 / 128.0 , -73 / 128.0 , -72 / 128.0 , -71 / 128.0 , -70 / 128.0 , -69 / 128.0 , -68 / 128.0 , -67 / 128.0 , -66 / 128.0 , -65 / 128.0
	, -64 / 128.0 , -63 / 128.0 , -62 / 128.0 , -61 / 128.0 , -60 / 128.0 , -59 / 128.0 , -58 / 128.0 , -57 / 128.0 , -56 / 128.0 , -55 / 128.0 , -54 / 128.0 , -53 / 128.0 , -52 / 128.0 , -51 / 128.0 , -50 / 128.0 , -49 / 128.0
	, -48 / 128.0 , -47 / 128.0 , -46 / 128.0 , -45 / 128.0 , -44 / 128.0 , -43 / 128.0 , -42 / 128.0 , -41 / 128.0 , -40 / 128.0 , -39 / 128.0 , -38 / 128.0 , -37 / 128.0 , -36 / 128.0 , -35 / 128.0 , -34 / 128.0 , -33 / 128.0
	, -32 / 128.0 , -31 / 128.0 , -30 / 128.0 , -29 / 128.0 , -28 / 128.0 , -27 / 128.0 , -26 / 128.0 , -25 / 128.0 , -24 / 128.0 , -23 / 128.0 , -22 / 128.0 , -21 / 128.0 , -20 / 128.0 , -19 / 128.0 , -18 / 128.0 , -17 / 128.0
	, -16 / 128.0 , -15 / 128.0 , -14 / 128.0 , -13 / 128.0 , -12 / 128.0 , -11 / 128.0 , -10 / 128.0 , -9 / 128.0 , -8 / 128.0 , -7 / 128.0 , -6 / 128.0 , -5 / 128.0 , -4 / 128.0 , -3 / 128.0 , -2 / 128.0 , -1 / 128.0
	, 0 / 128.0 , 1 / 128.0 , 2 / 128.0 , 3 / 128.0 , 4 / 128.0 , 5 / 128.0 , 6 / 128.0 , 7 / 128.0 , 8 / 128.0 , 9 / 128.0 , 10 / 128.0 , 11 / 128.0 , 12 / 128.0 , 13 / 128.0 , 14 / 128.0 , 15 / 128.0
	, 16 / 128.0 , 17 / 128.0 , 18 / 128.0 , 19 / 128.0 , 20 / 128.0 , 21 / 128.0 , 22 / 128.0 , 23 / 128.0 , 24 / 128.0 , 25 / 128.0 , 26 / 128.0 , 27 / 128.0 , 28 / 128.0 , 29 / 128.0 , 30 / 128.0 , 31 / 128.0
	, 32 / 128.0 , 33 / 128.0 , 34 / 128.0 , 35 / 128.0 , 36 / 128.0 , 37 / 128.0 , 38 / 128.0 , 39 / 128.0 , 40 / 128.0 , 41 / 128.0 , 42 / 128.0 , 43 / 128.0 , 44 / 128.0 , 45 / 128.0 , 46 / 128.0 , 47 / 128.0
	, 48 / 128.0 , 49 / 128.0 , 50 / 128.0 , 51 / 128.0 , 52 / 128.0 , 53 / 128.0 , 54 / 128.0 , 55 / 128.0 , 56 / 128.0 , 57 / 128.0 , 58 / 128.0 , 59 / 128.0 , 60 / 128.0 , 61 / 128.0 , 62 / 128.0 , 63 / 128.0
	, 64 / 128.0 , 65 / 128.0 , 66 / 128.0 , 67 / 128.0 , 68 / 128.0 , 69 / 128.0 , 70 / 128.0 , 71 / 128.0 , 72 / 128.0 , 73 / 128.0 , 74 / 128.0 , 75 / 128.0 , 76 / 128.0 , 77 / 128.0 , 78 / 128.0 , 79 / 128.0
	, 80 / 128.0 , 81 / 128.0 , 82 / 128.0 , 83 / 128.0 , 84 / 128.0 , 85 / 128.0 , 86 / 128.0 , 87 / 128.0 , 88 / 128.0 , 89 / 128.0 , 90 / 128.0 , 91 / 128.0 , 92 / 128.0 , 93 / 128.0 , 94 / 128.0 , 95 / 128.0
	, 96 / 128.0 , 97 / 128.0 , 98 / 128.0 , 99 / 128.0 , 100 / 128.0 , 101 / 128.0 , 102 / 128.0 , 103 / 128.0 , 104 / 128.0 , 105 / 128.0 , 106 / 128.0 , 107 / 128.0 , 108 / 128.0 , 109 / 128.0 , 110 / 128.0 , 111 / 128.0
	, 112 / 128.0 , 113 / 128.0 , 114 / 128.0 , 115 / 128.0 , 116 / 128.0 , 117 / 128.0 , 118 / 128.0 , 119 / 128.0 , 120 / 128.0 , 121 / 128.0 , 122 / 128.0 , 123 / 128.0 , 124 / 128.0 , 125 / 128.0 , 126 / 128.0 , 127 / 128.0 };

uint32_t getSamples(rtlsdr_frontend_t *lfe, float _Complex *v, uint32_t size, int32_t freqOffset) {
	struct sdr_state_t *sdr = &lfe->sdr;
	uint32_t i;
	uint8_t *buffer;
	for (i = 0; i < size; i++) {
		if (sdr->fifo.count < 2) {
			if (!readFromDevice(lfe)) {
				return i;
			}
		}
		buffer = cbReadDouble(&(sdr->fifo));
		sdr->localPhase -= freqOffset;
		sdr->localPhase = (sdr->localPhase + INPUT_RATE) % INPUT_RATE;

		v[i] = convTable[buffer[0]] + convTable[buffer[1]] * I;
		v[i] *= oscillatorTable[sdr->localPhase];
		
		sdr->sLevel = 0.00001 * jan_abs(v[i]) + (1 - 0.00001) * sdr->sLevel;
	}
	return size;
}

uint32_t getSample(rtlsdr_frontend_t *lfe, float _Complex *v, float *abs, int32_t freqOffset) {
	struct sdr_state_t *sdr = &lfe->sdr;
	uint8_t *buffer;
	if (sdr->fifo.count < 2) {
		if (!readFromDevice(lfe)) {
			return 0;
		}
	}
	buffer = cbReadDouble(&(sdr->fifo));

	sdr->localPhase -= freqOffset;
	sdr->localPhase = (sdr->localPhase + INPUT_RATE) % INPUT_RATE;

	*v = convTable[buffer[0]] + convTable[buffer[1]] * I;
	*v *= oscillatorTable[sdr->localPhase];

	*abs = jan_abs(*v);
	sdr->sLevel = 0.00001 * *abs + (1 - 0.00001) * sdr->sLevel;
	return 1;
}

void sdr_init(struct sdr_state_t *sdr)
{
	int i;

  // circular buffer init
  cbInit(&(sdr->fifo),(196608*2*4)); // 4 frames

  initPhaseReference(sdr);
  initOfdmDecoder(sdr);
  initFicHandler(sdr);

  for (i = 0; i < INPUT_RATE; i++) {
	  oscillatorTable[i] = cosf(2.0 * M_PI * i / INPUT_RATE) + sinf(2.0 * M_PI * i / INPUT_RATE) * I;
  }
}
