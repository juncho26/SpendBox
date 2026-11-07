// User-editable spending categories. Removing one archives it: history keeps its totals and name.
#ifndef CATEGORIES_H
#define CATEGORIES_H
#include "ledger.h"

#define CAT_MAX_ACTIVE  5     // what fits readably on the 200x200 list (see cats_row_font)
#define CAT_LABEL_MAX   8     // device label, shown upper-case
#define CAT_VOICE_MAX  23     // how it is spoken, letters and spaces

typedef struct { bool used, active, cash; char label[CAT_LABEL_MAX + 1]; char voice[CAT_VOICE_MAX + 1]; } CatDef;   // cash: paid in cash only, never uses card credit
typedef struct { CatDef c[MAX_CATS]; } CatTable;

typedef enum { CATS_OK = 0, CATS_FULL, CATS_NO_ID, CATS_BAD_LABEL, CATS_BAD_VOICE, CATS_DUPLICATE } CatsResult;

void       cats_default(CatTable *t);                                   // Food, Hobby, School
CatsResult cats_add(CatTable *t, const char *label, const char *voice, Category *id_out);
bool       cats_remove(CatTable *t, Category id);                       // false for mistake / unknown / already removed
int        cats_active(const CatTable *t, Category *ids, int cap);      // active user categories in id order
bool       cats_is_active(const CatTable *t, Category id);              // mistake counts as valid
const char *cats_label(const CatTable *t, Category id);                 // works for archived ids too ("MISTAKE" for 15)
int        cats_row_font(int n_rows);                                   // list font px so n rows fit the screen
#endif
