/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * handler_door.c - Door Relay code
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
#define	DEBUG	1
#define	USE_POPEN	0

#include "common.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <pty.h>

#define DOOR_UNLOCKED_DELAY	5	// Time in seconds before the door re-locks

// === IMPORTS ===

// === PROTOTYPES ===
 int	Door_InitHandler();
 int	Door_CanDispense(int User, int Item);
 int	Door_DoDispense(int User, int Item);
 int	writes(int fd, const char *str);
char	*ReadStatus(int FD);

// === GLOBALS ===
tHandler	gDoor_Handler = {
	"door",
	Door_InitHandler,
	Door_CanDispense,
	Door_DoDispense
};
char	*gsDoor_SerialPort;	// Set from config in main.c

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
	 int	door_serial_handle;
	
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
	
	door_serial_handle = InitSerial(gsDoor_SerialPort, 115200);
	if(door_serial_handle < 0) {
		fprintf(stderr, "Unable to open door serial '%s'\n", gsDoor_SerialPort);
		perror("Opening door port");
		return -1;
	}

	// Disable local echo
	{
		struct termios	info;
		tcgetattr(door_serial_handle, &info);
		info.c_cflag &= ~CLOCAL;
		tcsetattr(door_serial_handle, TCSANOW, &info);
	}

//	flush(door_serial_handle);

	writes(door_serial_handle, "4;");

#if 0
	char *status = ReadStatus(door_serial_handle);
	if( !status )	return -1;
	if( strcmp(status, "Opening door") != 0 ) {
		fprintf(stderr, "Unknown/unexpected door status '%s'\n", status);
		return -1;
	}
#endif
	// Read and discard anything in the buffer
	{
		char tmpbuf[32];
		read(door_serial_handle, tmpbuf, sizeof(tmpbuf));
	}

	close(door_serial_handle);
	
	#if DEBUG
	printf("Door_DoDispense: User %i opened door\n", User);
	#endif

	return 0;
}

int writes(int fd, const char *str)
{
	 int	len = strlen(str);
	
	if( len != write(fd, str, len) )
	{
		return 1;
	}
	return 0;
}

char *ReadStatus(int FD)
{
	char	tmpbuf[32];
	 int	len;
	len = read(FD, tmpbuf, sizeof(tmpbuf)-1);
	tmpbuf[len] = 0;
	char *msg = strchr(tmpbuf, ',');
	if( !msg ) {
		fprintf(stderr, "Door returned malformed data (no ',')\n");
		return NULL;
	}
	msg ++;
	char *end = strchr(tmpbuf, ';');
	if( !end ) {
		fprintf(stderr, "Door returned malformed data (no ';')\n");
		return NULL;
	}
	*end = '\0';

	return strdup(msg);
}
