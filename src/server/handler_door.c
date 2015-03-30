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

#include "common.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <pty.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <stdbool.h>

#define DOOR_UNLOCKED_DELAY	10	// Time in seconds before the door re-locks

// === IMPORTS ===

// === PROTOTYPES ===
void*	Door_Lock(void* Unused);
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
char	*gsDoor_SerialPort;	// Set from config in main.c
sem_t	gDoor_UnlockSemaphore;
pthread_t	gDoor_LockThread;
bool	gbDoor_LockThreadStarted;

// === CODE ===
void* Door_Lock(void* Unused __attribute__((unused)))
{
	gbDoor_LockThreadStarted = true;
	while(1)
	{
		sem_wait(&gDoor_UnlockSemaphore);

		int door_serial_handle = InitSerial(gsDoor_SerialPort, 9600);
		if(door_serial_handle < 0)
		{
			fprintf(stderr, "Unable to open door serial '%s'\n", gsDoor_SerialPort);
			perror("Opening door port");
		}

		// Disable local echo
		{
			struct termios	info;
			tcgetattr(door_serial_handle, &info);
			info.c_cflag &= ~CLOCAL;
			tcsetattr(door_serial_handle, TCSANOW, &info);
		}

		if(write(door_serial_handle, "\xff\x01\x01", 3) != 3)	// Relay ON
		{
			fprintf(stderr, "Failed to write Relay ON (unlock) command, errstr: %s", strerror(errno));
		}

		sleep(DOOR_UNLOCKED_DELAY);

		if(write(door_serial_handle, "\xff\x01\x00", 3) != 3)	// Relay OFF
		{
			fprintf(stderr, "Failed to write Relay OFF (lock) command, errstr: %s", strerror(errno));
		}

		close(door_serial_handle);
	}
}

int Door_InitHandler(void)
{
	// Thread started later

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
	
	// Door thread spun up here because program is forked after thread created
	if( !gbDoor_LockThreadStarted )
	{
		// Initialize semaphore, triggers door lock release if semaphore is greater than 0
		sem_init(&gDoor_UnlockSemaphore, 0, 0);	

		pthread_create(&gDoor_LockThread, NULL, &Door_Lock, NULL);
	}

	if(sem_post(&gDoor_UnlockSemaphore))
	{
		perror("Failed to post \"Unlock Door\" semaphore, sem_post returned");
		return -1;
	}

	#if DEBUG
	printf("Door_DoDispense: User %i opened door\n", User);
	#endif

	return 0;
}
