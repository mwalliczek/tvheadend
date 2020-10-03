#include <check.h>

#include "ficHandler.h"

struct service_queue service_all;

void process_ficInput(struct sdr_state_t *sdr, int16_t ficno);

START_TEST(ficHandlerTest) {

    struct sdr_state_t sdr;
    FILE *pFile;
    uint8_t *output;
    
    memset(&sdr, 0, sizeof(sdr));
    TAILQ_INIT(&service_all);
    
    sdr.mmi = calloc(1, sizeof(dab_ensemble_instance_t));
    sdr.mmi->mmi_ensemble = calloc(1, sizeof(dab_ensemble_t));
    
    initConstViterbi768();
    initFicHandler(&sdr);
    
    pFile = fopen ("input/dab/rtlsdr/ficHandlerTest/ficInput" , "rb" );
    fread (sdr.ofdm_input, 2, 2304, pFile);
    fclose(pFile);
    
    process_ficInput(&sdr, 0);
    
    printf("sdr.fibCRCsuccess %d\n", sdr.fibCRCsuccess);
    ck_assert_int_eq(sdr.fibCRCsuccess, 1);
    
    output = malloc(256);
    
    pFile = fopen ("input/dab/rtlsdr/ficHandlerTest/ficOutput" , "rb" );
    fread (output, 1, 256, pFile);
    fclose(pFile);
    
    int memcmpResult = memcmp(output, &sdr.bitBuffer_out[512], 256);
    printf("memcmp: %d\n", memcmpResult);
    ck_assert_int_eq(memcmpResult, 0);
    
    destroyFicHandler(&sdr);
    free(sdr.mmi->mmi_ensemble);
    free(sdr.mmi);
    free(output);
    
    exit(EXIT_SUCCESS);
} END_TEST

Suite * ficHandler_suite(void) {
    Suite *s;
    TCase *tc_core;

    s = suite_create("ficHandler");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, ficHandlerTest);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = ficHandler_suite();
    sr = srunner_create(s);

    srunner_set_xml(sr, "ficHandlerTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

