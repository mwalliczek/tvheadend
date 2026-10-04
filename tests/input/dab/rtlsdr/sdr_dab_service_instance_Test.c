#include <check.h>

#include "dab.h"
#include "tvheadend.h"
#include "mscHandler.h"
#include "dab_mot.h"

void sdr_dab_service_instance_dataCallback(const uint8_t* result, int16_t resultLength, const stream_parms* stream_parms, void* context);

static dab_ensemble_t *ensemble;
static dab_service_t *service;

static void setup(void) {
    initConstViterbi768();
    ensemble = calloc(1, sizeof(dab_ensemble_t));
    ensemble->subChannels[0].protLevel = 2;
    ensemble->subChannels[0].BitRate = 72;
    ensemble->subChannels[0].Length = 54;
    ensemble->subChannels[0].inUse = 1;
    service = calloc(1, sizeof(dab_service_t));
    service->s_dab_ensemble = ensemble;
    service->subChId = 0;
    service->s_nicename = (char *)"test";
}

static void teardown(void) {
    free(service);
    free(ensemble);
}

START_TEST(createDestroyTest) {
    sdr_dab_service_instance_t* sds = sdr_dab_service_instance_create(service);
    ck_assert_ptr_ne(sds, NULL);
    ck_assert_int_eq(sds->fragmentSize, 54 * CUSize);
    ck_assert_int_eq(sds->protection->outSize, 24 * 72);
    sdr_dab_service_instance_destroy(sds);
} END_TEST

START_TEST(invalidSubchannelTest) {
    /* e.g. a long form FIG 0/1 with a size too small for one bitrate unit */
    ensemble->subChannels[0].BitRate = 0;
    ck_assert_ptr_eq(sdr_dab_service_instance_create(service), NULL);
} END_TEST

START_TEST(frameDurationTest) {
    /* every DAB+ superframe covers 120 ms, regardless of the AU layout */
    static const int expectedAUs[4] = { 4, 2, 6, 3 };
    sdr_dab_service_instance_t* sds = sdr_dab_service_instance_create(service);
    for (int dacRate = 0; dacRate < 2; dacRate++) {
        for (int sbr = 0; sbr < 2; sbr++) {
            stream_parms sp;
            memset(&sp, 0, sizeof(sp));
            sp.dacRate = dacRate;
            sp.sbrFlag = sbr;
            sp.CoreSrIndex = dacRate ? (sbr ? 6 : 3) : (sbr ? 8 : 5);
            sp.ExtensionSrIndex = dacRate ? 3 : 5;
            sds->dts = 0;
            for (int i = 0; i < expectedAUs[2 * dacRate + sbr]; i++)
                sdr_dab_service_instance_dataCallback(NULL, 0, &sp, sds);
            ck_assert_int_eq(sds->dts, 90000 * 120 / 1000);
        }
    }
    sdr_dab_service_instance_destroy(sds);
} END_TEST

START_TEST(mscOutOfRangeTest) {
    struct sdr_state_t *sdr = calloc(1, sizeof(struct sdr_state_t));
    int16_t data[2 * K];
    sdr_dab_service_instance_t* sds = sdr_dab_service_instance_create(service);
    LIST_INIT(&sdr->active_service_instance);
    LIST_INSERT_HEAD(&sdr->active_service_instance, sds, service_link);
    memset(data, 0, sizeof(data));

    /* a complete CIF with a valid subchannel is passed on */
    for (int blk = 4; blk < 4 + numberofblocksperCIF; blk++)
        process_mscBlock(sdr, data, blk);
    ck_assert_int_eq(sds->nextIn, 1);

    /* a subchannel beyond the end of the CIF (864 CU) is ignored */
    ensemble->subChannels[0].StartAddr = 850;
    for (int blk = 4; blk < 4 + numberofblocksperCIF; blk++)
        process_mscBlock(sdr, data, blk);
    ck_assert_int_eq(sds->nextIn, 1);

    /* as is a subchannel which was resized after the service was started */
    ensemble->subChannels[0].StartAddr = 0;
    ensemble->subChannels[0].Length = 60;
    for (int blk = 4; blk < 4 + numberofblocksperCIF; blk++)
        process_mscBlock(sdr, data, blk);
    ck_assert_int_eq(sds->nextIn, 1);

    sdr_dab_service_instance_destroy(sds);
    free(sdr);
} END_TEST


/* ---- end to end: a packet mode sub-channel through the receive chain ---- */

static const int interleave_map[16] = { 0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15 };

static int parity(uint32_t x) {
    return __builtin_parity(x);
}

/*
 * DAB transmitter for one logical frame: energy dispersal, convolutional
 * code (K = 7, octal 133 171 145 133) and puncturing, giving soft bits
 */
static void encode_frame(const protection_t *prot, const uint8_t *bytes, int16_t *soft, int fragment) {
    static const int polys[4] = { 0155, 0117, 0123, 0155 };  /* bit reversed notation */
    int nbits = prot->outSize, o = 0, j = 0;
    uint32_t sr = 0;

    for (int i = 0; i < nbits + 6; i++) {
        int bit = 0;
        if (i < nbits)
            bit = ((bytes[i / 8] >> (7 - i % 8)) & 1) ^ prot->disperseVector[i];
        sr = ((sr << 1) | bit) & 0x7F;
        for (int k = 0; k < 4; k++, j++)
            if (prot->indexTable[j])
                soft[o++] = parity(sr & polys[k]) ? 127 : -127;
    }
    ck_assert_int_le(o, fragment);
    while (o < fragment)
        soft[o++] = 0;          /* padding */
}

static int mot_objects;
static size_t mot_body_len;
static uint8_t mot_body[4096];

static void on_mot_object(const dab_mot_object_t *obj, void *opaque) {
    mot_objects++;
    mot_body_len = obj->body_len;
    memcpy(mot_body, obj->body, obj->body_len < sizeof(mot_body) ? obj->body_len : sizeof(mot_body));
}

START_TEST(packetModeEndToEndTest) {
    /* EEP 3-A, 96 kbit/s: 72 CU, 288 bytes (6 packets of 48 bytes) per frame */
    sdr_dab_service_instance_t *sds;
    uint8_t stream[16384], pi[1024];
    size_t len, pilen;
    int frames, fragment;
    int16_t (*soft)[72 * CUSize];
    int16_t data[72 * CUSize];
    FILE *f;

    ensemble->subChannels[3].inUse = 1;
    ensemble->subChannels[3].BitRate = 96;
    ensemble->subChannels[3].Length = 72;
    ensemble->subChannels[3].protLevel = 2;
    sds = sdr_dab_data_instance_create(ensemble, 3, 2, on_mot_object, NULL);
    ck_assert_ptr_ne(sds, NULL);
    fragment = sds->fragmentSize;
    ck_assert_int_eq(fragment, 72 * CUSize);

    f = fopen("input/dab/epg/packets_directory.bin", "rb");
    ck_assert_ptr_ne(f, NULL);
    len = fread(stream, 1, sizeof(stream), f);
    fclose(f);
    f = fopen("input/dab/epg/pi.bin", "rb");
    ck_assert_ptr_ne(f, NULL);
    pilen = fread(pi, 1, sizeof(pi), f);
    fclose(f);

    /* the encoded logical frames, unused bytes are zero (CRC errors) */
    frames = (len + 287) / 288;
    soft = calloc(frames + 16, sizeof(*soft));
    for (int t = 0; t < frames; t++) {
        uint8_t bytes[288];
        memset(bytes, 0, sizeof(bytes));
        memcpy(bytes, stream + t * 288, len - t * 288 < 288 ? len - t * 288 : 288);
        encode_frame(sds->protection, bytes, soft[t], fragment);
    }
    for (int t = frames; t < frames + 16; t++) {
        uint8_t bytes[288];
        memset(bytes, 0, sizeof(bytes));
        encode_frame(sds->protection, bytes, soft[t], fragment);
    }

    /* time interleaving: bit i of frame t is sent in frame t + map[i % 16] */
    mot_objects = 0;
    for (int t = 0; t < frames + 16; t++) {
        for (int i = 0; i < fragment; i++) {
            int src = t - interleave_map[i & 15];
            data[i] = src >= 0 ? soft[src][i] : 0;
        }
        sdr_dab_service_instance_process_data(sds, data);
    }

    ck_assert_int_eq(mot_objects, 1);
    ck_assert_int_eq(mot_body_len, pilen);
    ck_assert_int_eq(memcmp(mot_body, pi, pilen), 0);
    ck_assert_int_eq(dab_mot_decoder_stats(sds->mot)->datagroup_errors, 0);

    free(soft);
    sdr_dab_service_instance_destroy(sds);
} END_TEST

static Suite *sdr_dab_service_instance_suite(void) {
    Suite *s = suite_create("sdr_dab_service_instance");
    TCase *tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, createDestroyTest);
    tcase_add_test(tc_core, invalidSubchannelTest);
    tcase_add_test(tc_core, frameDurationTest);
    tcase_add_test(tc_core, mscOutOfRangeTest);
    tcase_add_test(tc_core, packetModeEndToEndTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(sdr_dab_service_instance_suite());

    srunner_set_xml(sr, "sdr_dab_service_instance_TestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
