#ifndef _OFDMDECODER_H
#define _OFDMDECODER_H

void initOfdmDecoder(struct sdr_state_t *sdr);
void destroyOfdmDecoder(struct sdr_state_t *sdr);
void processBlock_0(struct sdr_state_t *sdr, const float _Complex* v);
void decodeBlock(struct sdr_state_t *sdr, const float _Complex* v, int32_t blkno);

#endif
