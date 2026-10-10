#include <string.h>
#include <check.h>

#include "mp4processor.h"
#include "rtlsdr/firecheck.h"

const uint8_t* myResult;
int16_t myResultLength;
int memcmpResult;

void callback(const uint8_t* result, int16_t resultLength, const stream_parms* stream_parms, void* context);
int16_t mp4Processor_writeFrame(int16_t framelen, const stream_parms *sp, uint8_t *output, uint8_t *data);

START_TEST(mp4processorTest) {

    mp4processor_t* mp4 = init_mp4processor(72, NULL, callback);
    uint8_t *input = calloc(1728, sizeof(uint8_t));
    FILE *pFile;
    int i;
    
    firecheck_init();

    myResult = NULL;
    myResultLength = 0;
    for (i=0; i<=6; i++) {
        char buffer[128];
        snprintf(buffer, 128, "input/dab/mp4in%d", i);
        pFile = fopen (buffer, "rb");
        fread (input, 1, 1728, pFile);
        fclose(pFile);
        mp4Processor_addtoFrame(mp4, input);
    }
    free(input);
    ck_assert_ptr_ne(myResult, NULL);
    ck_assert_int_eq(myResultLength, 361);
    ck_assert_int_eq(memcmpResult, 0);
    printf("myResult %p, myResultLength %d\n", myResult, myResultLength);
    /* status counters: the AUs of the superframe, no errors */
    ck_assert_int_gt(mp4->statAUs, 0);
    ck_assert_int_eq(mp4->statAUErrors, 0);
    ck_assert_int_eq(mp4->statRSUncorrectable, 0);
    
    destroy_mp4processor(mp4);
} END_TEST
    
/* a byte error in the fire code (first bytes of a superframe) is
 * repaired by the RS code, the superframe must not be lost */
START_TEST(firecodeErrorTest) {
    mp4processor_t* mp4 = init_mp4processor(72, NULL, callback);
    uint8_t *input = calloc(1728, sizeof(uint8_t));
    FILE *pFile;
    int i;

    firecheck_init();

    myResult = NULL;
    myResultLength = 0;
    for (i=0; i<=6; i++) {
        char buffer[128];
        snprintf(buffer, 128, "input/dab/mp4in%d", i);
        pFile = fopen (buffer, "rb");
        ck_assert_int_eq(fread (input, 1, 1728, pFile), 1728);
        fclose(pFile);
        /* one bit per byte: corrupt the first byte of every block, one
           of them is the start of the superframe */
        input[0] ^= 1;
        input[3] ^= 1;
        mp4Processor_addtoFrame(mp4, input);
    }
    free(input);
    ck_assert_ptr_ne(myResult, NULL);
    ck_assert_int_eq(myResultLength, 361);
    ck_assert_int_eq(memcmpResult, 0);

    destroy_mp4processor(mp4);
} END_TEST

void callback(const uint8_t* result, int16_t resultLength, const stream_parms* stream_parms, void* context) {
    if (NULL == myResult && resultLength > 0) {
        FILE *pFile;
        uint8_t *output = calloc(361, sizeof(uint8_t));
        printf("result %p, resultLength %d\n", result, resultLength);
        myResult = result;
        myResultLength = resultLength;
        pFile = fopen ("input/dab/mp4out", "rb");
        fread (output, 1, 361, pFile);
        fclose(pFile);
        memcmpResult = memcmp(output, myResult, 361);

        printf("memcmp: %d\n", memcmpResult);
        free(output);
    }
}

/* AudioSpecificConfig: AAC LC 960, explicit SBR and PS */
START_TEST(audioSpecificConfigTest) {
    stream_parms sp = { 0 };
    uint8_t asc[4];

    sp.CoreSrIndex = 6;         /* 24 kHz */
    sp.CoreChConfig = 2;
    ck_assert_int_eq(mp4Processor_audioSpecificConfig(&sp, asc), 2);
    /* 00010 0110 0010 100 0 */
    ck_assert_int_eq(asc[0], 0x13);
    ck_assert_int_eq(asc[1], 0x14);

    sp.sbrFlag = 1;
    sp.ExtensionSrIndex = 3;    /* 48 kHz */
    ck_assert_int_eq(mp4Processor_audioSpecificConfig(&sp, asc), 4);
    /* 00101 0110 0010 0011 00010 100 0000000 */
    ck_assert_int_eq(asc[0], 0x2B);
    ck_assert_int_eq(asc[1], 0x11);
    ck_assert_int_eq(asc[2], 0x8A);
    ck_assert_int_eq(asc[3], 0x00);

    sp.psFlag = 1;
    sp.CoreChConfig = 1;
    ck_assert_int_eq(mp4Processor_audioSpecificConfig(&sp, asc), 4);
    ck_assert_int_eq(asc[0] >> 3, 29);
} END_TEST

/* the raw access unit comes back out of the LATM frames we write */
START_TEST(latmPayloadTest) {
    static const int lens[] = { 1, 100, 254, 255, 256, 600, 1000 };
    stream_parms sp = { 0 };
    uint8_t au[1024], frame[1200], out[1024];
    int i, k, cfg, n;

    for (i = 0; i < (int)sizeof(au); i++)
        au[i] = (uint8_t)(i * 7 + 3);
    for (cfg = 0; cfg < 3; cfg++) {
        sp.CoreSrIndex = 6;
        sp.CoreChConfig = cfg == 2 ? 1 : 2;
        sp.ExtensionSrIndex = 3;
        sp.sbrFlag = cfg > 0;
        sp.psFlag = cfg == 2;
        for (k = 0; k < (int)(sizeof(lens) / sizeof(lens[0])); k++) {
            memset(frame, 0, sizeof(frame));
            n = mp4Processor_writeFrame(lens[k], &sp, frame, au);
            n = mp4Processor_latmPayload(frame, n, out, sizeof(out));
            ck_assert_int_eq(n, lens[k]);
            ck_assert_int_eq(memcmp(out, au, n), 0);
        }
    }

    /* the frame from the decoder test data */
    {
        FILE *f = fopen("input/dab/mp4out", "rb");
        ck_assert_ptr_ne(f, NULL);
        n = fread(frame, 1, sizeof(frame), f);
        fclose(f);
        ck_assert_int_eq(n, 361);
        n = mp4Processor_latmPayload(frame, n, out, sizeof(out));
        ck_assert_int_gt(n, 340);
        ck_assert_int_lt(n, 361);
    }

    /* broken input */
    ck_assert_int_eq(mp4Processor_latmPayload(frame, 2, out, sizeof(out)), -1);
    frame[0] = 0;
    ck_assert_int_eq(mp4Processor_latmPayload(frame, 361, out, sizeof(out)), -1);
} END_TEST

Suite * mp4processor_suite(void) {
    Suite *s;
    TCase *tc_core;

    s = suite_create("mp4processor");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, mp4processorTest);
    tcase_add_test(tc_core, firecodeErrorTest);
    tcase_add_test(tc_core, audioSpecificConfigTest);
    tcase_add_test(tc_core, latmPayloadTest);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = mp4processor_suite();
    sr = srunner_create(s);

    srunner_set_xml(sr, "mp4processorTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

