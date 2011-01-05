/*
 * OpenDispense2
 *
 * logging.c - Debug/Logging Routines
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "common.h"

// === CODE ==
void Log_Error(const char *Format, ...)
{
	va_list	args;

	va_start(args, Format);
	fprintf(stderr, "Error: ");
	vfprintf(stderr, Format, args);
	fprintf(stderr, "\n");
	va_end(args);
}

void Log_Info(const char *Format, ...)
{
	va_list	args;
	
	va_start(args, Format);
	printf("Info : ");
	vprintf(Format, args);
	printf("\n");
	va_end(args);
}

