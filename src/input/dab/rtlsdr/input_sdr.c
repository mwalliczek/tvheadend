/*
This file is part of rtl-dab, https://github.com/maydavid/rtl-dab
trl-dab is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Foobar is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with rtl-dab.  If not, see <http://www.gnu.org/licenses/>.


david may 2012
david.may.muc@googlemail.com

The sample reader (getSamples, getSample: frequency correction of the
samples and the long term signal level sLevel) follows the sample reader
of dab-cmdline; the oscillator is rewritten (recursive, in blocks).
dab-cmdline: Copyright (C) 2013 .. 2017 Jan van Katwijk (J.vanKatwijk@gmail.com),
GNU General Public License version 2 or later,
https://github.com/JvanKatwijk/dab-cmdline
(library/src/ofdm/sample-reader.cpp)

*/

#include <math.h>

#include "tvheadend.h"
#include "rtlsdr_private.h"
#include "dab.h"
#include "phasereference.h"
#include "ofdmDecoder.h"
#include "ficHandler.h"
#include "input_sdr_async.h"


float jan_abs(float _Complex z) {
    float re = crealf(z);
    float im = cimagf(z);
    if (re < 0) re = -re;
    if (im < 0) im = -im;
    return re + im;
}

float get_db(float x) {
    return 20 * log10f((x + 1) / (float)(256));
}

static
float convTable[] = {
    -128 / 128.0 , -127 / 128.0 , -126 / 128.0 , -125 / 128.0 , -124 / 128.0 , -123 / 128.0 , -122 / 128.0 , -121 / 128.0 , -120 / 128.0 , -119 / 128.0 , -118 / 128.0 , -117 / 128.0 , -116 / 128.0 , -115 / 128.0 , -114 / 128.0 , -113 / 128.0
    , -112 / 128.0 , -111 / 128.0 , -110 / 128.0 , -109 / 128.0 , -108 / 128.0 , -107 / 128.0 , -106 / 128.0 , -105 / 128.0 , -104 / 128.0 , -103 / 128.0 , -102 / 128.0 , -101 / 128.0 , -100 / 128.0 , -99 / 128.0 , -98 / 128.0 , -97 / 128.0
    , -96 / 128.0 , -95 / 128.0 , -94 / 128.0 , -93 / 128.0 , -92 / 128.0 , -91 / 128.0 , -90 / 128.0 , -89 / 128.0 , -88 / 128.0 , -87 / 128.0 , -86 / 128.0 , -85 / 128.0 , -84 / 128.0 , -83 / 128.0 , -82 / 128.0 , -81 / 128.0
    , -80 / 128.0 , -79 / 128.0 , -78 / 128.0 , -77 / 128.0 , -76 / 128.0 , -75 / 128.0 , -74 / 128.0 , -73 / 128.0 , -72 / 128.0 , -71 / 128.0 , -70 / 128.0 , -69 / 128.0 , -68 / 128.0 , -67 / 128.0 , -66 / 128.0 , -65 / 128.0
    , -64 / 128.0 , -63 / 128.0 , -62 / 128.0 , -61 / 128.0 , -60 / 128.0 , -59 / 128.0 , -58 / 128.0 , -57 / 128.0 , -56 / 128.0 , -55 / 128.0 , -54 / 128.0 , -53 / 128.0 , -52 / 128.0 , -51 / 128.0 , -50 / 128.0 , -49 / 128.0
    , -48 / 128.0 , -47 / 128.0 , -46 / 128.0 , -45 / 128.0 , -44 / 128.0 , -43 / 128.0 , -42 / 128.0 , -41 / 128.0 , -40 / 128.0 , -39 / 128.0 , -38 / 128.0 , -37 / 128.0 , -36 / 128.0 , -35 / 128.0 , -34 / 128.0 , -33 / 128.0
    , -32 / 128.0 , -31 / 128.0 , -30 / 128.0 , -29 / 128.0 , -28 / 128.0 , -27 / 128.0 , -26 / 128.0 , -25 / 128.0 , -24 / 128.0 , -23 / 128.0 , -22 / 128.0 , -21 / 128.0 , -20 / 128.0 , -19 / 128.0 , -18 / 128.0 , -17 / 128.0
    , -16 / 128.0 , -15 / 128.0 , -14 / 128.0 , -13 / 128.0 , -12 / 128.0 , -11 / 128.0 , -10 / 128.0 , -9 / 128.0 , -8 / 128.0 , -7 / 128.0 , -6 / 128.0 , -5 / 128.0 , -4 / 128.0 , -3 / 128.0 , -2 / 128.0 , -1 / 128.0
    , 0 / 128.0 , 1 / 128.0 , 2 / 128.0 , 3 / 128.0 , 4 / 128.0 , 5 / 128.0 , 6 / 128.0 , 7 / 128.0 , 8 / 128.0 , 9 / 128.0 , 10 / 128.0 , 11 / 128.0 , 12 / 128.0 , 13 / 128.0 , 14 / 128.0 , 15 / 128.0
    , 16 / 128.0 , 17 / 128.0 , 18 / 128.0 , 19 / 128.0 , 20 / 128.0 , 21 / 128.0 , 22 / 128.0 , 23 / 128.0 , 24 / 128.0 , 25 / 128.0 , 26 / 128.0 , 27 / 128.0 , 28 / 128.0 , 29 / 128.0 , 30 / 128.0 , 31 / 128.0
    , 32 / 128.0 , 33 / 128.0 , 34 / 128.0 , 35 / 128.0 , 36 / 128.0 , 37 / 128.0 , 38 / 128.0 , 39 / 128.0 , 40 / 128.0 , 41 / 128.0 , 42 / 128.0 , 43 / 128.0 , 44 / 128.0 , 45 / 128.0 , 46 / 128.0 , 47 / 128.0
    , 48 / 128.0 , 49 / 128.0 , 50 / 128.0 , 51 / 128.0 , 52 / 128.0 , 53 / 128.0 , 54 / 128.0 , 55 / 128.0 , 56 / 128.0 , 57 / 128.0 , 58 / 128.0 , 59 / 128.0 , 60 / 128.0 , 61 / 128.0 , 62 / 128.0 , 63 / 128.0
    , 64 / 128.0 , 65 / 128.0 , 66 / 128.0 , 67 / 128.0 , 68 / 128.0 , 69 / 128.0 , 70 / 128.0 , 71 / 128.0 , 72 / 128.0 , 73 / 128.0 , 74 / 128.0 , 75 / 128.0 , 76 / 128.0 , 77 / 128.0 , 78 / 128.0 , 79 / 128.0
    , 80 / 128.0 , 81 / 128.0 , 82 / 128.0 , 83 / 128.0 , 84 / 128.0 , 85 / 128.0 , 86 / 128.0 , 87 / 128.0 , 88 / 128.0 , 89 / 128.0 , 90 / 128.0 , 91 / 128.0 , 92 / 128.0 , 93 / 128.0 , 94 / 128.0 , 95 / 128.0
    , 96 / 128.0 , 97 / 128.0 , 98 / 128.0 , 99 / 128.0 , 100 / 128.0 , 101 / 128.0 , 102 / 128.0 , 103 / 128.0 , 104 / 128.0 , 105 / 128.0 , 106 / 128.0 , 107 / 128.0 , 108 / 128.0 , 109 / 128.0 , 110 / 128.0 , 111 / 128.0
    , 112 / 128.0 , 113 / 128.0 , 114 / 128.0 , 115 / 128.0 , 116 / 128.0 , 117 / 128.0 , 118 / 128.0 , 119 / 128.0 , 120 / 128.0 , 121 / 128.0 , 122 / 128.0 , 123 / 128.0 , 124 / 128.0 , 125 / 128.0 , 126 / 128.0 , 127 / 128.0 };

#define LEVEL_ALPHA	0.00001f

/* the oscillator rotates by -freqOffset / INPUT_RATE turns per sample */
static void osc_set_offset(struct sdr_state_t *sdr, int32_t freqOffset) {
    double w, re, im, t, sre, sim;
    int k;
    if (freqOffset == sdr->oscOffset)
        return;
    w = -2.0 * M_PI * freqOffset / INPUT_RATE;
    sre = cos(w);
    sim = sin(w);
    sdr->oscStepRe = sre;
    sdr->oscStepIm = sim;
    re = sre;
    im = sim;
    for (k = 0; k < OSC_BLOCK; k++) {
        sdr->oscTabRe[k] = re;
        sdr->oscTabIm[k] = im;
        t = re * sre - im * sim;
        im = re * sim + im * sre;
        re = t;
    }
    sdr->oscOffset = freqOffset;
}

/* keep the phasor on the unit circle (float rounding) */
static void osc_normalize(struct sdr_state_t *sdr) {
    float m = sqrtf(sdr->oscRe * sdr->oscRe + sdr->oscIm * sdr->oscIm);
    sdr->oscRe /= m;
    sdr->oscIm /= m;
}

/*
 * Convert and mix the samples to the corrected frequency. The samples of
 * a block are rotated with the phasor of the block start times a table
 * of the step powers, and the level is summed per block: no sample
 * depends on the one before, which keeps the FPU pipeline busy (the
 * recursive oscillator and level average were most of the time of the
 * demodulator thread on a Raspberry Pi).
 */
uint32_t getSamples(rtlsdr_frontend_t *lfe, float _Complex *v, uint32_t size, int32_t freqOffset) {
    struct sdr_state_t *sdr = &lfe->sdr;
    uint32_t i = 0, k, m, n;
    const uint8_t *buffer;
    float *out;

    osc_set_offset(sdr, freqOffset);
    while (i < size) {
        buffer = cbReadPtr(&sdr->fifo, &n);
        n /= 2;
        if (n == 0) {
            if (!readFromDevice(lfe))
                return i;
            continue;
        }
        if (n > size - i)
            n = size - i;

        for (k = 0; k < n; k += m) {
            const uint8_t *b = buffer + 2 * k;
            float bRe = sdr->oscRe, bIm = sdr->oscIm, sum = 0, t;
            uint32_t j;

            m = n - k < OSC_BLOCK ? n - k : OSC_BLOCK;
            out = (float *)&v[i + k];
            for (j = 0; j < m; j++) {
                float pRe = bRe * sdr->oscTabRe[j] - bIm * sdr->oscTabIm[j];
                float pIm = bRe * sdr->oscTabIm[j] + bIm * sdr->oscTabRe[j];
                float re = convTable[b[2 * j]], im = convTable[b[2 * j + 1]];
                float oRe = re * pRe - im * pIm;
                float oIm = re * pIm + im * pRe;
                out[2 * j] = oRe;
                out[2 * j + 1] = oIm;
                sum += fabsf(oRe) + fabsf(oIm);
            }
            /* advance the phasor by m steps */
            t = bRe * sdr->oscTabRe[m - 1] - bIm * sdr->oscTabIm[m - 1];
            sdr->oscIm = bRe * sdr->oscTabIm[m - 1] + bIm * sdr->oscTabRe[m - 1];
            sdr->oscRe = t;
            osc_normalize(sdr);
            /* long term average, (1 - a)^m ~ 1 - a m for a m << 1 */
            sdr->sLevel = (1 - LEVEL_ALPHA * m) * sdr->sLevel + LEVEL_ALPHA * sum;
        }
        cbConsume(&sdr->fifo, 2 * n);
        i += n;
    }
    return size;
}

uint32_t getSample(rtlsdr_frontend_t *lfe, float _Complex *v, float *abs, int32_t freqOffset) {
    struct sdr_state_t *sdr = &lfe->sdr;
    const uint8_t *buffer;
    uint32_t n;
    float re, im, t;

    buffer = cbReadPtr(&sdr->fifo, &n);
    if (n < 2) {
        if (!readFromDevice(lfe))
            return 0;
        buffer = cbReadPtr(&sdr->fifo, &n);
    }
    osc_set_offset(sdr, freqOffset);
    t = sdr->oscRe * sdr->oscStepRe - sdr->oscIm * sdr->oscStepIm;
    sdr->oscIm = sdr->oscRe * sdr->oscStepIm + sdr->oscIm * sdr->oscStepRe;
    sdr->oscRe = t;
    if ((++sdr->oscCount & 0xFFF) == 0)
        osc_normalize(sdr);

    re = convTable[buffer[0]];
    im = convTable[buffer[1]];
    cbConsume(&sdr->fifo, 2);
    t = re * sdr->oscRe - im * sdr->oscIm;
    im = re * sdr->oscIm + im * sdr->oscRe;
    re = t;
    *v = re + im * I;

    *abs = fabsf(re) + fabsf(im);
    sdr->sLevel = LEVEL_ALPHA * *abs + (1 - LEVEL_ALPHA) * sdr->sLevel;
    return 1;
}

void sdr_init_const(void) {
}

void sdr_init(struct sdr_state_t *sdr) {
    tvhdebug(LS_RTLSDR, "sdr_init %p", sdr);
    
    // circular buffer init
    cbInit(&(sdr->fifo), (196608 * 2 * 4)); // 4 frames

    initPhaseReference(sdr);
    initOfdmDecoder(sdr);
    initFicHandler(sdr);

    sdr->sLevel = 0;
    sdr->oscRe = 1;
    sdr->oscIm = 0;
    sdr->oscOffset = INT32_MIN;
    osc_set_offset(sdr, 0);
    sdr->oscCount = 0;

    sdr->mmi->fibProcessorIsSynced = 0;

#ifdef TRACE_RTLSDR_RAW
    sdr->traceFile = fopen("/tmp/rtlsdr_raw", "wb");
#endif
}

void sdr_destroy(struct sdr_state_t *sdr) {
    tvhdebug(LS_RTLSDR, "sdr_destroy %p", sdr);
    destroyFicHandler(sdr);
    destroyPhaseReference(sdr);
    destroyOfdmDecoder(sdr);
    cbFree(&sdr->fifo);
}
