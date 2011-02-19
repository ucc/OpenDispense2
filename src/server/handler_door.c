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

#define	DEBUG	1

#define DOOR_UNLOCKED_DELAY	10	// 10 seconds before it re-locks

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
char	*gsDoor_Password = "";
// int	giDoor_SerialFD;

// == CODE ===
int Door_InitHandler(void)
{	
	return 0;
}

/**
 */
int Door_CanDispense(int User, int Item)
{
	#if DEBUG
	printf("Door_CanDispense: (User=%i,Item=%i)\n", User, Item);
	#endif
	// Sanity please
	if( Item != 0 )	return -1;
	
	if( !(Bank_GetFlags(User) & USER_FLAG_DOORGROUP) )
	{
		#if DEBUG
		printf("Door_CanDispense: User %i not in door\n", User);
		#endif
		return 1;
	}
	
	#if DEBUG
	printf("Door_CanDispense: User %i can open the door\n", User);
	#endif
	
	return 0;
}

/**
 * \brief Actually do a dispense from the coke machine
 */
int Door_DoDispense(int User, int Item)
{
	FILE	*pipe;
	char	buf[512];	// Buffer flush location - the sewer :)
	
	#if DEBUG
	printf("Door_DoDispense: (User=%i,Item=%i)\n", User, Item);
	#endif
	
	// Sanity please
	if( Item != 0 )	return -1;
	
	// Check if user is in door
	if( !(Bank_GetFlags(User) & USER_FLAG_DOORGROUP) )
	{
		#if DEBUG
		printf("Door_CanDispense: User %i not in door\n", User);
		#endif
		return 1;
	}
	
	// llogin or other
	pipe = popen("llogin door -w -", "w");
	if( !pipe || pipe == (void*)-1 ) {
		#if DEBUG
		printf("Door_DoDispense: llogin failure\n");
		#endif
		return -1;
	}
	if( fread(buf, 512, 1, pipe) == 0 )	return -1;	// Flush!
	
	// Send password
	fputs(gsDoor_Password, pipe);
	fputs("\n", pipe);
	if( fread(buf, 512, 1, pipe) == 0 )	return -1;	// Flush!
	
	// ATH1 - Unlock door
	fputs("ATH1\n", pipe);
	if( fread(buf, 512, 1, pipe) == 0 )	return -1;	// Flush!
	
	// Wait before re-locking
	sleep(DOOR_UNLOCKED_DELAY);

	// Re-lock the door
	fputs("ATH0\n", pipe);
	if( fread(buf, 512, 1, pipe) == 0 )	return -1;	// Flush!
	
	pclose(pipe);
	
	#if DEBUG
	printf("Door_DoDispense: User %i opened door\n", User);
	#endif

	return 0;
}


