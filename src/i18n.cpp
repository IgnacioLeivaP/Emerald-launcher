#include "i18n.h"
#include <SDL2/SDL.h>
#include <cstring>
#include <unordered_map>

#ifdef __SWITCH__
#  include <switch.h>
#endif

namespace {

Lang s_lang = LANG_EN;

/* English → Spanish (neutral). Keep the keys exactly as written in the code. */
const char *const ES[][2] = {
    /* Shelf / launcher */
    {"Browse", "Explorar"},
    {"Play", "Jugar"},
    {"Look at box", "Ver caja"},
    {"Settings", "Ajustes"},
    {"Maximize", "Maximizar"},
    {"Version", "Versión"},
    {"Week", "Semana"},
    {"Back", "Atrás"},
    {"Locked", "Bloqueada"},
    {"Turn over", "Girar"},
    {"Drag to spin", "Arrastra para girar"},
    {"Spin", "Girar"},
    {"Put back", "Dejar"},
    {"Navigate", "Navegar"},
    {"Front", "Frente"},
    {"Side", "Lado"},
    {"ONE VERSION", "UNA VERSIÓN"},
    {"%d VERSIONS", "%d VERSIONES"},
    {"%d WEEKS", "%d SEMANAS"},
    {"Versions (%d)", "Versiones (%d)"},
    {"Weeks (%d)", "Semanas (%d)"},
    {"Play %s", "Jugar %s"},
    {"%s %d / %d", "%s %d / %d"},
    {"No screenshots for this version", "Esta versión no tiene capturas"},
    {"Locked - finish %s first", "Bloqueada: termina primero %s"},
    {"Completed - you can replay it any time", "Completada: puedes volver a jugarla cuando quieras"},
    {"Current week - your save carries over", "Semana actual: tu partida continúa"},
    {"In-game: Esc = menu", "En el juego: Esc = menú"},
    {"In-game: L3 = menu", "En el juego: L3 = menú"},
    {"No ROMs found. Place roms + db.json and restart.",
     "No se encontraron ROMs. Copia las roms y db.json y reinicia."},

    /* Box art */
    {"THE ADVENTURE", "LA AVENTURA"},
    {"THIS WEEK", "ESTA SEMANA"},
    {"THIS VERSION", "ESTA VERSIÓN"},
    {"Original", "Original"},

    /* Settings */
    {"Settings - %s", "Ajustes - %s"},
    {"Shader", "Filtro"},
    {"None (sharp pixels)", "Ninguno (píxeles nítidos)"},
    {"Smooth (ScaleFX-9x)", "Suavizado (ScaleFX-9x)"},
    {"Scanlines", "Líneas de escaneo"},
    {"CRT (scanlines + vignette)", "CRT (líneas + viñeta)"},
    {"LCD Grid (handheld)", "Rejilla LCD (portátil)"},
    {"Bloom (glow on brights)", "Resplandor (brillos)"},
    {"Clear Save Data", "Borrar partidas guardadas"},
    {"Launcher view", "Vista del lanzador"},
    {"3D Shelf", "Estante 3D"},
    {"Classic list", "Lista clásica"},
    {"Language", "Idioma"},
    {"Apply", "Aplicar"},
    {"Close", "Cerrar"},
    {"Switch view", "Cambiar vista"},
    {"Change", "Cambiar"},
    {"Delete saves", "Borrar partidas"},
    {"Cancel", "Cancelar"},

    /* Pause menu */
    {"Resume", "Continuar"},
    {"Save state", "Guardar estado"},
    {"Load state", "Cargar estado"},
    {"Take screenshot", "Tomar captura"},
    {"Shader: %s", "Filtro: %s"},
    {"Reset game", "Reiniciar juego"},
    {"Next week", "Siguiente semana"},
    {"Controls", "Controles"},
    {"Return to launcher", "Volver al lanzador"},
    {"Paused", "En pausa"},
    {"Select", "Elegir"},
    {"OK", "Aceptar"},
    {"Yes", "Sí"},
    {"No", "No"},
    {"Saved state", "Estado guardado"},
    {"No saved state yet", "Aún no hay estado guardado"},
    {"This game can't save states", "Este juego no admite estados"},
    {"Saved %s", "Guardado %s"},
    {"just now", "hace un momento"},
    {"%d min ago", "hace %d min"},
    {"%d h ago", "hace %d h"},
    {"%d days ago", "hace %d días"},
    {"yesterday", "ayer"},
    {"Load the saved state? Progress since then will be lost.",
     "¿Cargar el estado guardado? Se perderá el progreso desde entonces."},
    {"Reset the game? Unsaved progress will be lost.",
     "¿Reiniciar el juego? Se perderá el progreso sin guardar."},
    {"Go to week %d? Your save carries over.", "¿Ir a la semana %d? Tu partida continúa."},
    {"Week %d complete!", "¡Semana %d completada!"},
    {"R3: next week", "R3: siguiente semana"},
    {"Tab: next week", "Tab: siguiente semana"},

    {"Performance info: %s", "Info. de rendimiento: %s"},
    {"Performance info", "Info. de rendimiento"},
    {"On", "Sí"},
    {"Off", "No"},
    {"Auto", "Automático"},
    {"View", "Vista"},
    {"SETTINGS", "AJUSTES"},
    {"DISPLAY SHADER  (this game)", "FILTRO DE IMAGEN  (este juego)"},
    {"ALL GAMES", "TODOS LOS JUEGOS"},
    {"Delete ALL saves for this game?", "¿Borrar TODAS las partidas de este juego?"},
    {"This cannot be undone.", "No se puede deshacer."},
    {"Confirm", "Confirmar"},

    /* Toasts */
    {"State saved", "Estado guardado"},
    {"State loaded", "Estado cargado"},
    {"Couldn't save the state", "No se pudo guardar el estado"},
    {"Couldn't load the state", "No se pudo cargar el estado"},
    {"Screenshot saved", "Captura guardada"},
    {"Couldn't save the screenshot", "No se pudo guardar la captura"},
    {"Game reset", "Juego reiniciado"},
    {"Couldn't start %s", "No se pudo iniciar %s"},
    {"Couldn't load the core for this game", "No se pudo cargar el núcleo de este juego"},
    {"Couldn't load the game", "No se pudo cargar el juego"},

    /* Play time */
    {"%s played", "%s jugado"},
    {"< 1 min", "< 1 min"},
    {"%d min", "%d min"},
    {"%d h %d min", "%d h %d min"},
    {"%d h", "%d h"},

    {"Can continue - left %s", "Puedes continuar: lo dejaste %s"},
    {"Has a saved game", "Tiene una partida guardada"},

    /* Continue prompt */
    {"Continue where you left off?", "¿Continuar donde lo dejaste?"},
    {"Continue", "Continuar"},
    {"Start game", "Iniciar juego"},
    {"Loads your in-game save", "Carga tu partida guardada en el juego"},
    {"Left %s", "Lo dejaste %s"},
};

std::unordered_map<std::string, const char *> &table(Lang lang) {
    static std::unordered_map<std::string, const char *> maps[LANG_COUNT];
    static bool built = false;
    if (!built) {
        for (const auto &p : ES) maps[LANG_ES][p[0]] = p[1];
        built = true;
    }
    return maps[lang];
}

} // namespace

Lang i18n_system_lang(void) {
#ifdef __SWITCH__
    Lang lang = LANG_EN;
    if (R_SUCCEEDED(setInitialize())) {
        u64 code = 0;
        if (R_SUCCEEDED(setGetSystemLanguage(&code))) {
            char s[9] = {0};
            memcpy(s, &code, 8);                  /* "es", "es-419"… */
            if (s[0] == 'e' && s[1] == 's') lang = LANG_ES;
        }
        setExit();
    }
    return lang;
#else
    Lang lang = LANG_EN;
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (SDL_Locale *loc = SDL_GetPreferredLocales()) {
        for (SDL_Locale *l = loc; l->language; l++) {
            if (strcmp(l->language, "es") == 0) { lang = LANG_ES; break; }
            if (strcmp(l->language, "en") == 0) break;
        }
        SDL_free(loc);
    }
#endif
    return lang;
#endif
}

void i18n_init(const std::string &pref) {
    if (pref == "es")      s_lang = LANG_ES;
    else if (pref == "en") s_lang = LANG_EN;
    else                   s_lang = i18n_system_lang();
}

Lang i18n_lang(void)          { return s_lang; }
void i18n_set(Lang lang)      { s_lang = (lang >= 0 && lang < LANG_COUNT) ? lang : LANG_EN; }
const char *i18n_code(Lang l) { return l == LANG_ES ? "es" : "en"; }
const char *i18n_name(Lang l) { return l == LANG_ES ? "Español" : "English"; }

const char *tr(const char *english) {
    if (!english || s_lang == LANG_EN) return english;
    auto &m = table(s_lang);
    auto it = m.find(english);
    return it != m.end() ? it->second : english;
}

std::string tr(const std::string &english) { return tr(english.c_str()); }
