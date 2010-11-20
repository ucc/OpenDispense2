/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * itemdb.c - Dispense Item Databse
 *
 * This file is licenced under the 3-clause BSD Licence. See the file COPYING
 * for full details.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "common.h"
#include <regex.h>

// === GLOBALS ===
 int	giNumItems = 0;
tItem	*gaItems = NULL;
tHandler	*gaHandlers = NULL;
char	*gsItemListFile = DEFAULT_ITEM_FILE;

// === PROTOTYPES ===
void	Load_Itemlist(void);
char	*trim(char *__str);

// === CODE ===
/**
 * \brief Read the item list from disk
 */
void Load_Itemlist(void)
{
	FILE	*fp = fopen(gsItemListFile, "r");
	char	buffer[BUFSIZ];
	char	*line;
	 int	lineNum = 0;
	 int	i;
	regex_t	regex;
	regmatch_t	matches[5];
	
	i = regcomp(&regex, "^([a-zA-Z][a-zA-Z0-9]*)\\s+([0-9]+)\\s+([0-9]+)\\s+(.*)", REG_EXTENDED);
	//i = regcomp(&regex, "\\(\\d+\\)", 0);//\\s+([0-9]+)\\s+([0-9]+)\\s+(.*)", 0);
	if( i )
	{
		size_t	len = regerror(i, &regex, NULL, 0);
		char	*errorStr = malloc(len);
		regerror(i, &regex, errorStr, len);
		fprintf(stderr, "Rexex compilation failed - %s\n", errorStr);
		free(errorStr);
		exit(-1);
	}

	// Error check
	if(!fp) {
		fprintf(stderr, "Unable to open item file '%s'\n", gsItemListFile);
		perror("Unable to open item file");
	}
	
	while( fgets(buffer, BUFSIZ, fp) )
	{
		char	*tmp;
		char	*type, *desc;
		 int	num, price;

		lineNum ++;

		// Remove comments
		tmp = strchr(buffer, '#');
		if(tmp)	*tmp = '\0';
		tmp = strchr(buffer, ';');
		if(tmp)	*tmp = '\0';
		
		// Trim whitespace
		line = trim(buffer);
		
		if(strlen(line) == 0)	continue;
		
		// Pass regex over line
		if( (i = regexec(&regex, line, 5, matches, 0)) ) {
			size_t  len = regerror(i, &regex, NULL, 0);
			char    *errorStr = malloc(len);
			regerror(i, &regex, errorStr, len);
			fprintf(stderr, "Syntax error on line %i of item file '%s'\n%s", lineNum, gsItemListFile, errorStr);
			free(errorStr);
			exit(-1);
		}

		// Read line data
		type  = line + matches[1].rm_so;	line[ matches[1].rm_eo ] = '\0';
		num   = atoi( line + matches[2].rm_so );
		price = atoi( line + matches[3].rm_so );
		desc  = line + matches[4].rm_so;
		

		printf("Item '%s' - %i cents, %s:%i\n", desc, price, type, num);
	}	
}

char *trim(char *__str)
{
	char	*ret;
	 int	i;
	
	while( isspace(*__str) )
		__str++;
	ret = __str;

	i = strlen(ret);
	while( i-- && isspace(__str[i]) ) {
		__str[i] = '\0';
	}

	return ret;
}
