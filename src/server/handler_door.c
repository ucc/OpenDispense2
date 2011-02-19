/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * handler_doror.c - Door Relay code
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
#include <sys/wait.h>
#include <pty.h>

#define DOOR_UNLOCKED_DELAY	5	// Time in seconds before the door re-locks

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
volatile int	giDoor_ChildStatus;

// == CODE ===
void Door_SIGCHLDHandler(int signum)
{
	signum = 0;
	giDoor_ChildStatus ++;
	printf("SIGCHLD: giDoor_ChildStatus = %i \n", giDoor_ChildStatus);
}

int Door_InitHandler(void)
{
	signal(SIGCHLD, Door_SIGCHLDHandler);
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
	FILE	*child_stdin;
	#if 0
	 int	stdin_pair[2];
	 int	stdout_pair[2];
	#else
	 int	child_stdin_fd;
	#endif
	pid_t	childPid;
	pid_t	parentPid;
	
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
	
	giDoor_ChildStatus = 0;	// Set child status to zero
	parentPid = getpid();
	childPid = forkpty(&child_stdin_fd, NULL, NULL, NULL);
	
	if( childPid < 0 )
	{
		perror("fork");
		return -1;
	}
	
	// Child process
	if( childPid == 0 )
	{
		execl("/usr/bin/llogin", "llogin", "door", "-w-", NULL);
		perror("execl");
		exit(-1);
	}
	
	child_stdin = fdopen(child_stdin_fd, "w");

	int read_child_output()	
	{
		char	buf[1024];
		 int	len;
		if( giDoor_ChildStatus || (len = read(child_stdin_fd, buf, sizeof buf)) < 0)
		{
			#if DEBUG
			printf("Door_DoDispense: fread fail\n");
			#endif
			return -1;
		}
		buf[len] = '\0';
		
		#if DEBUG > 1
		printf("Door_DoDispense: buf = %i '%s'\n", len, buf);
		#endif
		return 0;
	}

	if( read_child_output() )	return -1;
	
	// Send password
	if( giDoor_ChildStatus || fputs(gsDoor_Password, child_stdin) <= 0 ) {
		printf("Door_DoDispense: fputs password fail\n");
		return -1;
	}
	fputs("\n", child_stdin);
	fflush(child_stdin);
	
	if( read_child_output() )	return -1;
	
	#if DEBUG
	printf("Door_DoDispense: Door unlock\n");
	#endif
	// ATH1 - Unlock door
	if( giDoor_ChildStatus || fputs("ATH1\n", child_stdin) == 0) {
		#if DEBUG
		printf("Door_DoDispense: fputs unlock failed (or child terminated)\n");
		#endif
		return -1;
	}
	fflush(child_stdin);
	
	// Wait before re-locking
	sleep(DOOR_UNLOCKED_DELAY);

	#if DEBUG
	printf("Door_DoDispense: Door re-lock\n");
	#endif
	// Re-lock the door (and quit llogin)
	if( giDoor_ChildStatus || fputs("ATH0\n", child_stdin) == 0 ) {
		fprintf(stderr, "Oh F**k, the door may be stuck unlocked, someone use llogin!\n");
		return -1;
	}
	fflush(child_stdin);
	fputs("\x1D", child_stdin);

	// Wait a little so llogin can send the lock message
	sleep(1);
	
	fclose(child_stdin);
	close(child_stdin_fd);
	
	#if DEBUG
	printf("Door_DoDispense: User %i opened door\n", User);
	#endif

	kill(childPid, SIGKILL);

	return 0;
}

