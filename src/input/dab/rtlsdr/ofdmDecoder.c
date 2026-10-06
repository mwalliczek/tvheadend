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
#include "mscHandler.h"

int16_t	get_snr(const float _Complex* v);
void decodeFICblock(struct sdr_state_t *sdr, const float _Complex* v, int32_t blkno);
void decodeMscblock(struct sdr_state_t *sdr, const float _Complex* v, int32_t blkno);
void processBlock_0_int(struct sdr_state_t *sdr, const float _Complex* v);

static int16_t myMapper[T_u];

#ifndef DAB_SINGLE_THREAD
static void *run_thread_fn(void *arg);
#endif

void initConstOfdmDecoder(void) {
	int16_t tmp[T_u];
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
}

void initOfdmDecoder(struct sdr_state_t *sdr) {

	sdr->mmi->tii_stats.snr = 0;

	tvhdebug(LS_RTLSDR, "initOfdmDecoder");
	sdr->ofdmDecoder.fftBuffer = fftwf_malloc(sizeof(fftwf_complex) * T_u);
	memset(sdr->ofdmDecoder.fftBuffer, 0, sizeof(fftwf_complex) * T_u);
	sdr->ofdmDecoder.plan = fftwf_plan_dft_1d(T_u, (float(*)[2]) sdr->ofdmDecoder.fftBuffer, (float(*)[2])sdr->ofdmDecoder.fftBuffer, FFTW_FORWARD, FFTW_ESTIMATE);

#ifndef DAB_SINGLE_THREAD
	tvh_mutex_init(&sdr->ofdmDecoder.busyLock, NULL);
	tvh_cond_init(&sdr->ofdmDecoder.busyCond, 1);
	memset(sdr->ofdmDecoder.busy, 0, sizeof(sdr->ofdmDecoder.busy));
	tvh_pipe(0, &sdr->ofdmDecoder.pipe);
	tvh_thread_create(&sdr->ofdmDecoder.thread, NULL,
		run_thread_fn, sdr, "rtlsdr-ofdm");
#endif
}

void destroyOfdmDecoder(struct sdr_state_t *sdr) {
	tvhdebug(LS_RTLSDR, "destroyOfdmDecoder");
#ifndef DAB_SINGLE_THREAD
	tvh_pipe_close(&sdr->ofdmDecoder.pipe);
	pthread_join(sdr->ofdmDecoder.thread, NULL);
	tvh_cond_destroy(&sdr->ofdmDecoder.busyCond);
	tvh_mutex_destroy(&sdr->ofdmDecoder.busyLock);
#endif
	fftwf_destroy_plan(sdr->ofdmDecoder.plan);
	fftwf_free(sdr->ofdmDecoder.fftBuffer);
}

#ifndef DAB_SINGLE_THREAD
/* wait until the OFDM thread has finished with the previous use of the buffer */
static void acquireBuffer(struct sdr_state_t *sdr, int blkno) {
	tvh_mutex_lock(&sdr->ofdmDecoder.busyLock);
	while (sdr->ofdmDecoder.busy[blkno])
		tvh_cond_wait(&sdr->ofdmDecoder.busyCond, &sdr->ofdmDecoder.busyLock);
	sdr->ofdmDecoder.busy[blkno] = 1;
	tvh_mutex_unlock(&sdr->ofdmDecoder.busyLock);
}

static void releaseBuffer(struct sdr_state_t *sdr, int blkno) {
	tvh_mutex_lock(&sdr->ofdmDecoder.busyLock);
	sdr->ofdmDecoder.busy[blkno] = 0;
	tvh_cond_signal(&sdr->ofdmDecoder.busyCond, 0);
	tvh_mutex_unlock(&sdr->ofdmDecoder.busyLock);
}
#endif

void processBlock_0(struct sdr_state_t *sdr, const float _Complex* v) {
	int blkno = 0;
#ifndef DAB_SINGLE_THREAD
	acquireBuffer(sdr, blkno);
#endif
	memcpy(sdr->ofdmDecoder.buffer[blkno], v, sizeof(float _Complex) * T_u);
#ifdef DAB_SINGLE_THREAD
	processBlock_0_int(sdr, sdr->ofdmDecoder.buffer[blkno]);
#else
	if (write(sdr->ofdmDecoder.pipe.wr, &blkno, sizeof(blkno)) == -1) {
		tvherror(LS_RTLSDR, "write to pipe failed!");
	}
#endif
}

static void doFft(struct sdr_state_t *sdr, const float _Complex* v) {
	memcpy(sdr->ofdmDecoder.fftBuffer, v, T_u * sizeof(float _Complex));
	fftwf_execute(sdr->ofdmDecoder.plan);
}

void processBlock_0_int(struct sdr_state_t *sdr, const float _Complex* v) {
	doFft(sdr, v);

	/**
	*	The SNR is determined by looking at a segment of bins
	*	within the signal region and bits outside.
	*	It is just an indication
	*/
	sdr->mmi->tii_stats.snr =  (0.7 * sdr->mmi->tii_stats.snr + 0.3 * 1000.0 * get_snr(sdr->ofdmDecoder.fftBuffer));
	/**
	*	we are now in the frequency domain, and we keep the carriers
	*	as coming from the FFT as phase reference.
	*/
	memcpy(sdr->ofdmDecoder.phaseReference,
		sdr->ofdmDecoder.fftBuffer, T_u * sizeof(float _Complex));
}

int16_t	get_snr(const float _Complex* v) {
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

void decodeBlock(struct sdr_state_t *sdr, const float _Complex* v, int32_t blkno) {
#ifndef DAB_SINGLE_THREAD
	acquireBuffer(sdr, blkno);
#endif
	memcpy(sdr->ofdmDecoder.buffer[blkno], v, sizeof(float _Complex) * T_s);
#ifdef DAB_SINGLE_THREAD
	if (blkno < 4) {
		decodeFICblock(sdr, sdr->ofdmDecoder.buffer[blkno], blkno);
	}
	else 
	{
		decodeMscblock(sdr, sdr->ofdmDecoder.buffer[blkno], blkno);
	}	
#else
	if (write(sdr->ofdmDecoder.pipe.wr, &blkno, sizeof(blkno)) == -1) {
		tvherror(LS_RTLSDR, "write to pipe failed!");
	}
#endif
}

static void decodeBlockInt(struct sdr_state_t *sdr, const float _Complex* v, int16_t *ibits) {
	int i;
	float _Complex r1[K];
	float sum = 0, scale;
	
	doFft(sdr, &v[T_g]);
	
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
		r1[i] = sdr->ofdmDecoder.fftBuffer[index] * conjf(sdr->ofdmDecoder.phaseReference[index]);
		sum += jan_abs(r1[i]);
	}
	memcpy(sdr->ofdmDecoder.phaseReference,
		sdr->ofdmDecoder.fftBuffer, T_u * sizeof(float _Complex));

	/**
	*	Soft bits for the viterbi decoder (-127 .. 127). They keep the
	*	amplitude of the carrier relative to the average of the block:
	*	carriers in a fade of a multipath / SFN channel are mostly noise
	*	and must not get the same weight as strong ones (normalizing
	*	every carrier to its own amplitude cost 4 - 6 dB there).
	*	An average carrier (|re| + |im| = sqrt(2) |r| for QPSK) maps
	*	to +-90, like the phase only values before.
	*/
	if (sum <= 0)
		sum = 1;
	scale = 127.0f * (float)M_SQRT2 * K / sum;
	for (i = 0; i < K; i++) {
		float re = -crealf(r1[i]) * scale;
		float im = -cimagf(r1[i]) * scale;
		ibits[i] = re > 127 ? 127 : re < -127 ? -127 : re;
		ibits[K + i] = im > 127 ? 127 : im < -127 ? -127 : im;
	}
}

void decodeFICblock(struct sdr_state_t *sdr, const float _Complex* v, int32_t blkno) {
	int16_t ibits[2 * K];

	decodeBlockInt(sdr, v, ibits);
	
	process_ficBlock(sdr, ibits, blkno);
}

void decodeMscblock(struct sdr_state_t *sdr, const float _Complex* v, int32_t blkno) {
	int16_t ibits[2 * K];
	
	decodeBlockInt(sdr, v, ibits);

	process_mscBlock(sdr, ibits, blkno);
}

#ifndef DAB_SINGLE_THREAD
static void *run_thread_fn(void *arg) {
	struct sdr_state_t *sdr = arg;
	int blkno;

	do {
		if (read(sdr->ofdmDecoder.pipe.rd, &blkno, sizeof(blkno)) <= 0) {
			break;
		}
		if (blkno == 0) {
			processBlock_0_int(sdr, sdr->ofdmDecoder.buffer[0]);
		}
		else if (blkno < 4) {
			decodeFICblock(sdr, sdr->ofdmDecoder.buffer[blkno], blkno);
		}
		else {
			decodeMscblock(sdr, sdr->ofdmDecoder.buffer[blkno], blkno);
		}
		releaseBuffer(sdr, blkno);
	} while (1);

	return 0;
}
#endif
