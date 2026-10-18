/*
 *    Copyright (C) 2013 .. 2017
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Computing
 *
 *    Derived from dab-cmdline, https://github.com/JvanKatwijk/dab-cmdline
 *    (library/includes/bit-extractors.h)
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    Bit extractors for the FIC/FIB parsing
 *
 *    Adapted to tvheadend by Matthias Walliczek (matthias@walliczek.de)
 */

#ifndef DAB_PRIVATE_H
#define DAB_PRIVATE_H

//	generic, up to 16 bits
static inline
uint16_t	getBits(const uint8_t *d, int32_t offset, int16_t size) {
	int16_t	i;
	uint16_t	res = 0;

	for (i = 0; i < size; i++) {
		res <<= 1;
		res |= (d[offset + i]) & 01;
	}
	return res;
}

static inline
uint16_t	getBits_1(const uint8_t *d, int32_t offset) {
	return (d[offset] & 0x01);
}

static inline
uint16_t	getBits_2(const uint8_t *d, int32_t offset) {
	uint16_t	res = d[offset];
	res <<= 1;
	res |= (d[offset + 1] & 01);
	return res;
}

static inline
uint16_t	getBits_3(const uint8_t *d, int32_t offset) {
	uint16_t	res = d[offset];
	res <<= 1;
	res |= d[offset + 1];
	res <<= 1;
	res |= d[offset + 2];
	return res;
}

static inline
uint16_t	getBits_4(const uint8_t *d, int32_t offset) {
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
uint16_t	getBits_5(const uint8_t *d, int32_t offset) {
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
uint16_t	getBits_6(const uint8_t *d, int32_t offset) {
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
uint16_t	getBits_7(const uint8_t *d, int32_t offset) {
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
uint16_t	getBits_8(const uint8_t *d, int32_t offset) {
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
uint32_t	getLBits(const uint8_t *d,
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