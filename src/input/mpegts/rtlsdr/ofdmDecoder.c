#include <string.h>
#include <math.h>

#include "dab.h"
#include "ofdmDecoder.h"

int16_t	get_snr(fftwf_complex* v);


void initOfdmDecoder(struct sdr_state_t *sdr) {
	sdr->current_snr = 0;
}

void processBlock_0(struct sdr_state_t *sdr, struct complex_t* v) {
	memcpy(sdr->fftBuffer, v, T_u * sizeof(fftwf_complex));
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
		sdr->fftBuffer, T_u * sizeof(struct complex_t));
}

int16_t	get_snr(fftwf_complex* v) {
	int16_t	i;
	float	noise = 0;
	float	signal = 0;
	int16_t	low = T_u / 2 - K / 2;
	int16_t	high = low + K;

	for (i = 10; i < low - 20; i++)
		noise += sdr_abs(v[(T_u / 2 + i) % T_u]);

	for (i = high + 20; i < T_u - 10; i++)
		noise += sdr_abs(v[(T_u / 2 + i) % T_u]);

	noise /= (low - 30 + T_u - high - 30);
	for (i = T_u / 2 - K / 4; i < T_u / 2 + K / 4; i++)
		signal += sdr_abs(v[(T_u / 2 + i) % T_u]);
	return get_db(signal / (K / 2)) - get_db(noise);
}
