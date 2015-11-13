/* pingcheck - Check connectivity of interfaces in OpenWRT
 *
 * Copyright (C) 2015 Bruno Randolf <br1@einfach.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
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
