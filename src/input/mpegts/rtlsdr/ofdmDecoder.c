#include <string.h>
#include <math.h>

#include "dab.h"
#include "ofdmDecoder.h"
#include "ficHandler.h"

int16_t	get_snr(float _Complex* v);
void decodeFICblock(struct sdr_state_t *sdr, float _Complex* v, int32_t blkno);
void decodeMscblock(struct sdr_state_t *sdr, float _Complex* v, int32_t blkno);

int16_t myMapper[T_u];

void initOfdmDecoder(struct sdr_state_t *sdr) {
	int16_t tmp[T_u];
	
	sdr->current_snr = 0;

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

void processBlock_0(struct sdr_state_t *sdr, float _Complex* v) {
	memcpy(sdr->fftBuffer, v, T_u * sizeof(float _Complex));
	fftwf_execute(sdr->plan);
	/**
	*	The SNR is determined by looking at a segment of bins
	*	within the signal region and bits outside.
	*	It is just an indication
	*/
	sdr->current_snr = 0.7 * sdr->current_snr + 0.3 * get_snr(sdr->fftBuffer);
	/**
	*	we are now in the frequency domain, and we keep the carriers
	*	as coming from the FFT as phase reference.
	*/
	memcpy(sdr->ofdmPhaseReference,
		sdr->fftBuffer, T_u * sizeof(float _Complex));
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
	if (blkno < 4)
		decodeFICblock(sdr, v, blkno);
	else
		decodeMscblock(sdr, v, blkno);
}

void decodeFICblock(struct sdr_state_t *sdr, float _Complex* v, int32_t blkno) {
	int i;
	float _Complex r1;
//	struct complex_t conjVector[T_u];
	int16_t ibits[2 * K];

	memcpy(sdr->fftBuffer, &v[T_g], T_u * sizeof(float _Complex));
	fftwf_execute(sdr->plan);
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
		r1 = sdr->fftBuffer[index] * conjf(sdr->ofdmPhaseReference[index]);
//		conjVector[index] = r1;
		float ab1 = jan_abs(r1);
		ibits[i] = -crealf(r1) / ab1 * 127.0;
		ibits[K + i] = -cimagf(r1) / ab1 * 127.0;
	}
	memcpy(sdr->ofdmPhaseReference,
		sdr->fftBuffer, T_u * sizeof(float _Complex));
	process_ficBlock(sdr, ibits, blkno);
}

void decodeMscblock(struct sdr_state_t *sdr, float _Complex* v, int32_t blkno) {
}
