#ifndef DAB_PRIVATE_H
#define DAB_PRIVATE_H

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

#endif