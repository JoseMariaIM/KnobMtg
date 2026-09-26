/* lang.c's translation tables: nothing in the build today enforces
 * that strings_en[] and strings_es[] both cover every string_id_t -
 * a missing entry doesn't fail to compile (C array designated
 * initializers default absent slots to NULL), it just silently shows
 * English on that one string in the Spanish UI (see t()'s fallback).
 * This makes the hole visible in CI instead of a bug report. */
#include "../../knobby/src/adapters/lang.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void)
{
    int id;
    int missing_en = 0, missing_es = 0;

    for (id = 0; id < STR_COUNT; id++) {
        const char *en = lang_test_raw(LANG_EN, (string_id_t)id);
        const char *es = lang_test_raw(LANG_ES, (string_id_t)id);
        if (en == NULL || en[0] == '\0') {
            fprintf(stderr, "  missing EN string for id %d\n", id);
            missing_en++;
        }
        if (es == NULL || es[0] == '\0') {
            fprintf(stderr, "  missing ES string for id %d\n", id);
            missing_es++;
        }
    }
    assert(missing_en == 0);
    printf("PASS: every one of the %d string ids has an English entry\n", STR_COUNT);
    assert(missing_es == 0);
    printf("PASS: every one of the %d string ids has a Spanish entry\n", STR_COUNT);

    /* A format string (has a '%') must carry the same specifiers in
       both languages, or the corresponding snprintf() call is a type
       mismatch waiting to happen the moment someone edits one but not
       the other. Doesn't police the surrounding text, just the count
       and order of '%' introducers. */
    for (id = 0; id < STR_COUNT; id++) {
        const char *en = lang_test_raw(LANG_EN, (string_id_t)id);
        const char *es = lang_test_raw(LANG_ES, (string_id_t)id);
        int en_pct = 0, es_pct = 0;
        const char *p;
        if (en == NULL || es == NULL) continue;
        for (p = en; *p; p++) if (*p == '%') en_pct++;
        for (p = es; *p; p++) if (*p == '%') es_pct++;
        if (en_pct != es_pct) {
            fprintf(stderr, "  id %d: %%-count mismatch (en=%d es=%d): \"%s\" / \"%s\"\n",
                    id, en_pct, es_pct, en, es);
        }
        assert(en_pct == es_pct);
    }
    printf("PASS: every format string has matching %%-specifier counts across languages\n");

    printf("\nAll lang completeness tests passed.\n");
    return 0;
}
