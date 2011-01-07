/*
 * OpenDispense2
 *
 * logging.c - Debug/Logging Routines
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "common.h"
#include <syslog.h>

// === CODE ==
void Log_Error(const char *Format, ...)
{
	va_list	args;

	va_start(args, Format);
	vsyslog(LOG_WARNING, Format, args);
	va_end(args);
}

void Log_Info(const char *Format, ...)
{
	va_list	args;
	
	va_start(args, Format);
	vsyslog(LOG_INFO, Format, args);
	va_end(args);
}

