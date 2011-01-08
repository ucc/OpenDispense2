/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 * - Dispense Server
 *
 * handler_snack.c - Snack machine code
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
 int	Snack_InitHandler();
 int	Snack_CanDispense(int User, int Item);
 int	Snack_DoDispense(int User, int Item);

// === GLOBALS ===
tHandler	gSnack_Handler = {
	"snack",
	Snack_InitHandler,
	Snack_CanDispense,
	Snack_DoDispense
};
char	*gsSnack_SerialPort = "/dev/ttyS1";
 int	giSnack_SerialFD;
regex_t	gSnack_ResponseRegex;

// == CODE ===
int Snack_InitHandler()
{
	giSnack_SerialFD = InitSerial(gsSnack_SerialPort, 9600);
	if( giSnack_SerialFD == -1 ) {
		fprintf(stderr, "ERROR: Unable to open snack serial port ('%s')\n", gsSnack_SerialPort);
	}
	
	regcomp(&gSnack_ResponseRegex, "^(\\d\\d\\d)(.*)$", REG_EXTENDED);
	return 0;
}

int Snack_CanDispense(int UNUSED(User), int Item)
{
	// Sanity please
	if( Item < 0 || Item > 99 )	return -1;
	
	// Hmm... could we implement slot statuses?
	
	return 0;
}

/**
 * \brief Actually do a dispense from the coke machine
 */
int Snack_DoDispense(int UNUSED(User), int Item)
{
	char	tmp[32];
	regmatch_t	matches[4];

	// Sanity please
	if( Item < 0 || Item > 99 )	return -1;

	// Dispense
	sprintf(tmp, "V%02i\n", Item);
	write(giSnack_SerialFD, tmp, 2);

	// Get status
	read(giSnack_SerialFD, tmp, sizeof(tmp)-1);
	regexec(&gSnack_ResponseRegex, tmp, sizeof(matches)/sizeof(matches[0]), matches, 0);

	return 0;
}
