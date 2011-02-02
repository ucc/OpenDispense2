/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * handler_doror.c - Door Relay code
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

// === IMPORTS ===

// === PROTOTYPES ===
 int	Door_InitHandler();
 int	Door_CanDispense(int User, int Item);
 int	Door_DoDispense(int User, int Item);

// === GLOBALS ===
tHandler	gDoor_Handler = {
	"door",
	Door_InitHandler,
	Door_CanDispense,
	Door_DoDispense
};
char	*gsDoor_Password;
char	*gsDoor_Command;
// int	giDoor_SerialFD;

// == CODE ===
int Door_InitHandler(void)
{
//	printf("connecting to door...\n");
//	giDoor_SerialFD = open(gsDoor_SerialPort, O_RDWR);
//	if( giDoor_SerialFD == -1 ) {
//		fprintf(stderr, "ERROR: Unable to open coke serial port ('%s')\n", gsDoor_SerialPort);
//	}
	return 0;
}

/**
 */
int Door_CanDispense(int User, int Item)
{
	// Sanity please
	if( Item == 0 )	return -1;
	
	if( !(Bank_GetFlags(User) & USER_FLAG_DOORGROUP) )
		return 1;
		
	gsDoor_Command = malloc(sizeof("llogin door -w ")+strlen(gsDoor_Password));
	sprintf(gsDoor_Command, "llogin door -w %s", gsDoor_Password);
	
	return 0;
}

/**
 * \brief Actually do a dispense from the coke machine
 */
int Door_DoDispense(int User, int Item)
{
	FILE	*pipe;
	// Sanity please
	if( Item != 0 )	return -1;
	
	// Check if user is in door
	if( !(Bank_GetFlags(User) & USER_FLAG_DOORGROUP) )
		return 1;
	
	// llogin or other
	pipe = popen(gsDoor_Command, "w");
	if( !pipe || pipe == (void*)-1 )
		return -1;
	
	fputs("ATH1F", pipe);
	sleep(1);
	fputs("ATH10", pipe);
	
	pclose(pipe);

	return 0;
}


