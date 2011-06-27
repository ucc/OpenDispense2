/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * main.c - Initialisation Code
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "common.h"
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <syslog.h>
#include <pthread.h>
#include "../cokebank.h"

// === IMPORTS ===
extern void	Init_Handlers(void);
extern void	Load_Itemlist(void);
extern void	Server_Start(void);
extern int	gbServer_RunInBackground;
extern int	giServer_Port;
extern char	*gsItemListFile;
extern char	*gsCoke_SerialPort;
extern char	*gsSnack_SerialPort;
extern char	*gsDoor_SerialPort;

// === PROTOTYPES ===
void	*Periodic_Thread(void *Unused);

// === GLOBALS ===
 int	giDebugLevel = 0;
char	*gsCokebankPath = "cokebank.db";
// - Functions called every 20s (or so)
#define ciMaxPeriodics	10
struct sPeriodicCall {
	void	(*Function)(void);
}	gaPeriodicCalls[ciMaxPeriodics];
pthread_t	gTimerThread;

// === CODE ===
void sigint_handler()
{
	exit(0);
}

void PrintUsage(const char *progname)
{
	fprintf(stderr, "Usage: %s\n", progname);
	fprintf(stderr, "  -p    Set server port (default 11020)\n");
	fprintf(stderr, "  -d    Set debug level (0 - 2, default 0)\n");
	fprintf(stderr, "  --itemsfile\n");
	fprintf(stderr, "        Set debug level (0 - 2, default 0)\n");
	fprintf(stderr, "  --cokeport\n");
	fprintf(stderr, "        Coke machine serial port (Default \"/dev/ttyS0\")\n");
	fprintf(stderr, "  --doorport\n");
	fprintf(stderr, "        Door modem/relay serial port (Default \"/dev/ttyS3\")\n");
	fprintf(stderr, "  --cokebank\n");
	fprintf(stderr, "        Coke bank database file (Default \"cokebank.db\")\n");
	fprintf(stderr, "  --[dont-]daemonise\n");
	fprintf(stderr, "        Run (or explicitly don't) the server disconnected from the terminal\n");
}

int main(int argc, char *argv[])
{
	 int	i;
	
	// Parse Arguments
	for( i = 1; i < argc; i++ )
	{
		char	*arg = argv[i];
		if( arg[0] == '-' && arg[1] != '-')
		{
			switch(arg[1])
			{
			case 'p':
				if( i + 1 >= argc )	return -1;
				giServer_Port = atoi(argv[++i]);
				break;
			case 'd':
				if( i + 1 >= argc )	return -1;
				giDebugLevel = atoi(argv[++i]);
				break;
			case 'D':
				gbServer_RunInBackground = 1;
				return -1;
			default:
				// Usage Error?
				PrintUsage(argv[0]);
				return -1;
			}
		}
		else if( arg[0] == '-' && arg[1] == '-' ) {
			if( strcmp(arg, "--itemsfile") == 0 ) {
				if( i + 1 >= argc )	return -1;
				gsItemListFile = argv[++i];
			}
			else if( strcmp(arg, "--cokeport") == 0 ) {
				if( i + 1 >= argc )	return -1;
				gsCoke_SerialPort = argv[++i];
			}
			else if( strcmp(arg, "--snackport") == 0 ) {
				if( i + 1 >= argc )	return -1;
				gsSnack_SerialPort = argv[++i];
			}
			else if( strcmp(arg, "--doorport") == 0 ) {
				if( i + 1 >= argc )	return -1;
				gsDoor_SerialPort = argv[++i];
			}
			else if( strcmp(arg, "--cokebank") == 0 ) {
				if( i + 1 >= argc )	return -1;
				gsCokebankPath = argv[++i];
			}
			else if( strcmp(arg, "--daemonise") == 0 ) {
				gbServer_RunInBackground = 1;
			}
			else if( strcmp(arg, "--dont-daemonise") == 0 ) {
				gbServer_RunInBackground = 0;
			}
			else {
				// Usage error?
				PrintUsage(argv[0]);
				return -1;
			}
		}
		else {
			// Usage Error?
			PrintUsage(argv[0]);
			return -1;
		}
	}
	
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);
	
	openlog("odispense2", 0, LOG_LOCAL4);
	
	if( Bank_Initialise(gsCokebankPath) )
		return -1;

	Init_Handlers();

	Load_Itemlist();
	
	Server_Start();
	
	pthread_kill(gTimerThread, SIGKILL);

	return 0;
}

void *Periodic_Thread(void *Unused)
{
	 int	i;
	Unused = NULL;	// quiet, gcc
	
	for( ;; )
	{
		sleep(10);	// Sleep for a while
//		printf("Periodic firing\n");
		for( i = 0; i < ciMaxPeriodics; i ++ )
		{
			if( gaPeriodicCalls[i].Function )
				gaPeriodicCalls[i].Function();
		}
	}
	return NULL;
}

void StartPeriodicThread(void)
{
	pthread_create( &gTimerThread, NULL, Periodic_Thread, NULL );
}

void AddPeriodicFunction(void (*Fcn)(void))
{
	int i;
	for( i = 0; i < ciMaxPeriodics; i ++ )
	{
		if( gaPeriodicCalls[i].Function )	continue;
		gaPeriodicCalls[i].Function = Fcn;
		return ;
	}
	
	fprintf(stderr, "Error: No space for %p in periodic list\n", Fcn);
}

int RunRegex(regex_t *regex, const char *string, int nMatches, regmatch_t *matches, const char *errorMessage)
{
	 int	ret;
	
	ret = regexec(regex, string, nMatches, matches, 0);
	if( ret == REG_NOMATCH ) {
		return -1;
	}
	if( ret ) {
		size_t  len = regerror(ret, regex, NULL, 0);
		char    errorStr[len];
		regerror(ret, regex, errorStr, len);
		printf("string = '%s'\n", string);
		fprintf(stderr, "%s\n%s", errorMessage, errorStr);
		exit(-1);
	}
	
	return ret;
}

void CompileRegex(regex_t *regex, const char *pattern, int flags)
{
	 int	ret = regcomp(regex, pattern, flags);
	if( ret ) {
		size_t	len = regerror(ret, regex, NULL, 0);
		char    errorStr[len];
		regerror(ret, regex, errorStr, len);
		fprintf(stderr, "Regex compilation failed - %s\n", errorStr);
		exit(-1);
	}
}

// Serial helper
int InitSerial(const char *File, int BaudRate)
{
	struct termios	info;
	 int	baud;
	 int	fd;
	
	fd = open(File, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if( fd == -1 )	return -1;
	
	switch(BaudRate)
	{
	case 1200:	baud = B1200;	break;
	case 9600:	baud = B9600;	break;
	default:	close(fd);	return -1;
	}
	
	info.c_lflag = 0;	// Non-Canoical, No Echo
	info.c_cflag = baud | CS8 | CLOCAL | CREAD;	// baud, 8N1
	info.c_iflag = IGNCR;	// Ignore \r
	info.c_oflag = 0;	// ???
	cfsetspeed(&info, baud);
	info.c_cc[VTIME] = 0;	// No time limit
	info.c_cc[VMIN] = 1;	// Block until 1 char
	
	tcflush(fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &info);
	
	return fd;
}


/**
 * \brief Create a formatted heap string
 */
char *mkstr(const char *Format, ...)
{
	va_list	args;
	 int	len;
	char	*ret;

	va_start(args, Format);
	len = vsnprintf(NULL, 0, Format, args);
	va_end(args);

	ret = malloc( len + 1 );
	if(!ret)	return NULL;

	va_start(args, Format);
	vsprintf(ret, Format, args);
	va_end(args);
	
	return ret;
}

