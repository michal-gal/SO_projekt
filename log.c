#include "log.h"

#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int log_fd = -1;
static int log_inited = 0;
static int log_stdio_enabled = 1;

static const char *log_default_path(void) // generuje domyślną ścieżkę do pliku logu
{
    static char path[256];
    if (path[0] != '\0')
        return path;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    // restauracja_YYYY-MM-DD_HH-MM-SS.log
    (void)snprintf(path, sizeof(path),
                   "restauracja_%04d-%02d-%02d_%02d-%02d-%02d.log",
                   tm_now.tm_year + 1900,
                   tm_now.tm_mon + 1,
                   tm_now.tm_mday,
                   tm_now.tm_hour,
                   tm_now.tm_min,
                   tm_now.tm_sec);

    return path;
}

static void log_close_at_exit(void) // zamyka plik logu przy zakończeniu programu
{
    if (log_fd >= 0)
    {
        (void)close(log_fd);
        log_fd = -1;
    }
}

static void log_init_once(void) // inicjalizuje logowanie tylko raz
{
    if (log_inited)
        return;
    log_inited = 1;

    const char *stdio_env = getenv("RESTAURACJA_LOG_STDIO");
    if (stdio_env && stdio_env[0] == '0' && stdio_env[1] == '\0')
        log_stdio_enabled = 0;

    const char *path = getenv("RESTAURACJA_LOG_FILE");
    if (!path || !*path)
        path = getenv("LOG_FILE");
    if (!path || !*path)
    {
        path = log_default_path();
        // Export for fork/exec children so they use the same file.
        (void)setenv("RESTAURACJA_LOG_FILE", path, 0);
    }

    int flags = O_WRONLY | O_CREAT | O_APPEND;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int fd = open(path, flags, 0644);
    if (fd >= 0)
    {
#ifndef O_CLOEXEC
        // Best-effort fallback: ensure the FD is closed on exec() to avoid leaking it to children.
        int old_flags = fcntl(fd, F_GETFD);
        if (old_flags != -1)
            (void)fcntl(fd, F_SETFD, old_flags | FD_CLOEXEC);
#endif
    }
    if (fd >= 0)
    {
        log_fd = fd;
        atexit(log_close_at_exit);
    }
}

void log_init_from_env(void) // inicjalizuje logowanie na podstawie zmiennych środowiskowych
{
    log_init_once();
}

void log_printf(char level, const char *fmt, ...) // loguje komunikat z danym poziomem
{
    log_init_once();

    char msg[3072];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

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
    int pn = snprintf(prefix, sizeof(prefix),
                      "%04d-%02d-%02d %02d:%02d:%02d pid=%d %c ",
                      tm_now.tm_year + 1900,
                      tm_now.tm_mon + 1,
                      tm_now.tm_mday,
                      tm_now.tm_hour,
                      tm_now.tm_min,
                      tm_now.tm_sec,
                      (int)getpid(),
                      level);
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

    if (log_fd >= 0)
        (void)write(log_fd, out, out_len);

    if (log_stdio_enabled)
        (void)write((level == 'E') ? STDERR_FILENO : STDOUT_FILENO, out, out_len);
}

void log_printf_force_stdio(char level, const char *fmt, ...) // loguje zawsze także na stdout/stderr
{
    log_init_once();

    char msg[3072];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (n <= 0)
        return;

    size_t msg_len = (size_t)n;
    if (msg_len >= sizeof(msg))
        msg_len = sizeof(msg) - 1;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char prefix[128];
    int pn = snprintf(prefix, sizeof(prefix),
                      "%04d-%02d-%02d %02d:%02d:%02d pid=%d %c ",
                      tm_now.tm_year + 1900,
                      tm_now.tm_mon + 1,
                      tm_now.tm_mday,
                      tm_now.tm_hour,
                      tm_now.tm_min,
                      tm_now.tm_sec,
                      (int)getpid(),
                      level);
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

    if (log_fd >= 0)
        (void)write(log_fd, out, out_len);

    (void)write((level == 'E') ? STDERR_FILENO : STDOUT_FILENO, out, out_len);
}
