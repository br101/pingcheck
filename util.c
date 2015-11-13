#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>

void __attribute__ ((format (printf, 2, 3)))
printlog(int level, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vsyslog(level, format, args);
	va_end(args);

	va_start(args, format);
	vprintf(format, args);
	printf("\n");
	va_end(args);
}
