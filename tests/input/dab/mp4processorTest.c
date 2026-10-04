#include <check.h>

#include "mp4processor.h"
#include "rtlsdr/firecheck.h"

const uint8_t* myResult;
int16_t myResultLength;
int memcmpResult;

void callback(const uint8_t* result, int16_t resultLength, const stream_parms* stream_parms, void* context);

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

Suite * mp4processor_suite(void) {
    Suite *s;
    TCase *tc_core;

    s = suite_create("mp4processor");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, mp4processorTest);
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

