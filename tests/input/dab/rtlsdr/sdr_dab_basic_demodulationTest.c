#include <check.h>

#include "rtlsdr_private.h"
#include "sdr_dab_basic_demodulation.h"
#include "phasereference.h"
#include "ofdmDecoder.h"
#include <sys/stat.h>

#define RAW_FILE "input/dab/rtlsdr/rtlsdr_raw"
#define RAW_URL  "https://matthias.walliczek.de/rtlsdr_raw"

void process_mscBlock(struct sdr_state_t *sdr, int16_t data[], int16_t blkno) {
}

static FILE *pFile;
static int count = 0;
static int fibCRCsuccessCount = -1;

struct service_queue service_all;

int readFromDevice(rtlsdr_frontend_t *lfe) {
    uint8_t *input;
    int res;
    if (lfe->sdr.fibCRCsuccess > 0 && fibCRCsuccessCount < 0)
        fibCRCsuccessCount = count;
    input = malloc(262144);
    res = fread(input, 1, 262144, pFile);
    if (res == 262144) {
        count += res;
        cbWrite(&(lfe->sdr.fifo), input, res);
    }
    free(input);
    return res == 262144;
}

START_TEST(demodulationTest) {
    dab_service_t *ds;
    int service_count = 0;
    rtlsdr_frontend_t *lfe;

    sdr_init_const();
    initConstPhaseReference();
    initConstViterbi768();
    initConstOfdmDecoder();
    TAILQ_INIT(&service_all);

    lfe = calloc(1, sizeof(rtlsdr_frontend_t));
    lfe->sdr.mmi = calloc(1, sizeof(dab_ensemble_instance_t));
    lfe->sdr.mmi->mmi_ensemble = calloc(1, sizeof(dab_ensemble_t));
    lfe->running = 1;
    lfe->lfe_dvr_pipe.rd = 1;

    sdr_init(&lfe->sdr);

    rtlsdr_demod_thread_fn(lfe);

    while ((ds = (dab_service_t *)TAILQ_FIRST(&service_all)) != NULL) {
        TAILQ_REMOVE(&service_all, ds, s_all_link);
        free(ds->s_dab_svcname);
        free(ds);
        service_count++;
    }

    printf("fibCRCrate %d (fibCRCsuccessCount %d, snr: %.6f, service_count: %d)\n",
           lfe->sdr.fibCRCrate, fibCRCsuccessCount, lfe->sdr.mmi->tii_stats.snr / 10000.0, service_count);

    ck_assert_int_ge(fibCRCsuccessCount, 0);
    ck_assert_int_gt(lfe->sdr.fibCRCrate, 50);
    ck_assert_int_gt(service_count, 0);
    ck_assert_int_eq(lfe->sdr.mmi->fibProcessorIsSynced, 1);

    sdr_destroy(&lfe->sdr);
    free(lfe->sdr.mmi->mmi_ensemble);
    free(lfe->sdr.mmi);
    free(lfe);
} END_TEST

static Suite *sdr_dab_basic_demodulation_suite(void) {
    Suite *s = suite_create("sdr_dab_basic_demodulation");
    TCase *tc_core = tcase_create("Core");
    tcase_set_timeout(tc_core, 300);
    tcase_add_test(tc_core, demodulationTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr;

    struct stat st;

    if (stat(RAW_FILE, &st) || st.st_size == 0) {
        if (system("wget -q " RAW_URL " -O " RAW_FILE ".part") == 0)
            rename(RAW_FILE ".part", RAW_FILE);
        unlink(RAW_FILE ".part");
    }
    pFile = fopen(RAW_FILE, "rb");
    {
        if (pFile == NULL) {
            unlink(RAW_FILE);
            printf("SKIPPED: %s not available (download from %s)\n", RAW_FILE, RAW_URL);
            return EXIT_SUCCESS;
        }
    }

    sr = srunner_create(sdr_dab_basic_demodulation_suite());
    srunner_set_xml(sr, "sdr_dab_basic_demodulationTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    fclose(pFile);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
