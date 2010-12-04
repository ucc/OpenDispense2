/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * handler_coke.c - Coke controller code
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <regex.h>

// === IMPORTS ===

// === PROTOTYPES ===
 int	Coke_InitHandler();
 int	Coke_CanDispense(int User, int Item);
 int	Coke_DoDispense(int User, int Item);

// === GLOBALS ===
tHandler	gCoke_Handler = {
	"coke",
	Coke_InitHandler,
	Coke_CanDispense,
	Coke_DoDispense
};
char	*gsCoke_SerialPort = "/dev/ttyS0";
 int	giCoke_SerialFD;
regex_t	gCoke_StatusRegex;

// == CODE ===
int Coke_InitHandler()
{
	printf("connecting to coke machine...\n");
	giCoke_SerialFD = open(gsCoke_SerialPort, O_RDWR);
	if( giCoke_SerialFD == -1 ) {
		fprintf(stderr, "ERROR: Unable to open coke serial port ('%s')\n", gsCoke_SerialPort);
	}
	regcomp(&gCoke_StatusRegex, "^slot\\s+(\\d)\\s+([^:]+):([a-zA-Z]+)\\s*", REG_EXTENDED);
	return 0;
}

int Coke_CanDispense(int User, int Item)
{
	char	tmp[32], *status;
	regmatch_t	matches[4];

	// Sanity please
	if( Item < 0 || Item > 6 )	return -1;
	
	// Ask the coke machine
	sprintf(tmp, "s%i\n", Item);
	write(giCoke_SerialFD, tmp, 2);

	// Read the response
	read(giCoke_SerialFD, tmp, sizeof(tmp)-1);
	regexec(&gCoke_StatusRegex, tmp, sizeof(matches)/sizeof(matches[0]), matches, 0);

	tmp[ matches[3].rm_eo ] = '\0';
	status = &tmp[ matches[3].rm_so ];

	printf("Machine responded slot status '%s'\n", status);

	if( strcmp(status, "full") == 0 )
		return 1;

	return 0;
}

/**
 * \brief Actually do a dispense from the coke machine
 */
int Coke_DoDispense(int User, int Item)
{
	char	tmp[32], *status;
	regmatch_t	matches[4];

	// Sanity please
	if( Item < 0 || Item > 6 )	return -1;

	// Dispense
	sprintf(tmp, "d%i\n", Item);
	write(giCoke_SerialFD, tmp, 2);

	// Get status
	read(giCoke_SerialFD, tmp, sizeof(tmp)-1);
	regexec(&gCoke_StatusRegex, tmp, sizeof(matches)/sizeof(matches[0]), matches, 0);
	
	tmp[ matches[3].rm_eo ] = '\0';
	status = &tmp[ matches[3].rm_so ];

	printf("Machine responded slot status '%s'\n", status);

	return 0;
}


