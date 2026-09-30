#pragma once
/* UI language. Strings are written in English in the code and translated by
   looking that text up: tr("Resume") gives "Continuar" in Spanish. Format
   strings work the same way (tr("%d VERSIONS")). Unknown strings are shown
   as they are, so a missing translation never hides anything. */
#include <string>

enum Lang { LANG_EN, LANG_ES, LANG_COUNT };

/* pref: "auto" (system language), "en" or "es". */
void        i18n_init(const std::string &pref);
Lang        i18n_lang(void);
void        i18n_set(Lang lang);
const char *i18n_code(Lang lang);          /* "en", "es" */
const char *i18n_name(Lang lang);          /* "English", "Español" */
const char *tr(const char *english);
std::string tr(const std::string &english);

/* The system's preferred language (falls back to English). */
Lang        i18n_system_lang(void);
