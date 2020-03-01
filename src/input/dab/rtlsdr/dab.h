#ifndef _DAB_H
#define _DAB_H

#include <stdint.h>
#include <fftw3.h>
#include <string.h>
#include <complex.h>

#include "input.h"

#include "sdr_fifo.h"
#include "../dab_constants.h"

#include "viterbi_768/viterbi-768.h"

#define DEFAULT_BUF_LENGTH (16 * 16384)

typedef struct sdr_dab_service_instance       sdr_dab_service_instance_t;

struct phase_reference_t {
	float _Complex *fftBuffer;
	fftwf_plan plan;
};

struct ofdm_decoder_t {
	float _Complex *fftBuffer;
	fftwf_plan plan;
	float _Complex phaseReference[T_u];
	pthread_t thread;
	th_pipe_t pipe;
	float _Complex buffer[L][T_s];
};

struct sdr_dab_service_instance
{
        dab_service_t   *dai_service;
        
        LIST_ENTRY(sdr_dab_service_instance) service_link;

        pthread_t thread;
        th_pipe_t pipe;
        

	int16_t		fragmentSize;
        int16_t*	interleaveData[16];
        uint8_t*	disperseVector;
	int16_t		nextIn;
	int16_t		nextOut;
	int16_t		*theData [20];

        subChannel* 	subChannel;
};

struct sdr_state_t {
	dab_ensemble_instance_t	*mmi;
	uint32_t frequency;
	CircularBuffer fifo;

	int		isSynced;

	int32_t		localPhase;
	float		sLevel;

	struct phase_reference_t phaseReference;

	struct ofdm_decoder_t ofdmDecoder;

	uint8_t		bitBuffer_out[768];
	int16_t		ofdm_input[2304];
	int16_t		index;
	int16_t		BitsperBlock;
	int16_t		ficno;

	int			fibCRCtotal;
	int			fibCRCsuccess;
	
	LIST_HEAD(,sdr_dab_service_instance) active_service_instance;
	
	int16_t		cifVector[55296];
	
	struct v vp;
};

float jan_abs(float _Complex z);
float get_db(float x);

static inline
int	check_CRC_bits(uint8_t *in, int32_t size) {
	static
		const uint8_t crcPolynome[] =
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 };	// MSB .. LSB
	int32_t	i, f;
	uint8_t	b[16];
	int16_t	Sum = 0;

	memset(b, 1, 16);

	for (i = size - 16; i < size; i++)
		in[i] ^= 1;

	for (i = 0; i < size; i++) {
		if ((b[0] ^ in[i]) == 1) {
			for (f = 0; f < 15; f++)
				b[f] = crcPolynome[f] ^ b[f + 1];
			b[15] = 1;
		}
		else {
			memmove(&b[0], &b[1], sizeof(uint8_t) * 15); // Shift
			b[15] = 0;
		}
	}

	for (i = 0; i < 16; i++)
		Sum += b[i];

	return Sum == 0;
}

sdr_dab_service_instance_t* sdr_dab_service_instance_create(dab_service_t* service);

void sdr_init(struct sdr_state_t *sdr);

void sdr_dab_service_instance_process_data(sdr_dab_service_instance_t *sds, int16_t *v, int16_t cnt);

#endif
