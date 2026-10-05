#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "dab_mot.h"

typedef struct received {
    int count;
    int content_type, content_subtype;
    uint16_t tid;
    char name[64];
    uint8_t body[4096];
    size_t body_len;
} received_t;

static void on_object(const dab_mot_object_t *obj, void *opaque) {
    received_t *r = opaque;
    r->count++;
    r->tid = obj->transport_id;
    r->content_type = obj->content_type;
    r->content_subtype = obj->content_subtype;
    snprintf(r->name, sizeof(r->name), "%s", obj->name ?: "");
    ck_assert_int_le(obj->body_len, sizeof(r->body));
    memcpy(r->body, obj->body, obj->body_len);
    r->body_len = obj->body_len;
}

static uint8_t *load(const char *name, size_t *len) {
    uint8_t *buf = malloc(65536);
    FILE *f = fopen(name, "rb");
    ck_assert_ptr_ne(f, NULL);
    *len = fread(buf, 1, 65536, f);
    fclose(f);
    return buf;
}

static void check_epg_object(received_t *r) {
    size_t len;
    uint8_t *pi = load("input/dab/epg/pi.bin", &len);
    ck_assert_int_eq(r->content_type, DAB_MOT_CT_EPG);
    ck_assert_int_eq(r->content_subtype, DAB_MOT_CST_EPG_PI);
    ck_assert_str_eq(r->name, "20261004_D312_PI.EHB");
    ck_assert_int_eq(r->body_len, len);
    ck_assert_int_eq(memcmp(r->body, pi, len), 0);
    free(pi);
}

/* feed the stream like the sub-channel decoder, one logical frame
 * (3 * bitrate bytes, not necessarily packet aligned) at a time */
static void feed(dab_mot_decoder_t *dec, const uint8_t *d, size_t len, size_t frame) {
    for (size_t i = 0; i < len; i += frame)
        dab_mot_decoder_feed_packets(dec, d + i, len - i < frame ? len - i : frame);
}

START_TEST(headerModeTest) {
    size_t len;
    uint8_t *pk = load("input/dab/epg/packets_header.bin", &len);
    received_t r;
    dab_mot_decoder_t *dec;

    memset(&r, 0, sizeof(r));
    dec = dab_mot_decoder_create(1, on_object, &r);
    feed(dec, pk, len, 96);
    ck_assert_int_eq(r.count, 1);
    ck_assert_int_eq(r.tid, 0x1234);
    check_epg_object(&r);
    ck_assert_int_eq(dab_mot_decoder_stats(dec)->packet_crc_errors, 0);
    ck_assert_int_eq(dab_mot_decoder_stats(dec)->datagroup_errors, 0);

    /* the carousel repeats the object, it is reported only once */
    feed(dec, pk, len, 2 * 96);
    ck_assert_int_eq(r.count, 1);
    dab_mot_decoder_destroy(dec);

    /* low bit rate sub-channels: the 96 byte packets span logical frames
       (8 kbit/s: 24 bytes, 24 kbit/s: 72 bytes per frame) */
    for (size_t frame = 24; frame <= 120; frame += 48) {
        memset(&r, 0, sizeof(r));
        dec = dab_mot_decoder_create(1, on_object, &r);
        feed(dec, pk, len, frame);
        ck_assert_msg(r.count == 1, "frame size %zu: %d objects", frame, r.count);
        check_epg_object(&r);
        ck_assert_int_eq(dab_mot_decoder_stats(dec)->packet_crc_errors, 0);
        dab_mot_decoder_destroy(dec);
    }

    /* packets for another address are ignored */
    memset(&r, 0, sizeof(r));
    dec = dab_mot_decoder_create(5, on_object, &r);
    feed(dec, pk, len, 96);
    ck_assert_int_eq(r.count, 0);
    dab_mot_decoder_destroy(dec);
    free(pk);
} END_TEST

START_TEST(directoryModeGzipTest) {
    size_t len;
    uint8_t *pk = load("input/dab/epg/packets_directory.bin", &len);
    received_t r;
    dab_mot_decoder_t *dec;

    memset(&r, 0, sizeof(r));
    dec = dab_mot_decoder_create(2, on_object, &r);
    feed(dec, pk, len, 48);
    ck_assert_int_eq(r.count, 1);
    ck_assert_int_eq(r.tid, 0x2345);
    check_epg_object(&r);
    dab_mot_decoder_destroy(dec);
    free(pk);
} END_TEST

START_TEST(corruptionTest) {
    size_t len;
    uint8_t *pk = load("input/dab/epg/packets_directory.bin", &len);
    uint8_t *bad = malloc(len);
    received_t r;
    dab_mot_decoder_t *dec;

    memset(&r, 0, sizeof(r));
    dec = dab_mot_decoder_create(2, on_object, &r);

    /* a bit error in the third packet: the CRC fails, nothing is delivered */
    memcpy(bad, pk, len);
    bad[2 * 48 + 10] ^= 0x01;
    feed(dec, bad, len, 48);
    ck_assert_int_eq(r.count, 0);
    ck_assert_int_ge(dab_mot_decoder_stats(dec)->packet_crc_errors, 1);

    /* the next carousel cycle completes the object */
    feed(dec, pk, len, 48);
    ck_assert_int_eq(r.count, 1);
    check_epg_object(&r);

    /* a lost packet (continuity index jump) drops only that data group */
    dab_mot_decoder_destroy(dec);
    memset(&r, 0, sizeof(r));
    dec = dab_mot_decoder_create(2, on_object, &r);
    feed(dec, pk, 48, 48);
    feed(dec, pk + 2 * 48, len - 2 * 48, 48);
    ck_assert_int_eq(r.count, 0);
    feed(dec, pk, len, 48);
    ck_assert_int_eq(r.count, 1);
    check_epg_object(&r);

    /* random garbage must not crash the decoder */
    srand(7);
    for (int n = 0; n < 500; n++) {
        memcpy(bad, pk, len);
        for (int k = 0; k < 3; k++)
            bad[rand() % len] = rand();
        feed(dec, bad, len, 48);
    }
    dab_mot_decoder_destroy(dec);
    free(bad);
    free(pk);
} END_TEST

/* packet mode FEC (EN 300 401 5.3.5): RS(204,188) over GF(256), x^8+x^4+x^3+x^2+1,
 * generator roots alpha^0 .. alpha^15 */
static uint8_t gf_exp[512], gf_log[256], rs_gen[17];

static uint8_t gf_mul(uint8_t a, uint8_t b) {
    return a && b ? gf_exp[gf_log[a] + gf_log[b]] : 0;
}

static void rs_init(void) {
    int i, j, x = 1;
    for (i = 0; i < 255; i++) {
        gf_exp[i] = gf_exp[i + 255] = x;
        gf_log[x] = i;
        x <<= 1;
        if (x & 0x100)
            x ^= 0x11D;
    }
    /* ascending coefficients */
    memset(rs_gen, 0, sizeof(rs_gen));
    rs_gen[0] = 1;
    for (i = 0; i < 16; i++)
        for (j = i + 1; j >= 0; j--)
            rs_gen[j] = (j ? rs_gen[j - 1] : 0) ^ gf_mul(gf_exp[i], rs_gen[j]);
}

static void rs_parity(const uint8_t *msg, uint8_t *parity) {
    uint8_t buf[204];
    int i, k;
    memcpy(buf, msg, 188);
    memset(buf + 188, 0, 16);
    for (i = 0; i < 188; i++)
        if (buf[i])
            for (k = 1; k <= 16; k++)
                buf[i + k] ^= gf_mul(buf[i], rs_gen[16 - k]);
    memcpy(parity, buf + 188, 16);
}

static size_t adt_index(int layout, int row, int col) {
    return (layout & 1) ? (size_t)row * 188 + col : (size_t)col * 12 + row;
}

static size_t rsd_index(int layout, int row, int col) {
    return (layout & 2) ? (size_t)row * 16 + col : (size_t)col * 12 + row;
}

/* the packet stream padded to whole FEC frames, each Application Data
 * Table followed by its 9 FEC packets */
static uint8_t *fec_stream(const uint8_t *pk, size_t len, int layout, size_t *outlen) {
    size_t frames = (len + 2255) / 2256, f, i;
    uint8_t *out = calloc(frames, 2256 + 216), *adt, rsd[198], msg[188], par[16];
    int row, col;

    for (f = 0; f < frames; f++) {
        adt = out + f * (2256 + 216);
        for (i = 0; i < 2256; i += 24) {
            if (f * 2256 + i < len) {
                memcpy(adt + i, pk + f * 2256 + i, 24);
            } else {
                /* padding packet, address 0 */
                uint16_t crc;
                memset(adt + i, 0, 24);
                adt[i] = 0x0C;          /* first and last */
                crc = dab_crc16(adt + i, 22);
                adt[i + 22] = crc >> 8;
                adt[i + 23] = crc & 0xFF;
            }
        }
        memset(rsd, 0, sizeof(rsd));
        for (row = 0; row < 12; row++) {
            for (col = 0; col < 188; col++)
                msg[col] = adt[adt_index(layout, row, col)];
            rs_parity(msg, par);
            for (col = 0; col < 16; col++)
                rsd[rsd_index(layout, row, col)] = par[col];
        }
        for (i = 0; i < 9; i++) {
            uint8_t *p = adt + 2256 + i * 24;
            p[0] = 0x03 | ((i & 3) << 4);
            p[1] = 0xFE;
            memcpy(p + 2, rsd + i * 22, 22);
        }
    }
    *outlen = frames * (2256 + 216);
    return out;
}

/* about two byte errors per code word, in the application data only */
static void fec_corrupt(uint8_t *d, size_t len) {
    for (size_t f = 0; f + 2256 + 216 <= len; f += 2256 + 216)
        for (size_t i = 5; i < 2256; i += 97)
            d[f + i] ^= 0x5A;
}

START_TEST(fecTest) {
    size_t len, flen;
    uint8_t *pk = load("input/dab/epg/packets_header.bin", &len), *fs;
    received_t r;
    dab_mot_decoder_t *dec;
    int layout;

    rs_init();
    for (layout = 0; layout < 4; layout++) {
        fs = fec_stream(pk, len, layout, &flen);

        /* error free: with or without FEC decoding */
        memset(&r, 0, sizeof(r));
        dec = dab_mot_decoder_create(1, on_object, &r);
        feed(dec, fs, flen, 72);
        ck_assert_int_eq(r.count, 1);
        dab_mot_decoder_destroy(dec);

        memset(&r, 0, sizeof(r));
        dec = dab_mot_decoder_create(1, on_object, &r);
        dab_mot_decoder_set_fec(dec, 1);
        feed(dec, fs, flen, 72);
        ck_assert_int_eq(r.count, 1);
        check_epg_object(&r);
        ck_assert_int_eq(dab_mot_decoder_stats(dec)->fec_bytes_corrected, 0);
        ck_assert_int_eq(dab_mot_decoder_stats(dec)->fec_rows_failed, 0);
        dab_mot_decoder_destroy(dec);

        /* byte errors: lost without FEC, corrected with it */
        fec_corrupt(fs, flen);
        memset(&r, 0, sizeof(r));
        dec = dab_mot_decoder_create(1, on_object, &r);
        feed(dec, fs, flen, 72);
        ck_assert_int_eq(r.count, 0);
        dab_mot_decoder_destroy(dec);

        memset(&r, 0, sizeof(r));
        dec = dab_mot_decoder_create(1, on_object, &r);
        dab_mot_decoder_set_fec(dec, 1);
        feed(dec, fs, flen, 72);
        ck_assert_msg(r.count == 1, "layout %d: %d objects", layout, r.count);
        check_epg_object(&r);
        ck_assert_int_eq(dab_mot_decoder_stats(dec)->packet_crc_errors, 0);
        ck_assert_int_gt(dab_mot_decoder_stats(dec)->fec_bytes_corrected, 0);
        ck_assert_int_eq(dab_mot_decoder_stats(dec)->fec_rows_failed, 0);
        dab_mot_decoder_destroy(dec);
        free(fs);
    }

    /* FEC signalled, but no FEC packets in the stream: passed through
       (delayed by a FEC frame, it might be corrected) */
    memset(&r, 0, sizeof(r));
    dec = dab_mot_decoder_create(1, on_object, &r);
    dab_mot_decoder_set_fec(dec, 1);
    for (int n = 0; n < 8; n++)
        feed(dec, pk, len, 96);
    ck_assert_int_eq(r.count, 1);
    check_epg_object(&r);
    dab_mot_decoder_destroy(dec);
    free(pk);
} END_TEST

START_TEST(crcTest) {
    /* CRC-16/GENIBUS check value */
    ck_assert_int_eq(dab_crc16((const uint8_t *)"123456789", 9), 0xD64E);
} END_TEST

static Suite *dab_mot_suite(void) {
    Suite *s = suite_create("dab_mot");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, crcTest);
    tcase_add_test(tc_core, headerModeTest);
    tcase_add_test(tc_core, directoryModeGzipTest);
    tcase_add_test(tc_core, corruptionTest);
    tcase_add_test(tc_core, fecTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(dab_mot_suite());

    srunner_set_xml(sr, "dab_motTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
