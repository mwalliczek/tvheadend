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
*
*	Once the bits are "in", interpretation and manipulation
*	should reconstruct the data blocks.
*	Ofdm_decoder is called once every Ts samples, and
*	its invocation results in 2 * Tu bits
*
*	Adapted to tvheadend by Matthias Walliczek (matthias@walliczek.de)
*/
#include <string.h>
#include <math.h>

#include "dab.h"
#include "ofdmDecoder.h"
#include "ficHandler.h"

int16_t	get_snr(float _Complex* v);
void decodeFICblock(struct sdr_state_t *sdr, float _Complex* v, int32_t blkno);
void decodeMscblock(struct sdr_state_t *sdr, float _Complex* v, int32_t blkno);
void processBlock_0_int(struct sdr_state_t *sdr, float _Complex* v);

int16_t myMapper[T_u];

static void *run_thread_fn(void *arg);

void initOfdmDecoder(struct sdr_state_t *sdr) {
	int16_t tmp[T_u];

	sdr->current_snr = 0;

	sdr->ofdmDecoder.fftBuffer = fftwf_malloc(sizeof(fftwf_complex) * T_u);
	memset(sdr->ofdmDecoder.fftBuffer, 0, sizeof(fftwf_complex) * T_u);
	sdr->ofdmDecoder.plan = fftwf_plan_dft_1d(T_u, (float(*)[2]) sdr->ofdmDecoder.fftBuffer, (float(*)[2])sdr->ofdmDecoder.fftBuffer, FFTW_FORWARD, FFTW_ESTIMATE);

	int16_t	index = 0;
	int16_t	i;

	tmp[0] = 0;
	for (i = 1; i < T_u; i++)
		tmp[i] = (13 * tmp[i - 1] + 511) % T_u;
	for (i = 0; i < T_u; i++) {
		if (tmp[i] == T_u / 2)
			continue;
		if ((tmp[i] < 256) ||
			(tmp[i] > (256 + K)))
			continue;
		//	we now have a table with values from lwb .. upb
		//
		myMapper[index++] = tmp[i] - T_u / 2;
		//	we now have a table with values from lwb - T_u / 2 .. lwb + T_u / 2
	}
	tvh_pipe(0, &sdr->ofdmDecoder.pipe);
	tvhthread_create(&sdr->ofdmDecoder.thread, NULL,
		run_thread_fn, sdr, "rtlsdr-ofdm");
}

void destroyOfdmDecoder(struct sdr_state_t *sdr) {
	tvh_pipe_close(&sdr->ofdmDecoder.pipe);
	pthread_join(sdr->ofdmDecoder.thread, NULL);
	fftwf_destroy_plan(sdr->ofdmDecoder.plan);
	fftwf_free(sdr->ofdmDecoder.fftBuffer);
}

void processBlock_0(struct sdr_state_t *sdr, float _Complex* v) {
	int blkno = 0;
	write(sdr->ofdmDecoder.pipe.wr, &blkno, sizeof(blkno));
	write(sdr->ofdmDecoder.pipe.wr, v, sizeof(float _Complex) * T_u);
}

void processBlock_0_int(struct sdr_state_t *sdr, float _Complex* v) {
	memcpy(sdr->ofdmDecoder.fftBuffer, v, T_u * sizeof(float _Complex));
	fftwf_execute(sdr->ofdmDecoder.plan);
	/**
	*	The SNR is determined by looking at a segment of bins
	*	within the signal region and bits outside.
	*	It is just an indication
	*/
	sdr->current_snr = 0.7 * sdr->current_snr + 0.3 * get_snr(sdr->ofdmDecoder.fftBuffer);
	/**
	*	we are now in the frequency domain, and we keep the carriers
	*	as coming from the FFT as phase reference.
	*/
	memcpy(sdr->ofdmDecoder.phaseReference,
		sdr->ofdmDecoder.fftBuffer, T_u * sizeof(float _Complex));
}

int16_t	get_snr(float _Complex* v) {
	int16_t	i;
	float	noise = 0;
	float	signal = 0;
	int16_t	low = T_u / 2 - K / 2;
	int16_t	high = low + K;

	for (i = 10; i < low - 20; i++)
		noise += cabsf(v[(T_u / 2 + i) % T_u]);

	for (i = high + 20; i < T_u - 10; i++)
		noise += cabsf(v[(T_u / 2 + i) % T_u]);

	noise /= (low - 30 + T_u - high - 30);
	for (i = T_u / 2 - K / 4; i < T_u / 2 + K / 4; i++)
		signal += cabsf(v[(T_u / 2 + i) % T_u]);
	return get_db(signal / (K / 2)) - get_db(noise);
}

void decodeBlock(struct sdr_state_t *sdr, float _Complex* v, int32_t blkno) {
	write(sdr->ofdmDecoder.pipe.wr, &blkno, sizeof(blkno));
	write(sdr->ofdmDecoder.pipe.wr, v, sizeof(float _Complex) * T_u);
}

void decodeFICblock(struct sdr_state_t *sdr, float _Complex* v, int32_t blkno) {
	int i;
	float _Complex r1;
	//	struct complex_t conjVector[T_u];
	int16_t ibits[2 * K];

	memcpy(sdr->ofdmDecoder.fftBuffer, &v[T_g], T_u * sizeof(float _Complex));
	fftwf_execute(sdr->ofdmDecoder.plan);
	/**
	*	Note that from here on, we are only interested in the
	*	"carriers" useful carriers of the FFT output
	*/
	for (i = 0; i < K; i++) {
		int16_t	index = myMapper[i];
		if (index < 0)
			index += T_u;
		/**
		*	decoding is computing the phase difference between
		*	carriers with the same index in subsequent blocks.
		*	The carrier of a block is the reference for the carrier
		*	on the same position in the next block
		*/
		r1 = sdr->ofdmDecoder.fftBuffer[index] * conjf(sdr->ofdmDecoder.phaseReference[index]);
		//		conjVector[index] = r1;
		float ab1 = jan_abs(r1);
		ibits[i] = -crealf(r1) / ab1 * 127.0;
		ibits[K + i] = -cimagf(r1) / ab1 * 127.0;
	}
	memcpy(sdr->ofdmDecoder.phaseReference,
		sdr->ofdmDecoder.fftBuffer, T_u * sizeof(float _Complex));
	process_ficBlock(sdr, ibits, blkno);
}

void decodeMscblock(struct sdr_state_t *sdr, float _Complex* v, int32_t blkno) {
}

static void *run_thread_fn(void *arg) {
	struct sdr_state_t *sdr = arg;
	int blkno;
	float _Complex buffer[T_u];

	do {
		if (read(sdr->ofdmDecoder.pipe.rd, &blkno, sizeof(blkno)) <= 0) {
			break;
		}
		if (read(sdr->ofdmDecoder.pipe.rd, buffer, sizeof(float _Complex) * T_u) <= 0) {
			break;
		}
		tvhtrace(LS_RTLSDR, "run ofdm %d", blkno);
		if (blkno == 0) {
			processBlock_0_int(sdr, buffer);
		}
		else if (blkno < 4) {
			decodeFICblock(sdr, buffer, blkno);
		}
		else {
			decodeMscblock(sdr, buffer, blkno);
		}
	} while (1);

}