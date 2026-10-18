#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "mp2processor.h"

/* frames handed out by the processor */
static int frames;
static int lastLen, lastRate, lastChannels;
static uint8_t lastFrame[2000];

static void callback(const uint8_t *frame, int len, int sampleRate,
                     int channels, void *context)
{
    (void)context;
    frames++;
    lastLen = len;
    lastRate = sampleRate;
    lastChannels = channels;
    memcpy(lastFrame, frame, len);
}

/* a stream of Layer II frames: header, then a payload numbered by frame */
static uint8_t *make_stream(const uint8_t hdr[4], int flen, int n, int prefix)
{
    uint8_t *s = malloc(prefix + flen * n);
    int i, j;

    for (i = 0; i < prefix; i++)
        s[i] = 0x55;
    for (i = 0; i < n; i++) {
        uint8_t *f = s + prefix + i * flen;
        memcpy(f, hdr, 4);
        for (j = 4; j < flen; j++)
            f[j] = (uint8_t)(i * 7 + j);    /* never 0xff 0xf. at j % 256 */
        f[4] = (uint8_t)i;
    }
    return s;
}

/* feed the bytes as the sub-channel delivers them: 3 * bitRate bytes per
   logical frame, one bit per byte */
static void feed(mp2processor_t *mp2, const uint8_t *s, int len)
{
    int n = 3 * mp2->bitRate, i, b;
    uint8_t *bits = malloc(8 * n);

    for (i = 0; i + n <= len; i += n) {
        for (b = 0; b < 8 * n; b++)
            bits[b] = (s[i + b / 8] >> (7 - b % 8)) & 1;
        mp2Processor_addtoFrame(mp2, bits);
    }
    free(bits);
}

static void reset(void)
{
    frames = lastLen = lastRate = lastChannels = 0;
}

/* MPEG-1 Layer II, 128 kbit/s, 48 kHz, stereo, CRC: 384 bytes = 24 ms */
static const uint8_t hdr48[4] = { 0xff, 0xfc, 0x84, 0x00 };

START_TEST(frameLengthTest) {
    int sr, ch;
    const uint8_t mono[4]  = { 0xff, 0xfc, 0x84, 0xc0 };
    const uint8_t lsf[4]   = { 0xff, 0xf4, 0x84, 0x00 };  /* 64 kbit/s, 24 kHz */
    const uint8_t layer3[4] = { 0xff, 0xfa, 0x84, 0x00 };
    const uint8_t mpeg25[4] = { 0xff, 0xe4, 0x84, 0x00 };
    const uint8_t freefmt[4] = { 0xff, 0xfc, 0x04, 0x00 };
    const uint8_t badrate[4] = { 0xff, 0xfc, 0xf4, 0x00 };
    const uint8_t badsr[4]   = { 0xff, 0xfc, 0x8c, 0x00 };

    ck_assert_int_eq(mp2_frame_length(hdr48, &sr, &ch), 384);
    ck_assert_int_eq(sr, 48000);
    ck_assert_int_eq(ch, 2);
    ck_assert_int_eq(mp2_frame_length(mono, &sr, &ch), 384);
    ck_assert_int_eq(ch, 1);
    ck_assert_int_eq(mp2_frame_length(lsf, &sr, &ch), 384);
    ck_assert_int_eq(sr, 24000);
    ck_assert_int_eq(mp2_frame_length(layer3, NULL, NULL), 0);
    ck_assert_int_eq(mp2_frame_length(mpeg25, NULL, NULL), 0);
    ck_assert_int_eq(mp2_frame_length(freefmt, NULL, NULL), 0);
    ck_assert_int_eq(mp2_frame_length(badrate, NULL, NULL), 0);
    ck_assert_int_eq(mp2_frame_length(badsr, NULL, NULL), 0);
} END_TEST

/* one frame per logical frame: after the first two (the first header is
   confirmed by the second) every logical frame gives one audio frame */
START_TEST(alignedFramesTest) {
    mp2processor_t *mp2 = init_mp2processor(128, NULL, callback);
    uint8_t *s = make_stream(hdr48, 384, 10, 0);

    reset();
    feed(mp2, s, 384);
    ck_assert_int_eq(frames, 0);
    feed(mp2, s + 384, 384 * 9);
    ck_assert_int_eq(frames, 10);
    ck_assert_int_eq(lastLen, 384);
    ck_assert_int_eq(lastRate, 48000);
    ck_assert_int_eq(lastChannels, 2);
    ck_assert_int_eq(memcmp(lastFrame, s + 9 * 384, 384), 0);
    ck_assert_int_eq(mp2->statFrames, 10);
    ck_assert_int_eq(mp2->statLostSync, 0);

    free(s);
    destroy_mp2processor(mp2);
} END_TEST

/* the reception starts in the middle of a frame */
START_TEST(unalignedStartTest) {
    mp2processor_t *mp2 = init_mp2processor(128, NULL, callback);
    uint8_t *s = make_stream(hdr48, 384, 10, 384 - 100);

    reset();
    feed(mp2, s, 384 * 10);
    /* the last frame is incomplete in the fed bytes */
    ck_assert_int_eq(frames, 9);
    ck_assert_int_eq(lastFrame[4], 8);
    ck_assert_int_eq(mp2->statLostSync, 0);

    free(s);
    destroy_mp2processor(mp2);
} END_TEST

/* a destroyed header: that frame is lost, the next ones are found again */
START_TEST(lostSyncTest) {
    mp2processor_t *mp2 = init_mp2processor(128, NULL, callback);
    uint8_t *s = make_stream(hdr48, 384, 12, 0);

    s[5 * 384 + 1] = 0x00;
    reset();
    feed(mp2, s, 384 * 12);
    ck_assert_int_eq(mp2->statLostSync, 1);
    /* frames 0..4, then 6..11 */
    ck_assert_int_eq(frames, 11);
    ck_assert_int_eq(lastFrame[4], 11);

    free(s);
    destroy_mp2processor(mp2);
} END_TEST

/* MPEG-2 LSF (24 kHz): a frame of 48 ms spans two logical frames */
START_TEST(halfSampleRateTest) {
    const uint8_t lsf[4] = { 0xff, 0xf4, 0x84, 0x00 };
    mp2processor_t *mp2 = init_mp2processor(64, NULL, callback);
    uint8_t *s = make_stream(lsf, 384, 6, 0);

    reset();
    feed(mp2, s, 384 * 6);
    ck_assert_int_eq(frames, 6);
    ck_assert_int_eq(lastRate, 24000);
    ck_assert_int_eq(memcmp(lastFrame, s + 5 * 384, 384), 0);

    free(s);
    destroy_mp2processor(mp2);
} END_TEST

/* no frames at all: nothing comes out, the buffer does not overflow */
START_TEST(noiseTest) {
    mp2processor_t *mp2 = init_mp2processor(128, NULL, callback);
    uint8_t *s = malloc(384 * 50);
    int i;

    srand(7);
    for (i = 0; i < 384 * 50; i++)
        s[i] = rand() & 0x7f;       /* no sync word */
    reset();
    feed(mp2, s, 384 * 50);
    ck_assert_int_eq(frames, 0);
    ck_assert_int_le(mp2->len, mp2->size);

    free(s);
    destroy_mp2processor(mp2);
} END_TEST

static Suite *mp2processor_suite(void) {
    Suite *s = suite_create("mp2processor");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, frameLengthTest);
    tcase_add_test(tc_core, alignedFramesTest);
    tcase_add_test(tc_core, unalignedStartTest);
    tcase_add_test(tc_core, lostSyncTest);
    tcase_add_test(tc_core, halfSampleRateTest);
    tcase_add_test(tc_core, noiseTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    Suite *s = mp2processor_suite();
    SRunner *sr = srunner_create(s);

    srunner_set_xml(sr, "mp2processorTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
