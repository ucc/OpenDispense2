/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 * - Dispense Client
 *
 * main.c - Core and Initialisation
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
#include <stdio.h>

// === GLOBALS ===
char	*gsDispenseServer = "martello";
 int	giDispensePort = 11020;

// === CODE ===
int main(int argc, char *argv[])
{
	// Connect to server
	

	// Determine what to do
	if( argc > 1 )
	{
		if( strcmp(argv[1], "acct") == 0 )
		{
			return 0;
		}
	}

	// Ask server for stock list
	
	// Display the list for the user
	// and choose what to dispense

	return 0;
}
