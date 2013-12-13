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
#include <sys/stat.h>
#include <time.h>

#define DUMP_ITEMS	0

// === IMPORTS ===
extern tHandler	gCoke_Handler;
extern tHandler	gSnack_Handler;
extern tHandler	gDoor_Handler;

// === PROTOTYPES ===
void	Init_Handlers(void);
void	Load_Itemlist(void);
void	Items_ReadFromFile(void);
char	*trim(char *__str);

// === GLOBALS ===
 int	giNumItems = 0;
tItem	*gaItems = NULL;
time_t	gItems_LastUpdated;
tHandler	gPseudo_Handler = {.Name="pseudo"};
tHandler	*gaHandlers[] = {&gPseudo_Handler, &gCoke_Handler, &gSnack_Handler, &gDoor_Handler};
 int	giNumHandlers = sizeof(gaHandlers)/sizeof(gaHandlers[0]);
char	*gsItemListFile = DEFAULT_ITEM_FILE;
#if USE_INOTIFY
 int	giItem_INotifyFD;
#endif
regex_t	gItemFile_Regex;

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
 * \brief Read the initial item list
 */
void Load_Itemlist(void)
{
	 int	rv;
	rv = regcomp(&gItemFile_Regex, "^-?([a-zA-Z][a-zA-Z]*)\\s+([0-9]+)\\s+([0-9]+)\\s+(.*)", REG_EXTENDED);
	if( rv )
	{
		size_t	len = regerror(rv, &gItemFile_Regex, NULL, 0);
		char	errorStr[len];
		regerror(rv, &gItemFile_Regex, errorStr, len);
		fprintf(stderr, "Rexex compilation failed - %s\n", errorStr);
		exit(-1);
	}
	
	Items_ReadFromFile();
	
	// Re-read the item file periodically
	// TODO: Be less lazy here and check the timestamp
	AddPeriodicFunction( Items_ReadFromFile );
}

/**
 * \brief Read the item list from disk
 */
void Items_ReadFromFile(void)
{
	FILE	*fp;
	char	buffer[BUFSIZ];
	char	*line;
	 int	lineNum = 0;
	 int	i, numItems = 0;
	tItem	*items = NULL;
	regmatch_t	matches[5];

	if( gItems_LastUpdated ) 
	{
		struct stat buf;
		if( stat(gsItemListFile, &buf) ) {
			fprintf(stderr, "Unable to stat() item file '%s'\n", gsItemListFile);
			return ;
		}
		
		// Check if the update is needed
		if( gItems_LastUpdated > buf.st_mtime )
			return ;
	}

	// Error check
	fp = fopen(gsItemListFile, "r");
	if(!fp) {
		fprintf(stderr, "Unable to open item file '%s'\n", gsItemListFile);
		perror("Unable to open item file");
		return ;
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
		if( RunRegex( &gItemFile_Regex, line, 5, matches, NULL) ) {
			fprintf(stderr, "Syntax error on line %i of item file '%s'\n", lineNum, gsItemListFile);
			return ;
		}

		// Read line data
		type  = line + matches[1].rm_so;	line[ matches[1].rm_eo ] = '\0';
		num   = atoi( line + matches[2].rm_so );
		price = atoi( line + matches[3].rm_so );
		desc  = line + matches[4].rm_so;	

		#if DUMP_ITEMS
		printf("Item '%s' - %i cents, %s:%i\n", desc, price, type, num);
		#endif

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

		for( i = 0; i < numItems; i ++ )
		{
			if( items[i].Handler != handler )	continue;
			if( items[i].ID != num )	continue;

			#if DUMP_ITEMS
			printf("Redefinition of %s:%i, updated\n", handler->Name, num);
			#endif
			items[i].Price = price;
			free(items[i].Name);
			items[i].Name = strdup(desc);
			break;
		}
		if( i < numItems )	continue;

		items = realloc( items, (numItems + 1)*sizeof(items[0]) );
		items[numItems].Handler = handler;
		items[numItems].ID = num;
		if( gbNoCostMode )
			items[numItems].Price = 0;
		else
			items[numItems].Price = price;
		items[numItems].Name = strdup(desc);
		items[numItems].bHidden = (line[0] == '-');
		numItems ++;
	}
	
	// Clean up old
	if( giNumItems )
	{
		giNumItems = 0;
		free(gaItems);
		gaItems = NULL;
	}
	fclose(fp);
	
	// Replace with new
	giNumItems = numItems;
	gaItems = items;
	
	gItems_LastUpdated = time(NULL);
}

/**
 * \brief Update the item file from the internal database
 */
void Items_UpdateFile(void)
{
	FILE	*fp;
	char	buffer[BUFSIZ];
	char	*line;
	 int	lineNum = 0;
	 int	i;
	regmatch_t	matches[5];
	char	**line_comments;
	 int	*line_items;

	// Error check
	fp = fopen(gsItemListFile, "r");
	if(!fp) {
		fprintf(stderr, "Unable to open item file '%s'\n", gsItemListFile);
		perror("Unable to open item file");
		return ;
	}
	
	// Count lines
	while( fgets(buffer, BUFSIZ, fp) )
	{
		lineNum ++;
	}
	
	line_comments = malloc(lineNum * sizeof(char*));
	line_items = malloc(lineNum * sizeof(int));
	
	// Parse file
	lineNum = 0;
	fseek(fp, 0, SEEK_SET);
	while( fgets(buffer, BUFSIZ, fp) )
	{
		char	*hashPos, *semiPos;
		char	*type;
		 int	num;
		tHandler	*handler;

		trim(buffer);

		lineNum ++;
		line_items[lineNum-1] = -1;
		line_comments[lineNum-1] = NULL;

		// Get comments
		hashPos = strchr(buffer, '#');
		semiPos = strchr(buffer, ';');
		if( hashPos && semiPos ) {
			if( hashPos < semiPos )
				line_comments[lineNum-1] = strdup(hashPos);
		}
		else if( hashPos ) {
			line_comments[lineNum-1] = strdup(hashPos);
		}
		else if( semiPos ) {
			line_comments[lineNum-1] = strdup(semiPos);
		}
		if(hashPos)	*hashPos = '\0';
		if(semiPos)	*semiPos = '\0';
		
		// Trim whitespace
		line = trim(buffer);
		if(strlen(line) == 0)	continue;
		
		// Pass regex over line
		if( RunRegex( &gItemFile_Regex, line, 5, matches, NULL) ) {
			fprintf(stderr, "Syntax error on line %i of item file '%s'\n", lineNum, gsItemListFile);
			return ;
		}

		// Read line data
		type  = line + matches[1].rm_so;	line[ matches[1].rm_eo ] = '\0';
		num   = atoi( line + matches[2].rm_so );

		// Find handler
		handler = NULL;
		for( i = 0; i < giNumHandlers; i ++ )
		{
			if( strcmp(type, gaHandlers[i]->Name) == 0 ) {
				handler = gaHandlers[i];
				break;
			}
		}
		if( !handler ) {
			fprintf(stderr, "Warning: Unknown item type '%s' on line %i\n", type, lineNum);
			continue ;
		}

		for( i = 0; i < giNumItems; i ++ )
		{
			if( gaItems[i].Handler != handler )	continue;
			if( gaItems[i].ID != num )	continue;
			
			line_items[lineNum-1] = i;
			break;
		}
		if( i >= giNumItems ) {
			continue;
		}
	}
	
	fclose(fp);
	
	//fp = fopen("items.cfg.new", "w");	// DEBUG: Don't kill the real item file until debugged
	fp = fopen(gsItemListFile, "w");
	
	// Create new file
	{
		 int	done_items[giNumItems];
		memset(done_items, 0, sizeof(done_items));
		
		// Existing items
		for( i = 0; i < lineNum; i ++ )
		{
			if( line_items[i] != -1 ) {
				tItem	*item = &gaItems[ line_items[i] ];
				
				if( done_items[ line_items[i] ] ) {
					fprintf(fp, "; DUP -");
				}
				done_items[ line_items[i] ] = 1;
				
				if( item->bHidden )
					fprintf(fp, "-");
				
				fprintf(fp, "%s\t%i\t%i\t%s\t",
					item->Handler->Name, item->ID, item->Price, item->Name
					);
			}
			
			if( line_comments[i] ) {
				fprintf(fp, "%s", line_comments[i]);
				free( line_comments[i] );
			}
			
			fprintf(fp, "\n");
		}
		
		// New items
		for( i = 0; i < giNumItems; i ++ )
		{
			tItem	*item = &gaItems[i];
			if( done_items[i] )	continue ;
			
			if( item->bHidden )
				fprintf(fp, "-");
			
			fprintf(fp, "%s\t%i\t%i\t%s\n",
				item->Handler->Name, item->ID, item->Price, item->Name
				);
		}
	}
	
	free( line_comments );
	free( line_items );
	fclose(fp);
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
