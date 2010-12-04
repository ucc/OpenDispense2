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
	
	giCoke_SerialFD = InitSerial(gsCoke_SerialPort, 9600);
	if( giCoke_SerialFD == -1 ) {
		fprintf(stderr, "ERROR: Unable to open coke serial port ('%s')\n", gsCoke_SerialPort);
	}
	
	CompileRegex(&gCoke_StatusRegex, "^slot\\s+(\\d)\\s+([^:]+):([a-zA-Z]+)\\s*", REG_EXTENDED);
	return 0;
}

int Coke_CanDispense(int User, int Item)
{
	char	tmp[32], *status;
	regmatch_t	matches[4];
	 int	ret;

	// Sanity please
	if( Item < 0 || Item > 6 )	return -1;	// -EYOURBAD
	
	// Ask the coke machine
	sprintf(tmp, "s%i\n", Item);
	write(giCoke_SerialFD, tmp, 2);

	// Wait a little
	sleep(250);

	// Read the response
	tmp[0] = '\0';
	ret = read(giCoke_SerialFD, tmp, sizeof(tmp)-1);
	//printf("ret = %i\n", ret);
	if( ret <= 0 ) {
		fprintf(stderr, "Coke machine is not being chatty (read = %i)\n", ret);
		return -1;
	}
	ret = RunRegex(&gCoke_StatusRegex, tmp, sizeof(matches)/sizeof(matches[0]), matches, "Bad Response");
	if( ret ) {
		return -1;
	}

	tmp[ matches[3].rm_eo ] = '\0';
	status = &tmp[ matches[3].rm_so ];

	printf("Machine responded slot status '%s'\n", status);

	if( strcmp(status, "full") == 0 )
		return 0;

	return 1;
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
	
	// Wait a little
	sleep(250);

	// Get status
	read(giCoke_SerialFD, tmp, sizeof(tmp)-1);
	regexec(&gCoke_StatusRegex, tmp, sizeof(matches)/sizeof(matches[0]), matches, 0);
	
	tmp[ matches[3].rm_eo ] = '\0';
	status = &tmp[ matches[3].rm_so ];

	printf("Machine responded slot status '%s'\n", status);

	return 0;
}


