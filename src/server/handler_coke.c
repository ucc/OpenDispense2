/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * handler_coke.c - Coke controller code
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 *
 * NOTES:
 * - Remember, the coke machine echoes your text back to you!
 */
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <regex.h>

#define READ_TIMEOUT	2	// 2 seconds for ReadChar

// === IMPORTS ===

// === PROTOTYPES ===
 int	Coke_InitHandler();
 int	Coke_CanDispense(int User, int Item);
 int	Coke_DoDispense(int User, int Item);
 int	WaitForColon();
 int	ReadLine(int len, char *output);

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
	
	CompileRegex(&gCoke_StatusRegex, "^slot\\s+([0-9]+)\\s+([^:]+):([a-zA-Z]+)\\s*", REG_EXTENDED);
	return 0;
}

int Coke_CanDispense(int UNUSED(User), int Item)
{
	char	tmp[40], *status;
	regmatch_t	matches[4];
	 int	ret;

	// Sanity please
	if( Item < 0 || Item > 6 )	return -1;	// -EYOURBAD
	
	// Can't dispense if the machine is not connected
	if( giCoke_SerialFD == -1 )
		return -2;
	
	// Flush the input buffer
	{
		char	tmpbuf[512];
		read(giCoke_SerialFD, tmpbuf, sizeof(tmpbuf));
	}
	
	// Wait for a prompt
	ret = 0;
	do {
		write(giCoke_SerialFD, "d7\r\n", 4);
	} while( WaitForColon() && ret++ < 3 );

	if( ret == 3 ) {
		fprintf(stderr, "Coke machine timed out\n");
		return -2;	// -EMYBAD
	}

	// TODO: Handle "not ok" response to D7
	
	// Ask the coke machine
	sprintf(tmp, "s%i\r\n", Item);
	write(giCoke_SerialFD, tmp, 4);

	// Read from the machine (ignoring empty lines)
	while( (ret = ReadLine(sizeof(tmp)-1, tmp)) == 0 );
	printf("ret = %i, tmp = '%s'\n", ret, tmp);
	if( tmp[0] == ':' ) {
		ret = ReadLine(sizeof(tmp)-1, tmp);
		printf("ret = %i, tmp = '%s'\n", ret, tmp);
	}

	// Catch an error	
	if( ret <= 0 ) {
		fprintf(stderr, "Coke machine is not being chatty (read = %i)\n", ret);
		if( ret == -1 ) {
			perror("Coke Machine");
		}
		return -1;
	}

	// Eat rest of response
	WaitForColon();

	// Parse status response
	ret = RunRegex(&gCoke_StatusRegex, tmp, sizeof(matches)/sizeof(matches[0]), matches, "Bad Response");
	if( ret ) {
		return -1;
	}

	// Get slot status
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
int Coke_DoDispense(int UNUSED(User), int Item)
{
	char	tmp[32];
	 int	i, ret;

	// Sanity please
	if( Item < 0 || Item > 6 )	return -1;

	// Can't dispense if the machine is not connected
	if( giCoke_SerialFD == -1 )
		return -2;
	
	// Flush the input buffer
	{
		char	tmpbuf[512];
		read(giCoke_SerialFD, tmpbuf, sizeof(tmpbuf));
	}

	// Wait for prompt
	i = 0;
	do {
		write(Item, "d7\r\n", 4);
	} while( WaitForColon() && i++ < 3 );

	// Dispense
	sprintf(tmp, "d%i\r\n", Item);
	write(giCoke_SerialFD, tmp, 4);
	
	// Read empty lines
	while( (ret = ReadLine(sizeof(tmp)-1, tmp)) == -1 );
	if( ret == -1 )	return -1;
	// Read d%i
	while( tmp[0] == ':' ) {
		ret = ReadLine(sizeof(tmp)-1, tmp);
		if( ret == -1 )	return -1;
	}
	// Get status
	ret = ReadLine(sizeof(tmp)-1, tmp);
	if( ret == -1 )	return -1;

	WaitForColon();	// Eat up rest of response

	// TODO: Regex
	if( strcmp(tmp, "ok") == 0 ) {
		// We think dispense worked
		// - The machine returns 'ok' if an empty slot is dispensed, even if
		//   it doesn't actually try to dispense (no sound)
		return 0;
	}

	printf("Machine returned unknown value '%s'\n", tmp);	

	return -1;
}

char ReadChar()
{
	fd_set	readfs;
	char	ch = 0;
	 int	ret;
	struct timeval	timeout;
	
	timeout.tv_sec = READ_TIMEOUT;
	timeout.tv_usec = 0;
	
	FD_ZERO(&readfs);
	FD_SET(giCoke_SerialFD, &readfs);
	
	ret = select(giCoke_SerialFD+1, &readfs, NULL, NULL, &timeout);
	if( ret == 0 )	return 0;	// Timeout
	if( ret != 1 ) {
		printf("readchar return %i\n", ret);
		return 0;
	}
	
	ret = read(giCoke_SerialFD, &ch, 1);
	if( ret != 1 ) {
		printf("ret = %i\n", ret);
		return 0;
	}
	
	return ch;
}

int WaitForColon()
{
	fd_set	readfs;
	char	ch = 0;
	
	FD_SET(giCoke_SerialFD, &readfs);
	
	while( (ch = ReadChar()) != ':' && ch != 0);
	
	if( ch == 0 )	return -1;	// Timeout
	
	return 0;
}

int ReadLine(int len, char *output)
{
	char	ch;
	 int	i = 0;
	
	for(;;)
	{
		ch = ReadChar();
			
		if( i < len )
			output[i++] = ch;
		
		if( ch == '\0' ) {
			break;
		}
		if( ch == '\n' || ch == '\r' ) {
			if( i < len )
				output[--i] = '\0';
			break;
		}
	}

	//printf("ReadLine: output=%s\n", output);

	if( !ch ) 	return -1;
	return i;
}


