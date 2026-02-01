#include "log.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int current_log_level = LOG_LEVEL;

/* Kontekst loggera w module, aby uniknąć rozproszonych zmiennych statycznych. */
struct LogCtx
{
    int log_fd;
    int log_inited;
    int log_stdio_enabled;
};

static struct LogCtx log_ctx_storage = {.log_fd = -1,
                                        .log_inited = 0,
                                        .log_stdio_enabled = 1};
static struct LogCtx *log_ctx = &log_ctx_storage;

static const char *
domyslna_sciezka_logu(void) // generuje domyślną ścieżkę do pliku logu
{
    static char path[256];
    if (path[0] != '\0')
        return path;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    // logs/restauracja_YYYY-MM-DD_HH-MM-SS.log
    (void)snprintf(path, sizeof(path),
                   "logs/restauracja_%04d-%02d-%02d_%02d-%02d-%02d.log",
                   tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                   tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

    return path;
}

static void
zamknij_log_przy_wyjsciu(void) // zamyka plik logu przy zakończeniu programu
{
    if (log_ctx->log_fd >= 0)
    {
        (void)close(log_ctx->log_fd);
        log_ctx->log_fd = -1;
    }
}

static void inicjuj_log_raz(void) // inicjalizuje logowanie tylko raz
{
    if (log_ctx->log_inited)
        return;
    log_ctx->log_inited = 1;

    const char *stdio_env = getenv("RESTAURACJA_LOG_STDIO");
    if (stdio_env && stdio_env[0] == '0' && stdio_env[1] == '\0')
        log_ctx->log_stdio_enabled = 0;

    const char *path = getenv("RESTAURACJA_LOG_FILE");
    if (!path || !*path)
        path = getenv("LOG_FILE");
    if (!path || !*path)
    {
        path = domyslna_sciezka_logu();
        // Ustaw zmienną środowiskową dla fork/exec potomków, aby używały
        // tego samego pliku.
        (void)setenv("RESTAURACJA_LOG_FILE", path, 0);
    }

    if (path && strncmp(path, "logs/", 5) == 0)
        (void)mkdir("logs", 0755);

    int flags = O_WRONLY | O_CREAT | O_APPEND;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int fd = open(path, flags, 0644);
    if (fd >= 0)
    {
#ifndef O_CLOEXEC
        // Awaryjnie: ustaw FD_CLOEXEC, żeby nie przeciekał do potomków.
        int old_flags = fcntl(fd, F_GETFD);
        if (old_flags != -1)
            (void)fcntl(fd, F_SETFD, old_flags | FD_CLOEXEC);
#endif
    }
    if (fd >= 0)
    {
        log_ctx->log_fd = fd;
        atexit(zamknij_log_przy_wyjsciu);
    }
}

void inicjuj_log_z_env(
    void) // inicjalizuje logowanie na podstawie zmiennych środowiskowych
{
    // Czytaj LOG_LEVEL z env
    const char *log_level_env = getenv("LOG_LEVEL");
    if (log_level_env)
    {
        errno = 0;
        char *end = NULL;
        long val = strtol(log_level_env, &end, 10);
        if (errno == 0 && end && *end == '\0' && val >= 0 && val <= 2)
        {
            current_log_level = (int)val;
        }
    }

    inicjuj_log_raz();
}

// Wspólna funkcja logowania z va_list
static void loguj_vprintf(char level, const char *fmt, va_list ap,
                          int force_stdio)
{
    char msg[3072];
    int n = vsnprintf(msg, sizeof(msg), fmt, ap);

    if (n <= 0)
        return;

    size_t msg_len = (size_t)n;
    if (msg_len >= sizeof(msg))
        msg_len = sizeof(msg) - 1;

    // Prefiks: YYYY-MM-DD HH:MM:SS pid=1234 L
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char prefix[128];
    int pn = snprintf(
        prefix, sizeof(prefix), "%04d-%02d-%02d %02d:%02d:%02d pid=%d %c ",
        tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday, tm_now.tm_hour,
        tm_now.tm_min, tm_now.tm_sec, (int)getpid(), level);
    size_t prefix_len = (pn > 0) ? (size_t)pn : 0;
    if (prefix_len >= sizeof(prefix))
        prefix_len = sizeof(prefix) - 1;

    size_t leading_nl = 0;
    if (msg_len > 0 && msg[0] == '\n')
        leading_nl = 1;

    char out[4096];
    size_t out_len = 0;
    if (leading_nl)
        out[out_len++] = '\n';

    size_t want = prefix_len;
    if (out_len + want > sizeof(out))
        want = sizeof(out) - out_len;
    if (want)
    {
        memcpy(out + out_len, prefix, want);
        out_len += want;
    }

    const char *msg_body = msg + leading_nl;
    size_t msg_body_len = msg_len - leading_nl;
    want = msg_body_len;
    if (out_len + want > sizeof(out))
        want = sizeof(out) - out_len;
    if (want)
    {
        memcpy(out + out_len, msg_body, want);
        out_len += want;
    }

    /* Polityka pliku: więcej logów przy wyższym LOG_LEVEL. */
    int write_file = force_stdio;
    if (!write_file)
    {
        if (level == 'D')
            write_file = (current_log_level >= 3);
        else if (level == 'I')
            write_file = (current_log_level >= 2);
        else if (level == 'P')
            write_file = (current_log_level >= 1);
        else if (level == 'E')
            write_file = (current_log_level >= 1);
        else
            write_file = 1;
    }

    if (write_file && log_ctx->log_fd >= 0)
        (void)write(log_ctx->log_fd, out, out_len);

    /*
     * Polityka konsoli:
     * - Gdy `force_stdio` (LOGS), zawsze na konsolę (stderr dla 'E').
     * - W pozostałych przypadkach tylko LOGP, jeśli włączono w env.
     */
    if (force_stdio)
    {
        (void)write((level == 'E') ? STDERR_FILENO : STDOUT_FILENO, out, out_len);
    }
    else if (level == 'P' && log_ctx->log_stdio_enabled)
    {
        (void)write(STDOUT_FILENO, out, out_len);
    }
}

void loguj(char level, const char *fmt,
           ...) // loguje komunikat z danym poziomem
{
    inicjuj_log_raz();

    va_list ap;
    va_start(ap, fmt);
    loguj_vprintf(level, fmt, ap, 0);
    va_end(ap);
}

void loguj_wymus_stdio(char level, const char *fmt,
                       ...) // loguje zawsze także na stdout/stderr
{
    inicjuj_log_raz();

    va_list ap;
    va_start(ap, fmt);
    loguj_vprintf(level, fmt, ap, 1);
    va_end(ap);
}

void loguj_blokiem(char level, const char *buf) // loguje blokiem jednym zapisem
{
    inicjuj_log_raz();

    if (!buf)
        return;

    size_t buf_len = strlen(buf);
    if (buf_len == 0)
        return;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char prefix[128];
    int pn = snprintf(
        prefix, sizeof(prefix), "%04d-%02d-%02d %02d:%02d:%02d pid=%d %c ",
        tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday, tm_now.tm_hour,
        tm_now.tm_min, tm_now.tm_sec, (int)getpid(), level);
    size_t prefix_len = (pn > 0) ? (size_t)pn : 0;
    if (prefix_len >= sizeof(prefix))
        prefix_len = sizeof(prefix) - 1;

    size_t leading_nl = (buf[0] == '\n') ? 1 : 0;
    const char *body = buf + leading_nl;
    size_t body_len = buf_len - leading_nl;

    size_t out_len = leading_nl + prefix_len + body_len;
    char *out = (char *)malloc(out_len + 1);
    if (!out)
        return;

    size_t pos = 0;
    if (leading_nl)
        out[pos++] = '\n';
    if (prefix_len)
    {
        memcpy(out + pos, prefix, prefix_len);
        pos += prefix_len;
    }
    if (body_len)
    {
        memcpy(out + pos, body, body_len);
        pos += body_len;
    }

    if (log_ctx->log_fd >= 0)
        (void)write(log_ctx->log_fd, out, pos);

    (void)write((level == 'E') ? STDERR_FILENO : STDOUT_FILENO, out, pos);

    free(out);
}
