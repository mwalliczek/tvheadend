#include <check.h>

#include "input.h"

START_TEST(dab_serviceTest) {
    dab_ensemble_t* mm = calloc(1, sizeof(dab_ensemble_t));

    dab_service_t* sut = dab_service_find(mm, 1, 1, 0);
    dab_service_delete(sut, 1);
    
} END_TEST

Suite * dab_service_suite(void) {
    Suite *s;
    TCase *tc_core;

    s = suite_create("dab_service");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, dab_serviceTest);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = dab_service_suite();
    sr = srunner_create(s);

    srunner_set_xml(sr, "dab_serviceTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

