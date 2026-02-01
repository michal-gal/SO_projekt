#include "log.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int current_log_level = LOG_LEVEL;

/* Per-module logger context to avoid scattered static globals. */
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
log_default_path(void) // generuje domyślną ścieżkę do pliku logu
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
log_close_at_exit(void) // zamyka plik logu przy zakończeniu programu
{
    if (log_ctx->log_fd >= 0)
    {
        (void)close(log_ctx->log_fd);
        log_ctx->log_fd = -1;
    }
}

static void log_init_once(void) // inicjalizuje logowanie tylko raz
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
        path = log_default_path();
        // Ustaw zmienną środowiskową dla fork/exec potomków, aby używały tego
        // samego pliku.
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
        // Fallback: postaraj się ustawić FD_CLOEXEC, żeby nie przeciekał do
        // procesów potomnych.
        int old_flags = fcntl(fd, F_GETFD);
        if (old_flags != -1)
            (void)fcntl(fd, F_SETFD, old_flags | FD_CLOEXEC);
#endif
    }
    if (fd >= 0)
    {
        log_ctx->log_fd = fd;
        atexit(log_close_at_exit);
    }
}

void log_init_from_env(
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

    log_init_once();
}

// Wspólna funkcja logowania z va_list
static void log_vprintf(char level, const char *fmt, va_list ap,
                        int force_stdio)
{
    char msg[3072];
    int n = vsnprintf(msg, sizeof(msg), fmt, ap);

    if (n <= 0)
        return;

    size_t msg_len = (size_t)n;
    if (msg_len >= sizeof(msg))
        msg_len = sizeof(msg) - 1;

    // Prefix: YYYY-MM-DD HH:MM:SS pid=1234 L
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

    /* File policy: include more logs as LOG_LEVEL increases. */
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
     * Console policy:
     * - If `force_stdio` is set (used by LOGS), always print to console
     *   (stderr for 'E', stdout otherwise).
     * - Otherwise, print to console only for LOGP ('P') when enabled by env.
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

void log_printf(char level, const char *fmt,
                ...) // loguje komunikat z danym poziomem
{
    log_init_once();

    va_list ap;
    va_start(ap, fmt);
    log_vprintf(level, fmt, ap, 0);
    va_end(ap);
}

void log_printf_force_stdio(char level, const char *fmt,
                            ...) // loguje zawsze także na stdout/stderr
{
    log_init_once();

    va_list ap;
    va_start(ap, fmt);
    log_vprintf(level, fmt, ap, 1);
    va_end(ap);
}
