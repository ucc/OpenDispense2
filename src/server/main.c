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
extern const char	*gsItemListFile;
extern const char	*gsCoke_ModbusAddress;
extern const char	*gsDoor_SerialPort;

// === PROTOTYPES ===
void	*Periodic_Thread(void *Unused);

// === GLOBALS ===
 int	giDebugLevel = 0;
 int	gbNoCostMode = 0;
const char	*gsCokebankPath = "cokebank.db";
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
	fprintf(stderr, "  -f,--configfile\n");
	fprintf(stderr, "        Set the config file path (default `dispsrv.conf')\n");
	fprintf(stderr, "  -d    Set debug level (0 - 2, default 0)\n");
	fprintf(stderr, "  --[dont-]daemonise\n");
	fprintf(stderr, "        Run (or explicitly don't run) the server disconnected from the terminal\n");
}

int main(int argc, char *argv[])
{
	 int	i;
	const char	*config_file = "dispsrv.conf";

	// Parse Arguments
	for( i = 1; i < argc; i++ )
	{
		char	*arg = argv[i];
		if( arg[0] == '-' && arg[1] != '-')
		{
			switch(arg[1])
			{
			case 'f':
				if( i + 1 >= argc )	return -1;
				config_file = argv[++i];
				break;
			case 'd':
				if( i + 1 >= argc )	return -1;
				Config_AddValue("debug_level", argv[++i]);
				giDebugLevel = atoi(argv[i]);
				break;
			default:
				// Usage Error
				fprintf(stderr, "Unknown option '-%c'\n", arg[1]);
				PrintUsage(argv[0]);
				return -1;
			}
		}
		else if( arg[0] == '-' && arg[1] == '-' )
		{
			if( strcmp(arg, "--configfile") == 0 ) {
				if( i + 1 >= argc )	return -1;
				config_file = argv[++i];
			}
			else if( strcmp(arg, "--daemonise") == 0 ) {
				Config_AddValue("daemonise", "true");
			}
			else if( strcmp(arg, "--dont-daemonise") == 0 ) {
				Config_AddValue("daemonise", "false");
			}
			else {
				// Usage error
				fprintf(stderr, "Unknown option '%s'\n", arg);
				PrintUsage(argv[0]);
				return -1;
			}
		}
		else
		{
			// Usage Error
			PrintUsage(argv[0]);
			return -1;
		}
	}

	Config_ParseFile( config_file );

	// Parse config values
	gbServer_RunInBackground = Config_GetValue_Bool("daemonise", 0);
	gsCokebankPath       = Config_GetValue("cokebank_database", 0);
	gsDoor_SerialPort    = Config_GetValue("door_serial_port", 0);
	gsCoke_ModbusAddress = Config_GetValue("coke_modbus_address", 0);
	giServer_Port        = Config_GetValue_Int("server_port", 0);
	gsItemListFile       = Config_GetValue("items_file", 0);

	gbNoCostMode         = (Config_GetValue_Bool("test_mode", 0) == 1);

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);
	
	openlog("odispense2", 0, LOG_LOCAL4);
	
	if( Bank_Initialise(gsCokebankPath) )
		return -1;

	Init_Handlers();

	Load_Itemlist();
	
	Server_Start();
	
	if(gTimerThread)
		pthread_kill(gTimerThread, SIGKILL);

	return 0;
}

void *Periodic_Thread(void *Unused __attribute__((unused)))
{
	 int	i;
	
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
		fprintf(stderr, "Regex compilation failed - %s\n%s\n", errorStr, pattern);
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
	case 115200:	baud = B115200;	break;
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

