#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

#include "log.h"

void __attribute__((format(printf, 2, 3)))
log_out(enum loglevel level, const char* format, ...)
{
	va_list args;

	if (level <= LOG_NOTICE) {
		va_start(args, format);
		vsyslog(level, format, args);
		va_end(args);
	}

	if (level <= LOG_INFO) {
		va_start(args, format);
		vprintf(format, args);
		printf("\n");
		va_end(args);
	}
}

void log_open(const char* name)
{
	openlog(name, LOG_PID | LOG_CONS, LOG_DAEMON);
}

void log_close(void)
{
	closelog();
}
