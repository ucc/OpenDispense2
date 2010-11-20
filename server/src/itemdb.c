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
	
	// Error check
	if(!fp) {
		fprintf(stderr, "Unable to open item file '%s'\n", gsItemListFile);
		perror("Unable to open item file");
	}
	
	while( fgets(buffer, BUFSIZ, fp) )
	{
		char	*tmp;
		char	*type, *num, *price, *desc;
		
		lineNum ++;

		// Remove comments
		tmp = strchr(buffer, '#');
		if(tmp)	*tmp = '\0';
		tmp = strchr(buffer, ';');
		if(tmp)	*tmp = '\0';
		
		// Trim whitespace
		line = trim(buffer);
		
		// Parse Line
		// - Type
		type = line;
		// - Number
		num = strchr(type, ' ');
		if(num) {
			while(*num == ' ' || *num == '\t')	num ++;
		}
		else {
			fprintf(stderr, "Syntax error on line %i of item file\n", lineNum);
			continue;
		}
		// - Price
		price = strchr(num, ' ');
		if( price ) {
			while(*num == ' ' || *num == '\t')	num ++;
		}
		else {
			fprintf(stderr, "Syntax error on line %i of item file\n", lineNum);
			continue;
		}
		// - Name/Description
		desc = strchr(price, ' ');
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
