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
bool	gbSyslogEnabled = true;

// === CODE ==
void Log_Error(const char *Format, ...)
{
	va_list	args;

	va_start(args, Format);
	if( gbSyslogEnabled )
	{
		vsyslog(LOG_WARNING, Format, args);
	}
	else
	{
		fprintf(stderr, "WARNING: ");
		vfprintf(stderr, Format, args);
		fprintf(stderr, "\n");
	}
	va_end(args);
}

void Log_Info(const char *Format, ...)
{
	va_list	args;
	
	va_start(args, Format);
	if( gbSyslogEnabled )
	{
		vsyslog(LOG_INFO, Format, args);
	}
	else
	{
		fprintf(stderr, "WARNING: ");
		vfprintf(stderr, Format, args);
		fprintf(stderr, "\n");
	}
	va_end(args);
}

