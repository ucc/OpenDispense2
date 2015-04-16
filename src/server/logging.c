/*
 * OpenDispense2
 *
 * logging.c - Debug/Logging Routines
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include "common.h"
#include <syslog.h>

// === GLOBALS ===
bool	gbSyslogDisabled = true;

// === CODE ==
void Log_Error(const char *Format, ...)
{
	va_list	args;

	if( !gbSyslogDisabled )
	{
		va_start(args, Format);
		vsyslog(LOG_WARNING, Format, args);
		va_end(args);
	}
	
	va_start(args, Format);
	fprintf(stderr, "WARNING: ");
	vfprintf(stderr, Format, args);
	fprintf(stderr, "\n");
	va_end(args);
}

void Log_Info(const char *Format, ...)
{
	va_list	args;
	
	if( !gbSyslogDisabled )
	{
		va_start(args, Format);
		vsyslog(LOG_INFO, Format, args);
		va_end(args);
	}
	va_start(args, Format);
	fprintf(stderr, "INFO: ");
	vfprintf(stderr, Format, args);
	fprintf(stderr, "\n");
	va_end(args);
}

