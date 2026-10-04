#include <check.h>

#include "tvheadend.h"
#include "input.h"
#include "fib-processor.h"

struct service_queue service_all;

extern int tvh_thread_debug;
extern int test_mutex_locked;
extern int test_mutex_trylock_fail;
extern char *test_network_name;

/* a FIB unpacked to one bit per byte, as delivered by the FIC decoder */
typedef struct {
    uint8_t bits[256];
    int pos;                /* write position in bits */
} fib_t;

static void put(fib_t *f, int nbits, uint32_t value) {
    ck_assert_int_le(f->pos + nbits, 256);
    for (int i = nbits - 1; i >= 0; i--)
        f->bits[f->pos++] = (value >> i) & 1;
}

static void fib_init(fib_t *f) {
    memset(f, 0, sizeof(*f));
}

/* pad the rest of the 30 data bytes with 0xFF (end marker + padding) */
static void fib_finish(fib_t *f) {
    while (f->pos < 240)
        put(f, 8, 0xFF);
}

static void fig_header(fib_t *f, int type, int length) {
    put(f, 3, type);
    put(f, 5, length);
}

static void fig0_header(fib_t *f, int length, int pd, int ext) {
    fig_header(f, 0, length);
    put(f, 1, 0);           /* C/N */
    put(f, 1, 0);           /* OE */
    put(f, 1, pd);          /* P/D */
    put(f, 5, ext);
}

static void fig1_label(fib_t *f, int ext, uint16_t id, const char *label) {
    int i, len = strlen(label);
    fig_header(f, 1, 1 + 2 + 16 + 2);
    put(f, 4, 0);           /* charset: EBU Latin */
    put(f, 1, 0);           /* OE */
    put(f, 3, ext);
    put(f, 16, id);
    for (i = 0; i < 16; i++)
        put(f, 8, i < len ? (uint8_t)label[i] : ' ');
    put(f, 16, 0xFF00);     /* character flag field */
}

static dab_ensemble_instance_t *dei;

static void setup(void) {
    TAILQ_INIT(&service_all);
    dei = calloc(1, sizeof(dab_ensemble_instance_t));
    dei->mmi_ensemble = calloc(1, sizeof(dab_ensemble_t));
    tvh_thread_debug = 1;   /* use the counting mutex mocks */
    test_mutex_locked = 0;
    test_mutex_trylock_fail = 0;
}

static void teardown(void) {
    dab_service_t *s;
    while ((s = (dab_service_t *)TAILQ_FIRST(&service_all)) != NULL) {
        TAILQ_REMOVE(&service_all, s, s_all_link);
        free(s->s_dab_svcname);
        free(s);
    }
    free(dei->mmi_ensemble);
    free(dei);
    free(test_network_name);
    test_network_name = NULL;
    tvh_thread_debug = 0;
}

static void process(fib_t *f) {
    fib_finish(f);
    process_FIB(dei, f->bits, 0);
    ck_assert_int_eq(test_mutex_locked, 0);
}

START_TEST(emptyFibTest) {
    fib_t f;
    fib_init(&f);
    process(&f);
} END_TEST

START_TEST(subchannelShortFormTest) {
    fib_t f;
    fib_init(&f);
    fig0_header(&f, 1 + 3, 0, 1);
    put(&f, 6, 5);          /* SubChId */
    put(&f, 10, 100);       /* start address */
    put(&f, 1, 0);          /* short form */
    put(&f, 1, 0);          /* table switch */
    put(&f, 6, 23);         /* table index: 84 CU, level 1, 80 kbit/s */
    process(&f);

    subChannel *sc = &dei->mmi_ensemble->subChannels[5];
    ck_assert_int_eq(sc->inUse, 1);
    ck_assert_int_eq(sc->StartAddr, 100);
    ck_assert_int_eq(sc->shortForm, 1);
    ck_assert_int_eq(sc->Length, 84);
    ck_assert_int_eq(sc->protLevel, 1);
    ck_assert_int_eq(sc->BitRate, 80);

    /* a new organisation has to be saved, a repetition not */
    ck_assert_int_eq(dei->mmi_ensemble->mm_fic_changed, 1);
    dei->mmi_ensemble->mm_fic_changed = 0;
    process_FIB(dei, f.bits, 0);
    ck_assert_int_eq(dei->mmi_ensemble->mm_fic_changed, 0);
} END_TEST

START_TEST(subchannelLongFormTest) {
    fib_t f;
    fib_init(&f);
    fig0_header(&f, 1 + 4 + 4, 0, 1);
    put(&f, 6, 1);          /* EEP 3-A, 54 CU -> 72 kbit/s */
    put(&f, 10, 0);
    put(&f, 1, 1);          /* long form */
    put(&f, 3, 0);          /* option A */
    put(&f, 2, 2);          /* protection level 3 */
    put(&f, 10, 54);
    put(&f, 6, 2);          /* EEP 2-B, 84 CU -> 128 kbit/s */
    put(&f, 10, 54);
    put(&f, 1, 1);
    put(&f, 3, 1);          /* option B */
    put(&f, 2, 1);          /* protection level 2 */
    put(&f, 10, 84);
    process(&f);

    subChannel *sc = &dei->mmi_ensemble->subChannels[1];
    ck_assert_int_eq(sc->shortForm, 0);
    ck_assert_int_eq(sc->protLevel, 2);
    ck_assert_int_eq(sc->Length, 54);
    ck_assert_int_eq(sc->BitRate, 72);
    sc = &dei->mmi_ensemble->subChannels[2];
    ck_assert_int_eq(sc->StartAddr, 54);
    ck_assert_int_eq(sc->protLevel, 1 | 4);
    ck_assert_int_eq(sc->BitRate, 128);
} END_TEST

START_TEST(ensembleLabelTest) {
    fib_t f;
    fib_init(&f);
    fig1_label(&f, 0, 0x10C1, "Caf\x82 DAB");
    process(&f);
    ck_assert_ptr_ne(test_network_name, NULL);
    ck_assert_str_eq(test_network_name, "Caf\xc3\xa9 DAB");
    ck_assert_int_eq(dei->mmi_ensemble->mm_onid, 0x10C1);
    ck_assert_int_eq(dei->fibProcessorIsSynced, 1);
} END_TEST

START_TEST(audioServiceTest) {
    fib_t f;
    dab_service_t *s;

    fib_init(&f);
    fig1_label(&f, 1, 0xD312, "Bayern 3");
    process(&f);

    fib_init(&f);
    fig0_header(&f, 1 + 3, 0, 1);       /* subchannel 7 */
    put(&f, 6, 7);
    put(&f, 10, 0);
    put(&f, 1, 0);
    put(&f, 1, 0);
    put(&f, 6, 43);
    fig0_header(&f, 1 + 2 + 1 + 2, 0, 2);   /* service D312 -> subchannel 7 */
    put(&f, 16, 0xD312);
    put(&f, 1, 0);          /* local flag */
    put(&f, 3, 0);          /* CAId */
    put(&f, 4, 1);          /* number of components */
    put(&f, 2, 0);          /* TMid: MSC stream audio */
    put(&f, 6, 63);         /* ASCTy: DAB+ */
    put(&f, 6, 7);          /* SubChId */
    put(&f, 1, 1);          /* primary */
    put(&f, 1, 0);          /* CA */
    process(&f);

    s = dab_service_find(dei->mmi_ensemble, 0xD312, 0, NULL);
    ck_assert_ptr_ne(s, NULL);
    ck_assert_str_eq(s->s_dab_svcname, "Bayern 3");
    ck_assert_int_eq(s->subChId, 7);
    ck_assert_int_eq(s->ASCTy, 63);
    ck_assert_int_eq(s->s_verified, 1);
} END_TEST

START_TEST(packetComponentOverflowTest) {
    fib_t f;
    int sc = 1;
    subChannel last;

    memset(&last, 0, sizeof(last));
    /* 6 FIBs with 12 packet components each: more than the 64 slots */
    for (int n = 0; n < 6; n++) {
        fib_init(&f);
        fig0_header(&f, 1 + 2 + 1 + 12 * 2, 0, 2);
        put(&f, 16, 0xE000 + n);
        put(&f, 1, 0);
        put(&f, 3, 0);
        put(&f, 4, 12);
        for (int i = 0; i < 12; i++) {
            put(&f, 2, 3);  /* TMid: MSC packet data */
            put(&f, 12, sc++);
            put(&f, 1, 0);
            put(&f, 1, 0);
        }
        process(&f);
    }
    for (int i = 0; i < 64; i++)
        ck_assert_int_eq(dei->mmi_ensemble->ServiceComps[i].inUse, 1);
    /* nothing was written in front of the ServiceComps array */
    ck_assert_int_eq(memcmp(&dei->mmi_ensemble->subChannels[63], &last, sizeof(last)), 0);
} END_TEST

START_TEST(programTypeTest) {
    fib_t f;
    dab_service_t *s1, *s2;

    /* FIG 0/17 only annotates known services */
    fib_init(&f);
    fig0_header(&f, 1 + 4, 0, 17);
    put(&f, 16, 0xD003);
    put(&f, 16, 0x0000);
    process(&f);
    ck_assert_ptr_eq(dab_service_find(dei->mmi_ensemble, 0xD003, 0, NULL), NULL);
    dab_service_find(dei->mmi_ensemble, 0xD001, 1, NULL);
    dab_service_find(dei->mmi_ensemble, 0xD002, 1, NULL);

    fib_init(&f);
    fig0_header(&f, 1 + 5 + 4, 0, 17);
    put(&f, 16, 0xD001);    /* first entry with language */
    put(&f, 1, 0);          /* S/D */
    put(&f, 1, 0);          /* P/S */
    put(&f, 1, 1);          /* L flag */
    put(&f, 1, 0);          /* CC flag */
    put(&f, 4, 0);
    put(&f, 8, 0x08);       /* language: German */
    put(&f, 3, 0);
    put(&f, 5, 10);         /* programme type */
    put(&f, 16, 0xD002);    /* second entry without language */
    put(&f, 1, 0);
    put(&f, 1, 0);
    put(&f, 1, 0);
    put(&f, 1, 0);
    put(&f, 4, 0);
    put(&f, 3, 0);
    put(&f, 5, 14);
    process(&f);

    s1 = dab_service_find(dei->mmi_ensemble, 0xD001, 0, NULL);
    s2 = dab_service_find(dei->mmi_ensemble, 0xD002, 0, NULL);
    ck_assert_ptr_ne(s1, NULL);
    ck_assert_ptr_ne(s2, NULL);
    ck_assert_int_eq(s1->language, 0x08);
    ck_assert_int_eq(s1->hasLanguage, 1);
    ck_assert_int_eq(s1->programType, 10);
    ck_assert_int_eq(s2->hasLanguage, 0);
    ck_assert_int_eq(s2->programType, 14);

    /* when the global lock is busy for the first entry, the second one
       still has to be parsed at the right offset */
    s2->programType = 0;
    test_mutex_trylock_fail = 1;
    process_FIB(dei, f.bits, 0);
    ck_assert_int_eq(test_mutex_locked, 0);
    ck_assert_int_eq(s2->programType, 14);
} END_TEST

START_TEST(fecSchemeTest) {
    fib_t f;
    fib_init(&f);
    fig0_header(&f, 1 + 2, 0, 14);
    put(&f, 6, 3);
    put(&f, 2, 1);
    put(&f, 6, 4);
    put(&f, 2, 1);
    dei->mmi_ensemble->subChannels[10].SubChId = 3;
    dei->mmi_ensemble->subChannels[11].SubChId = 4;
    process(&f);
    ck_assert_int_eq(dei->mmi_ensemble->subChannels[10].FEC_scheme, 1);
    /* the last entry of the FIG must not be lost */
    ck_assert_int_eq(dei->mmi_ensemble->subChannels[11].FEC_scheme, 1);
} END_TEST

START_TEST(oversizedFigTest) {
    fib_t f;
    fib_init(&f);
    fig_header(&f, 7, 27);  /* FIG type 7, skipped, up to byte 28 */
    f.pos = 28 * 8;
    fig0_header(&f, 31, 0, 1); /* claims 31 bytes, only 2 left in the FIB */
    process(&f);
    ck_assert_int_eq(dei->mmi_ensemble->subChannels[0].inUse, 0);
} END_TEST


/* FIG 0/1, 0/2 (data service with a packet mode component), 0/3 and 0/13 */
static void epg_service_fib(fib_t *f, int appType) {
    fib_init(f);
    fig0_header(f, 1 + 3, 0, 1);        /* subchannel 3: 16 CU */
    put(f, 6, 3);
    put(f, 10, 500);
    put(f, 1, 0);
    put(f, 1, 0);
    put(f, 6, 0);
    fig0_header(f, 1 + 4 + 1 + 2, 1, 2); /* data service E0123456, one packet component */
    put(f, 32, 0xE0123456);
    put(f, 1, 0);
    put(f, 3, 0);
    put(f, 4, 1);
    put(f, 2, 3);                       /* TMid: packet data */
    put(f, 12, 5);                      /* SCId */
    put(f, 1, 1);                       /* primary */
    put(f, 1, 0);
    fig0_header(f, 1 + 5, 0, 3);        /* SCId 5 -> subchannel 3, address 1, MOT */
    put(f, 12, 5);
    put(f, 3, 0);
    put(f, 1, 0);                       /* no CAOrg */
    put(f, 1, 0);                       /* DG flag */
    put(f, 1, 0);
    put(f, 6, 60);                      /* DSCTy: MOT */
    put(f, 6, 3);
    put(f, 10, 1);
    fig0_header(f, 1 + 4 + 1 + 2, 1, 13); /* user application of E0123456 */
    put(f, 32, 0xE0123456);
    put(f, 4, 0);                       /* SCIdS */
    put(f, 4, 1);                       /* one application */
    put(f, 11, appType);
    put(f, 5, 0);
}

START_TEST(epgComponentTest) {
    fib_t f;
    int subch = -1, address = -1;

    epg_service_fib(&f, 2);             /* MOT slideshow: not an EPG */
    process(&f);
    ck_assert_int_eq(dab_ensemble_find_epg_component(dei->mmi_ensemble, &subch, &address), 0);

    epg_service_fib(&f, 7);             /* SPI / EPG */
    process(&f);
    ck_assert_int_eq(dab_ensemble_find_epg_component(dei->mmi_ensemble, &subch, &address), 1);
    ck_assert_int_eq(subch, 3);
    ck_assert_int_eq(address, 1);
    ck_assert_int_eq(dei->mmi_ensemble->subChannels[3].BitRate, 32);
} END_TEST

static Suite *fib_processor_suite(void) {
    Suite *s = suite_create("fib-processor");
    TCase *tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, emptyFibTest);
    tcase_add_test(tc_core, subchannelShortFormTest);
    tcase_add_test(tc_core, subchannelLongFormTest);
    tcase_add_test(tc_core, ensembleLabelTest);
    tcase_add_test(tc_core, audioServiceTest);
    tcase_add_test(tc_core, packetComponentOverflowTest);
    tcase_add_test(tc_core, programTypeTest);
    tcase_add_test(tc_core, fecSchemeTest);
    tcase_add_test(tc_core, oversizedFigTest);
    tcase_add_test(tc_core, epgComponentTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(fib_processor_suite());

    srunner_set_xml(sr, "fib-processorTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
