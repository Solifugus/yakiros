/*
 * log.h - YakirOS logging system
 *
 * Provides centralized logging for the graph resolver and its modules.
 * Writes to /dev/kmsg when available (early boot), falls back to stderr.
 */

#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

/* Initialize the logging system */
void log_open(void);

/* Core logging function - use macros below instead */
void graph_log(const char *level, const char *fmt, ...);

/* Logging macros for different severity levels */
#define LOG_INFO(...)  graph_log("INFO",  __VA_ARGS__)
#define LOG_WARN(...)  graph_log("WARN",  __VA_ARGS__)
#define LOG_ERR(...)   graph_log("ERROR", __VA_ARGS__)

#endif /* LOG_H */