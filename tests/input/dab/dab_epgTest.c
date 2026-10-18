#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "dab_epg.h"

/* 2026-10-04 00:00 UTC */
#define DAY ((time_t)(61317 - 40587) * 86400)

typedef struct result {
    int count;
    dab_epg_programme_t p[8];
    char *str[8][8];
} result_t;

static char *dup(const char *s) { return s ? strdup(s) : NULL; }

static void collect(const dab_epg_programme_t *p, void *opaque) {
    result_t *r = opaque;
    if (r->count < 8) {
        int i = r->count;
        r->p[i] = *p;
        r->p[i].crid = r->str[i][0] = dup(p->crid);
        r->p[i].short_name = r->str[i][1] = dup(p->short_name);
        r->p[i].medium_name = r->str[i][2] = dup(p->medium_name);
        r->p[i].long_name = r->str[i][3] = dup(p->long_name);
        r->p[i].short_desc = r->str[i][4] = dup(p->short_desc);
        r->p[i].long_desc = r->str[i][5] = dup(p->long_desc);
        r->p[i].genre = r->str[i][6] = dup(p->genre);
        r->p[i].lang = r->str[i][7] = dup(p->lang);
    }
    r->count++;
}

static void result_free(result_t *r) {
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            free(r->str[i][j]);
}

static uint8_t *load(const char *name, size_t *len) {
    uint8_t *buf = malloc(65536);
    FILE *f = fopen(name, "rb");
    ck_assert_ptr_ne(f, NULL);
    *len = fread(buf, 1, 65536, f);
    fclose(f);
    return buf;
}

/* document generated with the hybridspi reference encoder */
START_TEST(referenceDocumentTest) {
    size_t len;
    uint8_t *doc = load("input/dab/epg/pi.bin", &len);
    result_t r;
    memset(&r, 0, sizeof(r));

    ck_assert_int_eq(dab_epg_parse(doc, len, NULL, collect, &r), 2);
    ck_assert_int_eq(r.count, 2);

    ck_assert(r.p[0].service.valid);
    ck_assert(r.p[0].service.has_ensemble);
    ck_assert_int_eq(r.p[0].service.ecc, 0xE0);
    ck_assert_int_eq(r.p[0].service.eid, 0x10C1);
    ck_assert_int_eq(r.p[0].service.sid, 0xD312);
    ck_assert_int_eq(r.p[0].start, DAY + 13 * 3600);
    ck_assert_int_eq(r.p[0].duration, 4 * 3600);
    ck_assert_int_eq(r.p[0].short_id, 0x123456);
    ck_assert_int_eq(r.p[0].on_air, 1);
    ck_assert_str_eq(r.p[0].crid, "crid://br.de/12345");
    ck_assert_str_eq(r.p[0].short_name, "Morgen");
    ck_assert_str_eq(r.p[0].medium_name, "Fr\xc3\xbc" "haufdreher");
    ck_assert_str_eq(r.p[0].long_name, "Bayern 3 - Die Fr\xc3\xbc" "haufdreher");
    ck_assert_str_eq(r.p[0].genre, "urn:tva:metadata:cs:ContentCS:2002:3.6.8");

    ck_assert_int_eq(r.p[1].service.sid, 0xD312);
    ck_assert_int_eq(r.p[1].start, DAY + 17 * 3600 + 30);
    ck_assert_int_eq(r.p[1].duration, 90 * 60);
    ck_assert_ptr_eq(r.p[1].short_name, NULL);
    ck_assert_str_eq(r.p[1].medium_name, "Mittag");

    result_free(&r);
    free(doc);
} END_TEST

/* ---- hand made documents ---- */

typedef struct buf { uint8_t d[1024]; size_t len; } buf_t;

static void put(buf_t *b, const void *d, size_t len) {
    memcpy(b->d + b->len, d, len);
    b->len += len;
}

static void elem(buf_t *b, int tag, const buf_t *content) {
    uint8_t h[4] = { tag, 0, 0, 0 };
    if (content->len < 0xFE) {
        h[1] = content->len;
        put(b, h, 2);
    } else {
        h[1] = 0xFE; h[2] = content->len >> 8; h[3] = content->len & 0xFF;
        put(b, h, 4);
    }
    put(b, content->d, content->len);
}

static void elem_raw(buf_t *b, int tag, const void *d, size_t len) {
    buf_t c = { .len = 0 };
    put(&c, d, len);
    elem(b, tag, &c);
}

static void text_elem(buf_t *b, int tag, const char *s) {
    buf_t c = { .len = 0 };
    elem_raw(&c, 0x01, s, strlen(s));
    elem(b, tag, &c);
}

/* MJD 61317, 18:15 UTC, local time offset +2h */
static const uint8_t time_lto[] = { 0x3b, 0xe1, 0x54, 0x8f, 0x04 };
/* MJD 61317, 20:00 UTC */
static const uint8_t time_20h[] = { 0x3b, 0xe1, 0x45, 0x00 };

START_TEST(tokensDefaultIdTest) {
    buf_t doc = { .len = 0 }, root = { .len = 0 }, sched = { .len = 0 };
    buf_t prog = { .len = 0 }, md = { .len = 0 }, loc = { .len = 0 }, t = { .len = 0 };
    buf_t tokens = { .len = 0 };
    result_t r;
    /* default content id: audio service C221 on the same ensemble */
    static const uint8_t cid[] = { 0x00, 0xC2, 0x21 };
    static const uint8_t genre[] = { 0x03, 0x06, 0x01 };
    static const uint8_t dur[] = { 0x0E, 0x10 };

    elem_raw(&tokens, 0x01, "Nachrichten", 11);
    elem_raw(&tokens, 0x02, " aus Bayern", 11);
    elem(&root, 0x04, &tokens);
    elem_raw(&root, 0x05, cid, sizeof(cid));
    elem_raw(&root, 0x06, "de", 2);

    elem_raw(&prog, 0x81, "\x00\x00\x07", 3);
    text_elem(&prog, 0x11, "\x01\x02");                 /* "Nachrichten aus Bayern" */
    text_elem(&md, 0x1A, "Kurz: \x01");
    text_elem(&md, 0x1B, "Alle \x01\x02 und der Welt");
    elem(&prog, 0x13, &md);
    {
        buf_t g = { .len = 0 };
        elem_raw(&g, 0x80, genre, sizeof(genre));
        elem(&prog, 0x14, &g);
    }
    elem_raw(&t, 0x80, time_lto, sizeof(time_lto));
    elem_raw(&t, 0x81, dur, sizeof(dur));
    elem(&loc, 0x2C, &t);
    elem(&prog, 0x19, &loc);
    elem(&sched, 0x1C, &prog);
    elem(&root, 0x21, &sched);
    elem(&doc, 0x02, &root);

    memset(&r, 0, sizeof(r));
    ck_assert_int_eq(dab_epg_parse(doc.d, doc.len, NULL, collect, &r), 1);
    ck_assert_int_eq(r.p[0].service.sid, 0xC221);
    ck_assert_int_eq(r.p[0].service.has_ensemble, 0);
    /* the time is UTC, the local time offset does not change it */
    ck_assert_int_eq(r.p[0].start, DAY + 18 * 3600 + 15 * 60);
    ck_assert_int_eq(r.p[0].duration, 3600);
    ck_assert_int_eq(r.p[0].short_id, 7);
    ck_assert_str_eq(r.p[0].medium_name, "Nachrichten aus Bayern");
    ck_assert_str_eq(r.p[0].short_desc, "Kurz: Nachrichten");
    ck_assert_str_eq(r.p[0].long_desc, "Alle Nachrichten aus Bayern und der Welt");
    ck_assert_str_eq(r.p[0].genre, "urn:tva:metadata:cs:ContentCS:2002:3.6.1");
    ck_assert_str_eq(r.p[0].lang, "de");
    result_free(&r);
} END_TEST

static size_t build_scope_doc(buf_t *doc, int with_scope, int off_air) {
    buf_t root = { .len = 0 }, sched = { .len = 0 }, scope = { .len = 0 }, ss = { .len = 0 };
    buf_t prog = { .len = 0 }, loc = { .len = 0 }, t = { .len = 0 };
    static const uint8_t cid[] = { 0x00, 0xD3, 0x13 };
    static const uint8_t dur[] = { 0x07, 0x08 };

    doc->len = 0;
    if (with_scope) {
        elem_raw(&ss, 0x80, cid, sizeof(cid));
        elem(&scope, 0x25, &ss);
        elem(&sched, 0x24, &scope);
    }
    if (off_air)
        elem_raw(&prog, 0x84, "\x02", 1);
    text_elem(&prog, 0x10, "News");
    elem_raw(&t, 0x80, time_20h, sizeof(time_20h));
    elem_raw(&t, 0x81, dur, sizeof(dur));
    elem(&loc, 0x2C, &t);
    elem(&prog, 0x19, &loc);
    elem(&sched, 0x1C, &prog);
    elem(&root, 0x21, &sched);
    elem(doc, 0x02, &root);
    return doc->len;
}

START_TEST(serviceSelectionTest) {
    buf_t doc;
    result_t r;
    dab_epg_contentid_t def = { .valid = 1, .sid = 0xAAAA };

    /* no bearer, no default content id, no scope: unknown service */
    build_scope_doc(&doc, 0, 0);
    memset(&r, 0, sizeof(r));
    ck_assert_int_eq(dab_epg_parse(doc.d, doc.len, NULL, collect, &r), 0);

    /* the caller can provide the service (e.g. from the MOT ScopeId) */
    ck_assert_int_eq(dab_epg_parse(doc.d, doc.len, &def, collect, &r), 1);
    ck_assert_int_eq(r.p[0].service.sid, 0xAAAA);
    ck_assert_int_eq(r.p[0].start, DAY + 20 * 3600);
    ck_assert_int_eq(r.p[0].duration, 1800);
    result_free(&r);

    /* the schedule scope wins over the default */
    build_scope_doc(&doc, 1, 1);
    memset(&r, 0, sizeof(r));
    ck_assert_int_eq(dab_epg_parse(doc.d, doc.len, &def, collect, &r), 1);
    ck_assert_int_eq(r.p[0].service.sid, 0xD313);
    ck_assert_int_eq(r.p[0].on_air, 0);
    result_free(&r);
} END_TEST

START_TEST(timepointTest) {
    static const uint8_t now[] = { 0, 0, 0, 0 };
    static const uint8_t bad_hour[] = { 0x3b, 0xe1, 0x46, 0x00 }; /* 24:00 */
    time_t t = 1;
    ck_assert_int_eq(dab_epg_decode_timepoint(now, 4, &t), 0);
    ck_assert_int_eq(t, 0);
    ck_assert_int_eq(dab_epg_decode_timepoint(time_20h, 4, &t), 0);
    ck_assert_int_eq(t, DAY + 20 * 3600);
    ck_assert_int_eq(dab_epg_decode_timepoint(bad_hour, 4, &t), -1);
    ck_assert_int_eq(dab_epg_decode_timepoint(time_20h, 3, &t), -1);
} END_TEST

START_TEST(contentIdTest) {
    static const uint8_t data_service[] = { 0x10, 0xE0, 0x12, 0x34, 0x56 };
    static const uint8_t with_ensemble[] = { 0x42, 0xE0, 0x10, 0xC1, 0xD3, 0x12 };
    dab_epg_contentid_t id;
    ck_assert_int_eq(dab_epg_decode_contentid(data_service, 5, &id), 0);
    ck_assert_int_eq(id.sid, 0xE0123456);
    ck_assert_int_eq(dab_epg_decode_contentid(with_ensemble, 6, &id), 0);
    ck_assert_int_eq(id.scids, 2);
    ck_assert_int_eq(id.eid, 0x10C1);
    ck_assert_int_eq(id.sid, 0xD312);
    ck_assert_int_eq(dab_epg_decode_contentid(with_ensemble, 5, &id), -1);
} END_TEST

START_TEST(robustnessTest) {
    size_t len;
    uint8_t *doc = load("input/dab/epg/pi.bin", &len);
    uint8_t *copy = malloc(len);
    result_t r;

    ck_assert_int_eq(dab_epg_parse(doc, 0, NULL, NULL, NULL), -1);
    ck_assert_int_eq(dab_epg_parse((const uint8_t *)"\x03\x00", 2, NULL, NULL, NULL), -1);
    /* truncated documents must not be read beyond their end */
    for (size_t l = 1; l < len; l++) {
        memcpy(copy, doc, l);
        ck_assert_int_le(dab_epg_parse(copy, l, NULL, NULL, NULL), 2);
    }
    /* random corruption */
    srand(42);
    for (int n = 0; n < 2000; n++) {
        memcpy(copy, doc, len);
        for (int k = 0; k < 4; k++)
            copy[rand() % len] = rand();
        memset(&r, 0, sizeof(r));
        dab_epg_parse(copy, len, NULL, collect, &r);
        result_free(&r);
    }
    free(copy);
    free(doc);
} END_TEST

static Suite *dab_epg_suite(void) {
    Suite *s = suite_create("dab_epg");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, referenceDocumentTest);
    tcase_add_test(tc_core, tokensDefaultIdTest);
    tcase_add_test(tc_core, serviceSelectionTest);
    tcase_add_test(tc_core, timepointTest);
    tcase_add_test(tc_core, contentIdTest);
    tcase_add_test(tc_core, robustnessTest);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void) {
    int number_failed;
    SRunner *sr = srunner_create(dab_epg_suite());

    srunner_set_xml(sr, "dab_epgTestResult.xml");
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
