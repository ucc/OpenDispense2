/*
 * OpenDispense2
 *
 * This code is published under the terms of the Acess licence.
 * See the file COPYING for details.
 *
 * doregex.h - Regex Header
 */
#ifndef _DOREGEX_H_
#define _DOREGEX_H_

#include <regex.h>
extern void	CompileRegex(regex_t *Regex, const char *Pattern, int Flags);
extern int	RunRegex(regex_t *regex, const char *string, int nMatches, regmatch_t *matches, const char *errorMessage);

#endif
