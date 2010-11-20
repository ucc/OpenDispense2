/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * main.c - Initialisation Code
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "common.h"

// === IMPORTS ===
extern void	Init_Cokebank(void);	// cokebank.c
extern void	Load_Itemlist(void);
extern void	Server_Start(void);
extern int	giServer_Port;
extern char*	gsItemListFile;
extern char*	gsCoke_SerialPort;

// === GLOBALS ===
 int	giDebugLevel = 0;

// === CODE ===
int main(int argc, char *argv[])
{
	 int	i;
	
	// Parse Arguments
	for( i = 1; i < argc; i++ )
	{
		char	*arg = argv[i];
		if( arg[0] == '-' && arg[1] != '-')
		{
			switch(arg[1])
			{
			case 'p':
				giServer_Port = atoi(argv[++i]);
				break;
			case 'd':
				giDebugLevel = atoi(argv[++i]);
				break;
			default:
				// Usage Error?
				break;
			}
		}
		else if( arg[0] == '-' && arg[1] == '-' ) {
			if( strcmp(arg, "--itemsfile") == 0 ) {
				gsItemListFile = argv[++i];
			}
			if( strcmp(arg, "--cokeport") == 0 ) {
				gsCoke_SerialPort = argv[++i];
			}
			else {
				// Usage error?
			}
		}
		else {
			// Usage Error?
		}
	}
	
	Init_Cokebank();
	
	Load_Itemlist();
	
	Server_Start();
	
	return 0;
}

