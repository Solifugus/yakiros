/*
 * log.c - YakirOS logging system implementation
 *
 * Centralized logging for the graph resolver system.
 * Uses /dev/kmsg for early boot logging before syslog is available.
 */

#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

#define LOG_PATH "/dev/kmsg"   /* Early logging before syslog */
#define MAX_LOG_LINE 1024

static int log_fd = -1;

void log_open(void) {
    log_fd = open(LOG_PATH, O_WRONLY | O_APPEND);
    if (log_fd < 0) {
        /* Fallback to stderr if /dev/kmsg unavailable */
        log_fd = STDERR_FILENO;
    }
}

void graph_log(const char *level, const char *fmt, ...) {
    va_list ap;
    char buf[MAX_LOG_LINE];
    struct timeval tv;
    int n;

    /* Get timestamp */
    gettimeofday(&tv, NULL);

    /* Format: [timestamp] graph-resolver <level> message */
    n = snprintf(buf, sizeof(buf), "[%5ld.%03ld] graph-resolver <%s> ",
                 tv.tv_sec % 100000, tv.tv_usec / 1000, level);

    /* Add the actual message */
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);

    /* Ensure newline termination */
    if (n > 0 && n < (int)sizeof(buf) - 1 && buf[n - 1] != '\n') {
        buf[n++] = '\n';
    }

    /* Write to log destination */
    (void)!write(log_fd, buf, n);
}