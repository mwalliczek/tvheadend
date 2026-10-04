#include <check.h>

#include "dab.h"
#include "tvheadend.h"
#include "mscHandler.h"

void sdr_dab_service_instance_dataCallback(const uint8_t* result, int16_t resultLength, const stream_parms* stream_parms, void* context);

static dab_ensemble_t *ensemble;
static dab_service_t *service;

static void setup(void) {
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

static Suite *sdr_dab_service_instance_suite(void) {
    Suite *s = suite_create("sdr_dab_service_instance");
    TCase *tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, createDestroyTest);
    tcase_add_test(tc_core, invalidSubchannelTest);
    tcase_add_test(tc_core, frameDurationTest);
    tcase_add_test(tc_core, mscOutOfRangeTest);
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
