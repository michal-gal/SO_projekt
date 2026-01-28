#ifndef LOG_H
#define LOG_H

// ====== INKLUDY ======
#include <errno.h>  // errno
#include <string.h> // strerror

// ====== LOGOWANIE ======
// Poziomy (compile-time):
// - 0 = tylko podsumowania (LOGI/LOGD/LOGE wyciszone)
// - 1 = tylko podsumowania (jak poprzednie LOG_LEVEL=0)
// - 2 = błędy i podsumowania (jak poprzednie LOG_LEVEL=1)
//
// Format wpisu (prefiks dodawany automatycznie do LOGI/LOGD/LOGE):
//   YYYY-MM-DD HH:MM:SS pid=<pid> <LEVEL> <wiadomość>
// gdzie LEVEL to: I/D/E.
//
// Log do pliku (runtime):
// - domyślnie (bez zmiennych środowiskowych) tworzy plik:
//     restauracja_YYYY-MM-DD_HH-MM-SS.log
// - albo ustaw jawnie: RESTAURACJA_LOG_FILE=/ścieżka/do/pliku.log
// - wyłącz duplikację na stdout/stderr: RESTAURACJA_LOG_STDIO=0
//
// Uwaga: logger jest współdzielony przez wszystkie procesy i używa O_APPEND + write(),
// co działa sensownie w wieloprocesowym środowisku.
#ifndef LOG_LEVEL
#define LOG_LEVEL 1
#endif

extern int current_log_level;

// Logger zapisuje do pliku, jeśli ustawisz zmienną środowiskową:
//   RESTAURACJA_LOG_FILE=/tmp/restauracja.log
// Domyślnie logi nadal trafiają też na stdout/stderr. Żeby to wyłączyć:
//   RESTAURACJA_LOG_STDIO=0
void log_init_from_env(void);
void log_printf(char level, const char *fmt, ...);
void log_printf_force_stdio(char level, const char *fmt, ...);

// ====== MAKRA LOGOWANIA ======
#define LOGI(...)                         \
    do                                    \
    {                                     \
        if (current_log_level >= 3)       \
            log_printf('I', __VA_ARGS__); \
    } while (0)

#define LOGD(...)                         \
    do                                    \
    {                                     \
        if (current_log_level >= 3)       \
            log_printf('D', __VA_ARGS__); \
    } while (0)

#define LOGE(...)                         \
    do                                    \
    {                                     \
        if (current_log_level >= 2)       \
            log_printf('E', __VA_ARGS__); \
    } while (0)

// Podsumowania/komunikaty krytyczne: drukuj zawsze, niezależnie od LOG_LEVEL i RESTAURACJA_LOG_STDIO.
#define LOGS(...)                                 \
    do                                            \
    {                                             \
        log_printf_force_stdio('I', __VA_ARGS__); \
    } while (0)

// Ważne zdarzenia procesowe (widoczne w LOG_LEVEL >= 1)
#define LOGP(...)                         \
    do                                    \
    {                                     \
        if (current_log_level >= 1)       \
            log_printf('P', __VA_ARGS__); \
    } while (0)

#define LOGE_ERRNO(prefix)                           \
    do                                               \
    {                                                \
        LOGE("%s: %s\n", (prefix), strerror(errno)); \
    } while (0)

#endif
