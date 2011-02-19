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
#if !USE_POPEN
# include <unistd.h>
#endif

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
volatile int	giDoor_ChildStatus;

// == CODE ===
void Door_SIGCHLDHandler(int signum)
{
	signum = 0;
	printf("SIGCHLD\n");
	giDoor_ChildStatus ++;
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
	char	buf[512];
	 int	stdin_pair[2];
	 int	stdout_pair[2];
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
	
	// Create stdin/stdout
	if( pipe(stdin_pair) || pipe(stdout_pair) )
	{
		perror("pipe");
		return -1;
	}
	
	giDoor_ChildStatus = 0;	// Set child status to zero
	parentPid = getpid();
	childPid = fork();
	
	if( childPid < 0 )
	{
		perror("fork");
		return -1;
	}
	
	// Child process
	if( childPid == 0 )
	{
		
		// Close write end of stdin, and set it to #0
		close(stdin_pair[1]);	dup2(stdin_pair[0], 0);
		// Close read end of stdout, and set it to #1
		close(stdout_pair[0]);	dup2(stdout_pair[1], 1);
		
		execl("/bin/sh", "sh", "-c", "llogin door -w-", NULL);
		perror("execl");
		exit(-1);
	}
	
	child_stdin = fdopen(stdin_pair[1], "w");
	close(stdin_pair[0]);	// child stdin read
	close(stdout_pair[1]);	// child stdout write
	
	if( giDoor_ChildStatus || read(stdout_pair[0], buf, 512) < 0) {
		#if DEBUG
		printf("Door_DoDispense: fread fail\n");
		#endif
		return -1;
	}
	
	// Send password
	if( giDoor_ChildStatus || fputs(gsDoor_Password, child_stdin) <= 0 ) {
		#if DEBUG
		printf("Door_DoDispense: fputs password\n");
		#endif
		return -1;
	}
	fputs("\n", child_stdin);
	
	
	#if DEBUG
	printf("Door_DoDispense: Door unlock\n");
	#endif
	
	// ATH1 - Unlock door
	if( giDoor_ChildStatus || fputs("ATH1\n", child_stdin) <= 0) {
		#if DEBUG
		printf("Door_DoDispense: fputs unlock\n");
		#endif
		return -1;
	}
	
	// Wait before re-locking
	sleep(DOOR_UNLOCKED_DELAY);


	#if DEBUG
	printf("Door_DoDispense: Door re-lock\n");
	#endif

	// Re-lock the door
	if( giDoor_ChildStatus || fputs("ATH0\n", child_stdin) == 0 ) {
		#if DEBUG
		printf("Door_DoDispense: fputs lock\n");
		#endif
		return -1;
	}
	
	fclose(child_stdin);
	close(stdin_pair[1]);	// child stdin write
	close(stdout_pair[0]);	// child stdout read
	
	#if DEBUG
	printf("Door_DoDispense: User %i opened door\n", User);
	#endif

	return 0;
}

