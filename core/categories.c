#include "categories.h"
#include <string.h>
#include <ctype.h>

static void copy_upper(char *dst, int cap, const char *src) {
    int i = 0; for (; src[i] && i < cap; i++) dst[i] = (char)toupper((unsigned char)src[i]); dst[i] = 0;
}
static void copy_lower(char *dst, int cap, const char *src) {
    int i = 0; for (; src[i] && i < cap; i++) dst[i] = (char)tolower((unsigned char)src[i]); dst[i] = 0;
}
static bool label_ok(const char *s) {
    size_t n = strlen(s); if (n == 0 || n > CAT_LABEL_MAX) return false;
    for (size_t i = 0; i < n; i++) if (!isalnum((unsigned char)s[i])) return false;
    return true;
}
static bool voice_ok(const char *s) {            // letters and single spaces; the recognizer takes plain words
    size_t n = strlen(s); if (n == 0 || n > CAT_VOICE_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        if (!(isalpha((unsigned char)s[i]) || s[i] == ' ')) return false;
        if (s[i] == ' ' && (i == 0 || i == n - 1 || s[i - 1] == ' ')) return false;
    }
    return true;
}
void cats_default(CatTable *t) {
    memset(t, 0, sizeof *t);
    const char *l[] = { "FOOD", "HOBBY", "SCHOOL" }, *v[] = { "food", "hobby", "school" };
    for (int i = 0; i < 3; i++) { t->c[i].used = t->c[i].active = true; copy_upper(t->c[i].label, CAT_LABEL_MAX, l[i]); copy_lower(t->c[i].voice, CAT_VOICE_MAX, v[i]); }
}
int cats_active(const CatTable *t, Category *ids, int cap) {
    int n = 0; for (int i = 0; i < CAT_MISTAKE && n < cap; i++) if (t->c[i].used && t->c[i].active) ids[n++] = (Category)i;
    return n;
}
CatsResult cats_add(CatTable *t, const char *label, const char *voice, Category *id_out) {
    if (!label_ok(label)) return CATS_BAD_LABEL;
    if (!voice_ok(voice)) return CATS_BAD_VOICE;
    Category act[MAX_CATS]; if (cats_active(t, act, MAX_CATS) >= CAT_MAX_ACTIVE) return CATS_FULL;
    char L[CAT_LABEL_MAX + 1], V[CAT_VOICE_MAX + 1]; copy_upper(L, CAT_LABEL_MAX, label); copy_lower(V, CAT_VOICE_MAX, voice);
    if (!strcmp(V, "mistake")) return CATS_DUPLICATE;
    for (int i = 0; i < CAT_MISTAKE; i++)                       // same label or spoken word as a live category
        if (t->c[i].used && t->c[i].active && (!strcmp(t->c[i].label, L) || !strcmp(t->c[i].voice, V))) return CATS_DUPLICATE;
    for (int i = 0; i < CAT_MISTAKE; i++) if (!t->c[i].used) {  // ids are never recycled: archived ones stay "used"
        t->c[i].used = t->c[i].active = true; strcpy(t->c[i].label, L); strcpy(t->c[i].voice, V);
        if (id_out) *id_out = (Category)i;
        return CATS_OK;
    }
    return CATS_NO_ID;
}
bool cats_remove(CatTable *t, Category id) {
    if (id >= CAT_MISTAKE || !t->c[id].used || !t->c[id].active) return false;
    t->c[id].active = false; return true;                       // label kept so old totals still have a name
}
bool cats_is_active(const CatTable *t, Category id) {
    if (id == CAT_MISTAKE) return true;
    return id < CAT_MISTAKE && t->c[id].used && t->c[id].active;
}
const char *cats_label(const CatTable *t, Category id) {
    if (id == CAT_MISTAKE) return "MISTAKE";
    return (id < CAT_MISTAKE && t->c[id].used) ? t->c[id].label : "?";
}
int cats_row_font(int n) { return n <= 3 ? 30 : n == 4 ? 26 : 22; }
