#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "charsets.h"

static void check_conv(const char *in, int size, CharacterSet cs, const char *expected) {
    char *s = toStringUsingCharset(in, cs, size);
    ck_assert_ptr_ne(s, NULL);
    ck_assert_str_eq(s, expected);
    free(s);
}

START_TEST(ebuLatinAsciiTest) {
    check_conv("Bayern 3        ", 16, EbuLatin, "Bayern 3");
    check_conv("DLF", -1, EbuLatin, "DLF");
} END_TEST

START_TEST(ebuLatinNonAsciiTest) {
    /* EBU Latin is not ISO 8859-1: 0x82 is e acute, 0x91 a umlaut, 0x8d sharp s */
    check_conv("Caf\x82", -1, EbuLatin, "Caf\xc3\xa9");
    check_conv("Stra\x8d" "e", -1, EbuLatin, "Stra\xc3\x9f" "e");
    check_conv("\x91\x97\x99", -1, EbuLatin, "\xc3\xa4\xc3\xb6\xc3\xbc");
    /* euro sign needs three UTF-8 bytes */
    check_conv("5\xa9", -1, EbuLatin, "5\xe2\x82\xac");
    /* every non-zero character gives valid, non-empty output */
    for (int c = 1; c < 256; c++) {
        char in[2] = { (char)c, 'x' };
        char *s = toStringUsingCharset(in, EbuLatin, 2);
        ck_assert_int_ge(strlen(s), 2);
        ck_assert_int_le(strlen(s), 4);
        free(s);
    }
} END_TEST

START_TEST(trimTest) {
    check_conv("", -1, EbuLatin, "");
    check_conv("                ", 16, EbuLatin, "");
    check_conv("A ", 0, EbuLatin, "");
    /* an embedded NUL terminates a fixed size label */
    check_conv("ABC\0DEF", 7, EbuLatin, "ABC");
} END_TEST

START_TEST(otherCharsetsTest) {
    check_conv("Gr\xfc\xdf", -1, IsoLatin, "Gr\xc3\xbc\xc3\x9f");
    check_conv("Gr\xc3\xbc\xc3\x9f  ", -1, UnicodeUtf8, "Gr\xc3\xbc\xc3\x9f");
    check_conv("\x00" "A" "\x00\xe4" "\x20\xac", 6, UnicodeUcs2, "A\xc3\xa4\xe2\x82\xac");
} END_TEST

static Suite *charsets_suite(void) {
    Suite *s = suite_create("charsets");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, ebuLatinAsciiTest);
    tcase_add_test(tc_core, ebuLatinNonAsciiTest);
    tcase_add_test(tc_core, trimTest);
    tcase_add_test(tc_core, otherCharsetsTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(charsets_suite());

    srunner_set_xml(sr, "charsetsTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
