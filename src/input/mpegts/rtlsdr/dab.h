#ifndef _DAB_H
#define _DAB_H

#include <stdint.h>
#include <fftw3.h>
#include <string.h>
#include <complex.h>

#include "sdr_fifo.h"
#include "dab_constants.h"

/* A demapped transmission frame represents a transmission frame in
   the final state before the FIC-specific and MSC-specific decoding
   stages.

   This format is used as the output of the hardware-specific drivers
   and the input to the processing stages which are common across all
   supported devices.
 */

/* The FIBs for one transmission frame, and the results of CRC checking */
struct tf_fibs_t {
  uint8_t ok_count;
  uint8_t FIB[12][32];    /* The actual FIB data, including CRCs */
  uint8_t FIB_CRC_OK[12]; /* 1 = CRC OK, 0 = CRC Error */ 
};

struct demapped_transmission_frame_t {
  uint8_t has_fic;        /* Is there any FIC information at all? (The Wavefinder drops the FIC symbols in every 5th transmission frame )*/
  uint8_t fic_symbols_demapped[3][3072];
  struct tf_fibs_t fibs;  /* The decoded and CRC-checked FIBs */
  uint8_t msc_filter[72];   /* Indicates which symbols are present in msc_symbols_demapped */
  uint8_t msc_symbols_demapped[72][3072];
};

struct subchannel_info_t {
  int id;  /* Or -1 if not active */
  int eepprot;
  int slForm;
  int uep_index;
  int eep_option;
  int start_cu;
  int size;
  int bitrate;
  int eep_protlev;
  int protlev;
  int ASCTy;
};

/* The information from the FIBs required to construct the ETI stream */
struct tf_info_t {
  uint16_t EId;           /* Ensemble ID */
  uint8_t CIFCount_hi;    /* 5 bits - 0 to 31 */
  uint8_t CIFCount_lo;    /* 8 bits - 0 to 249 */

  /* Information on all subchannels defined by the FIBs in this
     tranmission frame.  Note that this may not be all the active
     sub-channels in the ensemble, so we need to merge the data from
     multiple transmission frames.
  */
  struct subchannel_info_t subchans[64];
};

struct ens_info_t {
  uint16_t EId;           /* Ensemble ID */
  uint8_t CIFCount_hi;    /* Our own CIF Count */
  uint8_t CIFCount_lo;
  struct subchannel_info_t subchans[64];
};

#define DEFAULT_BUF_LENGTH (16 * 16384)
#define GAIN_SETTLE_TIME 0

struct sdr_state_t {
	uint32_t frequency;
	CircularBuffer fifo;

	int		isSynced;

	int32_t		localPhase;
	float		sLevel;

	float _Complex *fftBuffer;
	fftwf_plan plan;

	float		current_snr;
	float _Complex ofdmPhaseReference[T_u];

	uint8_t		bitBuffer_out[768];
	int16_t		ofdm_input[2304];
	int16_t		index;
	int16_t		BitsperBlock;
	int16_t		ficno;

	int			fibProcessorIsSynced;
};

struct dab_state_t
{
  struct sdr_state_t device_state;
  struct demapped_transmission_frame_t tfs[5]; /* We need buffers for 5 tranmission frames - the four previous, plus the new */
  struct tf_info_t tf_info;
  struct ens_info_t ens_info;
  void* v; /* Viterbi decoder instance */

  unsigned char* cifs_msc[16];  /* Each CIF consists of 3072*18 bits */
  unsigned char* cifs_fibs[16];  /* Each CIF consists of 3072*18 bits */
  int ncifs;  /* Number of CIFs in buffer - we need 16 to start outputting them */
  int tfidx;  /* Next tf buffer to read to. */
  int locked;
  int ens_info_shown;
  int okcount;

  /* Callback function to process a decoded ETI frame */
  void (* eti_callback)(uint8_t *eti);
};

float jan_abs(float _Complex z);
float get_db(float x);

//	generic, up to 16 bits
static inline
uint16_t	getBits(uint8_t *d, int32_t offset, int16_t size) {
	int16_t	i;
	uint16_t	res = 0;

	for (i = 0; i < size; i++) {
		res <<= 1;
		res |= (d[offset + i]) & 01;
	}
	return res;
}

static inline
uint16_t	getBits_1(uint8_t *d, int32_t offset) {
	return (d[offset] & 0x01);
}

static inline
uint16_t	getBits_2(uint8_t *d, int32_t offset) {
	uint16_t	res = d[offset];
	res <<= 1;
	res |= (d[offset + 1] & 01);
	return res;
}

static inline
uint16_t	getBits_3(uint8_t *d, int32_t offset) {
	uint16_t	res = d[offset];
	res <<= 1;
	res |= d[offset + 1];
	res <<= 1;
	res |= d[offset + 2];
	return res;
}

static inline
uint16_t	getBits_4(uint8_t *d, int32_t offset) {
	uint16_t	res = d[offset];
	res <<= 1;
	res |= d[offset + 1];
	res <<= 1;
	res |= d[offset + 2];
	res <<= 1;
	res |= d[offset + 3];
	return res;
}

static inline
uint16_t	getBits_5(uint8_t *d, int32_t offset) {
	uint16_t	res = d[offset];
	res <<= 1;
	res |= d[offset + 1];
	res <<= 1;
	res |= d[offset + 2];
	res <<= 1;
	res |= d[offset + 3];
	res <<= 1;
	res |= d[offset + 4];
	return res;
}

static inline
uint16_t	getBits_6(uint8_t *d, int32_t offset) {
	uint16_t	res = d[offset];
	res <<= 1;
	res |= d[offset + 1];
	res <<= 1;
	res |= d[offset + 2];
	res <<= 1;
	res |= d[offset + 3];
	res <<= 1;
	res |= d[offset + 4];
	res <<= 1;
	res |= d[offset + 5];
	return res;
}

static inline
uint16_t	getBits_7(uint8_t *d, int32_t offset) {
	uint16_t	res = d[offset];
	res <<= 1;
	res |= d[offset + 1];
	res <<= 1;
	res |= d[offset + 2];
	res <<= 1;
	res |= d[offset + 3];
	res <<= 1;
	res |= d[offset + 4];
	res <<= 1;
	res |= d[offset + 5];
	res <<= 1;
	res |= d[offset + 6];
	return res;
}

static inline
uint16_t	getBits_8(uint8_t *d, int32_t offset) {
	uint16_t	res = d[offset];
	res <<= 1;
	res |= d[offset + 1];
	res <<= 1;
	res |= d[offset + 2];
	res <<= 1;
	res |= d[offset + 3];
	res <<= 1;
	res |= d[offset + 4];
	res <<= 1;
	res |= d[offset + 5];
	res <<= 1;
	res |= d[offset + 6];
	res <<= 1;
	res |= d[offset + 7];
	return res;
}


static inline
uint32_t	getLBits(uint8_t *d,
	int32_t offset, int16_t amount) {
	uint32_t	res = 0;
	int16_t		i;

	for (i = 0; i < amount; i++) {
		res <<= 1;
		res |= (d[offset + i] & 01);
	}
	return res;
}

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

int sdr_demod(struct demapped_transmission_frame_t *tf, struct sdr_state_t *sdr);
void sdr_init(struct sdr_state_t *sdr);
void init_dab_state(struct dab_state_t **dab, void (* eti_callback)(uint8_t *eti));
void dab_process_frame(struct dab_state_t *dab);

#endif
