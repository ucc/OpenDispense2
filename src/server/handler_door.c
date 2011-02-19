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
void	Door_int_SIGCHLDHandler(int signum);

// === GLOBALS ===
tHandler	gDoor_Handler = {
	"door",
	Door_InitHandler,
	Door_CanDispense,
	Door_DoDispense
};
char	*gsDoor_Password = "";
volatile int	giDoor_ChildTerminated;

// == CODE ===
int Door_InitHandler(void)
{	
	signal(SIGCHLD, Door_int_SIGCHLDHandler);
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
	#if !USE_POPEN
	 int	stdin_pair[2];
	 int	stdout_pair[2];
	pid_t	childPid;
	pid_t	parent_pid;
	#endif
	
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
	
	#if !USE_POPEN
	// Create stdin/stdout
	if( pipe(stdin_pair) || pipe(stdout_pair) )
	{
		perror("pipe");
		return -1;
	}
	
	parent_pid = getpid();
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
		
		execl("llogin", "door", "-w-", NULL);
		kill(parent_pid, SIGCHLD);
		perror("execl");
		exit(-1);
	}
	
	child_stdin = fdopen(stdin_pair[1], "w");
	close(stdin_pair[0]);	// child stdin read
	close(stdout_pair[1]);	// child stdout write
	
	#else
	// llogin or other
	child_stdin = popen("llogin door -w -", "w");
	if( !child_stdin || child_stdin == (void*)-1 ) {
		#if DEBUG
		printf("Door_DoDispense: llogin failure\n");
		#endif
		return -1;
	}
	#endif
	
	if(fread(buf, 1, 512, child_stdin) == 0)	return -1;
	
	// Send password
	if( fputs(gsDoor_Password, child_stdin) <= 0 )	return -1;
	fputs("\n", child_stdin);
	
	
	#if DEBUG
	printf("Door_DoDispense: Door unlock\n");
	#endif
	
	// ATH1 - Unlock door
	if( fputs("ATH1\n", child_stdin) <= 0)	return -1;
	
	// Wait before re-locking
	sleep(DOOR_UNLOCKED_DELAY);


	#if DEBUG
	printf("Door_DoDispense: Door re-lock\n");
	#endif

	// Re-lock the door
	if( fputs("ATH0\n", child_stdin) <= 0 )	return -1;
	
	#if !USE_POPEN
	fclose(child_stdin);
	close(stdin_pair[1]);	// child stdin write
	close(stdout_pair[0]);	// child stdout read
	#else
	pclose(child_stdin);
	#endif
	
	#if DEBUG
	printf("Door_DoDispense: User %i opened door\n", User);
	#endif

	return 0;
}

void Door_int_SIGCHLDHandler(int signum)
{
	signum = 0;	// Snut up
	giDoor_ChildTerminated = 1;
}

