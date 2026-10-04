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

/* feed the stream like the sub-channel decoder: packet aligned frames */
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
