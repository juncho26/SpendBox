#include "hal.h"
void hal_voice_set_commands(const CatTable *t) { (void)t; }
void hal_voice_start(void) {}
void hal_voice_cancel(void) {}
bool hal_voice_finish(Category *out) { (void)out; return false; }   // "not heard" -> the picker opens, choose with the buttons
