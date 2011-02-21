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
#include <stdarg.h>
#include <pthread.h>

#define READ_TIMEOUT	2	// 2 seconds for ReadChar
#define TRACE_COKE	1

#if TRACE_COKE
# define TRACE(v...) do{printf("%s: ",__func__);printf(v);}while(0)
#else
# define TRACE(...)
#endif

// === IMPORTS ===

// === PROTOTYPES ===
 int	Coke_InitHandler();
 int	Coke_int_GetSlotStatus(char *Buffer, int Slot);
void	Coke_int_UpdateSlotStatuses(void);
 int	Coke_CanDispense(int User, int Item);
 int	Coke_DoDispense(int User, int Item);
 int	Writef(const char *Format, ...);
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
 int	gaCoke_CachedStatus[7];
pthread_mutex_t	gCoke_Mutex = PTHREAD_MUTEX_INITIALIZER;

// == CODE ===
int Coke_InitHandler()
{
	CompileRegex(&gCoke_StatusRegex, "^slot\\s+([0-9]+)\\s+([^:]+):([a-zA-Z]+)\\s*", REG_EXTENDED);
	
	printf("connecting to coke machine...\n");
	
	giCoke_SerialFD = InitSerial(gsCoke_SerialPort, 9600);
	if( giCoke_SerialFD == -1 ) {
		fprintf(stderr, "ERROR: Unable to open coke serial port ('%s')\n", gsCoke_SerialPort);
	}
	else {
		int i;
		for( i = 0; i < 7; i ++ )
			gaCoke_CachedStatus[i] = -1;
		// Reset the slot names.
		// - Dunno why this is needed, but the machine plays silly
		//   sometimes.
		Writef("n0 Slot0\n");
		if( !WaitForColon() )
		{
			Writef("n1 Slot1\n");
			WaitForColon();
			Writef("n2 Slot2\n");
			WaitForColon();
			Writef("n3 Slot3\n");
			WaitForColon();
			Writef("n4 Slot4\n");
			WaitForColon();
			Writef("n5 Slot5\n");
			WaitForColon();
			Writef("n6 Coke\n");
			
			Coke_int_UpdateSlotStatuses();
		}
	}
	
	AddPeriodicFunction(Coke_int_UpdateSlotStatuses);
	
	return 0;
}

int Coke_int_GetSlotStatus(char *Buffer, int Slot)
{
	regmatch_t	matches[4];
	 int	ret;
	char	*status;	
	
	// Parse status response
	ret = RunRegex(&gCoke_StatusRegex, Buffer, sizeof(matches)/sizeof(matches[0]), matches, "Bad Response");
	if( ret ) {
		return -1;
	}

	// Get slot status
	Buffer[ matches[3].rm_eo ] = '\0';
	status = &Buffer[ matches[3].rm_so ];
	
	TRACE("Machine responded slot %i status '%s'\n", Slot, status);

	if( strcmp(status, "full") == 0 ) {
		gaCoke_CachedStatus[Slot] = 0;	// 0: Avaliiable
		return 0;
	}
	else {
		gaCoke_CachedStatus[Slot] = 1;	// 1: Empty
		return 1;
	}
}

/**
 * \brief Update the status of all coke slots
 * \note Uses goto to reduce the chance of the lock being kept
 */
void Coke_int_UpdateSlotStatuses(void)
{
	 int	i, len;
	char	tmp[40];
	
	if( giCoke_SerialFD == -1 )	return ;
	
	pthread_mutex_lock(&gCoke_Mutex);
	
	while( ReadLine(sizeof tmp, tmp) >= 0 )	;
	TRACE("send d7\n");
	Writef("d7\r\n");	// Update slot statuses
	if( WaitForColon() )	goto ret;
	TRACE("send s\n");
	Writef("s\r\n");
	do {
		i = ReadLine(sizeof tmp, tmp);	// Read back what we just said
		if( i == -1 ) {
			TRACE("Eat read failed");
			goto ret;
		}
	} while(tmp[0] != ':' && tmp[1] != 's');
	
	for( i = 0; i <= 6; i ++ )
	{
		len = ReadLine(sizeof tmp, tmp);
		if( len == -1 ) {
			TRACE("Read failed on slot %i\n", i);
			goto ret;	// I give up :(
		}
		TRACE("tmp = '%s'\n", tmp);
		Coke_int_GetSlotStatus(tmp, i);
	}
	// Eat blank line
	len = ReadLine(sizeof tmp, tmp);
	if( len == -1 ) {
		TRACE("Read failed on blank line\n");
	}

	TRACE("Updated\n");

ret:
	pthread_mutex_unlock(&gCoke_Mutex);
}

int Coke_CanDispense(int UNUSED(User), int Item)
{
	// Sanity please
	if( Item < 0 || Item > 6 )	return -1;	// -EYOURBAD
	
	// Can't dispense if the machine is not connected
	if( giCoke_SerialFD == -1 )
		return -2;
	
	return gaCoke_CachedStatus[Item];
}

/**
 * \brief Actually do a dispense from the coke machine
 */
int Coke_DoDispense(int UNUSED(User), int Item)
{
	char	tmp[32];
	 int	ret, len;

	// Sanity please
	if( Item < 0 || Item > 6 )	return -1;

	// Can't dispense if the machine is not connected
	if( giCoke_SerialFD == -1 )
		return -2;
	
	// LOCK
	pthread_mutex_lock(&gCoke_Mutex);
	
	TRACE("flushing input\n");
	

	// Wait for prompt
	ret = 0;
	while( WaitForColon() && ret < 3 )
	{
		// Flush the input buffer
		char	tmpbuf[512];
		read(giCoke_SerialFD, tmpbuf, sizeof(tmpbuf));
		TRACE("sending 'd7'\n");
		Writef("d7\r\n");
		ret ++;
	}
	if( ret == 3 )
	{
		TRACE("timed out\n");
		pthread_mutex_unlock(&gCoke_Mutex);
		return -1;
	}

	TRACE("sending 'd%i'\n", Item);
	// Dispense
	Writef("d%i\r\n", Item);
	
	// Read empty lines and echo-backs
	do {
		ret = ReadLine(sizeof(tmp)-1, tmp);
		if( ret == -1 ) {
			pthread_mutex_unlock(&gCoke_Mutex);
			return -1;
		}
		TRACE("read %i '%s'\n", ret, tmp);
	} while( ret == 0 || tmp[0] == ':' || tmp[0] == 'd' );

	WaitForColon();	// Eat up rest of response
	
	TRACE("done\n");

	// TODO: Regex instead?
	if( strcmp(tmp, "ok") == 0 ) {
		// We think dispense worked
		// - The machine returns 'ok' if an empty slot is dispensed, even if
		//   it doesn't actually try to dispense (no sound)
		ret = 0;
	}
	else {
		printf("Coke_DoDispense: Machine returned unknown value '%s'\n", tmp);
		ret = -1;
	}
	
	TRACE("Updating slot status\n");
	
	// Update status
	WaitForColon();
	Writef("s%i\r\n", Item);
	len = ReadLine(sizeof tmp, tmp);
	if(len == -1)	gaCoke_CachedStatus[Item] = -1;
	Coke_int_GetSlotStatus(tmp, Item);
	{
		char buf[512];
		read(giCoke_SerialFD, buf, 512);	// Flush
	}
	
	// Release and return
	pthread_mutex_unlock(&gCoke_Mutex);
	
	//return ret;
	// HACK!!!
	return 0;
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
		printf("ReadChar: select return %i\n", ret);
		return 0;
	}
	
	ret = read(giCoke_SerialFD, &ch, 1);
	if( ret != 1 ) {
		printf("ReadChar: ret != 1 (%i)\n", ret);
		return 0;
	}
	
	return ch;
}

int Writef(const char *Format, ...)
{
	va_list	args;
	 int	len;
	
	va_start(args, Format);
	len = vsnprintf(NULL, 0, Format, args);
	va_end(args);
	
	{
		char	buf[len+1];
		va_start(args, Format);
		vsnprintf(buf, len+1, Format, args);
		va_end(args);
		
		#if DEBUG
		printf("Writef: %s", buf);
		#endif
		
		return write(giCoke_SerialFD, buf, len);
	}
	
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

	if( !ch ) 	return -1;
	return i;
}


