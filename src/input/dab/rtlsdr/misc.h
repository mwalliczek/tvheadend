#ifndef _MISC_H
#define _MISC_H

#include "dab.h"

void merge_info(struct ens_info_t* ei, struct tf_info_t *info);
void time_deinterleave(uint8_t* dst, uint8_t* cifs[]);
void create_eti(struct dab_state_t* dab);
void dump_ens_info(struct ens_info_t* info);
void dab_descramble_bytes(uint8_t *buf, int32_t nbytes);
int check_fib_crc(uint8_t* data);
int init_eti(uint8_t* eti,struct ens_info_t *info);

#endif

