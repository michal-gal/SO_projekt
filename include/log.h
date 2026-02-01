#ifndef LOG_H
#define LOG_H

// ====== INKLUDY ======
#include <errno.h>  // errno
#include <string.h> // strerror

// ====== LOGOWANIE ======
// Poziomy (w trakcie działania, LOG_LEVEL):
// - 0 = tylko podsumowania (LOGS)
// - 1 = LOGP + LOGE + LOGS
// - 2 = LOGI + LOGP + LOGE + LOGS
// - 3 = LOGD + LOGI + LOGP + LOGE + LOGS
//
// Format wpisu (prefiks dodawany automatycznie do LOGI/LOGD/LOGE):
//   YYYY-MM-DD HH:MM:SS pid=<pid> <LEVEL> <wiadomość>
// gdzie LEVEL to: I/D/E.
//
// Log do pliku (w trakcie działania):
// - domyślnie (bez zmiennych środowiskowych) tworzy plik:
//     restauracja_YYYY-MM-DD_HH-MM-SS.log
// - albo ustaw jawnie: RESTAURACJA_LOG_FILE=/ścieżka/do/pliku.log
// - wyłącz duplikację na stdout/stderr: RESTAURACJA_LOG_STDIO=0
// - konsola zawsze tylko „podstawowe” logi (LOGS i ewentualnie LOGP)
// - plik zawiera rosnący zakres w zależności od LOG_LEVEL
//
// Uwaga: logger jest współdzielony przez wszystkie procesy i używa O_APPEND
// oraz write(), co działa poprawnie w środowisku wieloprocesowym.
#ifndef LOG_LEVEL
#define LOG_LEVEL 1
#endif

extern int current_log_level;

// Moduł logowania zapisuje do pliku, jeśli ustawisz zmienną środowiskową:
//   RESTAURACJA_LOG_FILE=/tmp/restauracja.log
// Domyślnie logi nadal trafiają też na stdout/stderr. Żeby to wyłączyć:
//   RESTAURACJA_LOG_STDIO=0
void inicjuj_log_z_env(void);
void loguj(char level, const char *fmt, ...);
void loguj_wymus_stdio(char level, const char *fmt, ...);
void loguj_blokiem(char level, const char *buf);

// ====== MAKRA LOGOWANIA ======
#define LOGI(...)               \
  do                            \
  {                             \
    if (current_log_level >= 2) \
      loguj('I', __VA_ARGS__);  \
  } while (0)

#define LOGD(...)               \
  do                            \
  {                             \
    if (current_log_level >= 3) \
      loguj('D', __VA_ARGS__);  \
  } while (0)

#define LOGE(...)               \
  do                            \
  {                             \
    if (current_log_level >= 1) \
      loguj('E', __VA_ARGS__);  \
  } while (0)

// Podsumowania/komunikaty krytyczne: drukuj zawsze, niezależnie od LOG_LEVEL i
// RESTAURACJA_LOG_STDIO.
#define LOGS(...)                        \
  do                                     \
  {                                      \
    loguj_wymus_stdio('I', __VA_ARGS__); \
  } while (0)

// Ważne zdarzenia procesowe (widoczne w LOG_LEVEL >= 1)
#define LOGP(...)               \
  do                            \
  {                             \
    if (current_log_level >= 1) \
      loguj('P', __VA_ARGS__);  \
  } while (0)

#define LOGE_ERRNO(prefix)                       \
  do                                             \
  {                                              \
    LOGE("%s: %s\n", (prefix), strerror(errno)); \
  } while (0)

#endif
