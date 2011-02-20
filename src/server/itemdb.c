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
#include <sys/inotify.h>
#include <signal.h>
#include <sys/fcntl.h>
#include <unistd.h>

// === IMPORTS ===
extern tHandler	gCoke_Handler;
extern tHandler	gSnack_Handler;
extern tHandler	gDoor_Handler;

// === PROTOTYPES ===
void	Init_Handlers(void);
void	ItemList_Changed(int signum);
void	Load_Itemlist(void);
char	*trim(char *__str);

// === GLOBALS ===
 int	giNumItems = 0;
tItem	*gaItems = NULL;
tHandler	gPseudo_Handler = {Name:"pseudo"};
tHandler	gSpacer_Handler = {Name:"spacer"};
tHandler	*gaHandlers[] = {&gSpacer_Handler, &gPseudo_Handler, &gCoke_Handler, &gSnack_Handler, &gDoor_Handler};
 int	giNumHandlers = sizeof(gaHandlers)/sizeof(gaHandlers[0]);
char	*gsItemListFile = DEFAULT_ITEM_FILE;
#if USE_INOTIFY
 int	giItem_INotifyFD;
#endif

// === CODE ===
void Init_Handlers()
{
	 int	i;
	for( i = 0; i < giNumHandlers; i ++ )
	{
		if( gaHandlers[i]->Init )
			gaHandlers[i]->Init(0, NULL);	// TODO: Arguments
	}
	
	// Use inotify to watch the snack config file
	#if USE_INOTIFY
	{
		int oflags;
		
		giItem_INotifyFD = inotify_init();
		inotify_add_watch(giItem_INotifyFD, gsItemListFile, IN_MODIFY);
		
		// Handle SIGIO
		signal(SIGIO, &ItemList_Changed);
		
		// Fire SIGIO when data is ready on the FD
		fcntl(giItem_INotifyFD, F_SETOWN, getpid());
		oflags = fcntl(giItem_INotifyFD, F_GETFL);
		fcntl(giItem_INotifyFD, F_SETFL, oflags | FASYNC);
	}
	#endif
}

#if USE_INOTIFY
void ItemList_Changed(int signum)
{
	char	buf[512];
	read(giItem_INotifyFD, buf, 512);
	Load_Itemlist();
	
	signum = 0;	// Shut up GCC
}
#endif

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
	
	i = regcomp(&regex, "^-?([a-zA-Z][a-zA-Z]*)\\s+([0-9]+)\\s+([0-9]+)\\s+(.*)", REG_EXTENDED);
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
		tHandler	*handler;

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

		handler = NULL;
		for( i = 0; i < giNumHandlers; i ++ )
		{
			if( strcmp(type, gaHandlers[i]->Name) == 0 ) {
				handler = gaHandlers[i];
				break;
			}
		}

		if( !handler ) {
			fprintf(stderr, "Unknow item type '%s' on line %i (%s)\n", type, lineNum, desc);
			continue ;
		}

		for( i = 0; i < giNumItems; i ++ )
		{
			if( gaItems[i].Handler != handler )	continue;
			if( gaItems[i].ID != num )	continue;

			printf("Redefinition of %s:%i, updated\n", handler->Name, num);
			gaItems[i].Price = price;
			free(gaItems[i].Name);
			gaItems[i].Name = strdup(desc);
			break;
		}
		if( i < giNumItems )	continue;

		gaItems = realloc( gaItems, (giNumItems + 1)*sizeof(gaItems[0]) );
		gaItems[giNumItems].Handler = handler;
		gaItems[giNumItems].ID = num;
		gaItems[giNumItems].Price = price;
		gaItems[giNumItems].Name = strdup(desc);
		gaItems[giNumItems].bHidden = (line[0] == '-');
		giNumItems ++;
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
