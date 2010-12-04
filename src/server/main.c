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

// === IMPORTS ===
extern void	Init_Cokebank(const char *Argument);	// cokebank.c
extern void	Init_Handlers(void);
extern void	Load_Itemlist(void);
extern void	Server_Start(void);
extern int	giServer_Port;
extern char*	gsItemListFile;
extern char*	gsCoke_SerialPort;
extern char*	gsSnack_SerialPort;

// === GLOBALS ===
 int	giDebugLevel = 0;
char	*gsCokebankPath = "cokebank.db";

// === CODE ===
void sigint_handler()
{
	exit(0);
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
				giServer_Port = atoi(argv[++i]);
				break;
			case 'd':
				giDebugLevel = atoi(argv[++i]);
				break;
			default:
				// Usage Error?
				break;
			}
		}
		else if( arg[0] == '-' && arg[1] == '-' ) {
			if( strcmp(arg, "--itemsfile") == 0 ) {
				gsItemListFile = argv[++i];
			}
			else if( strcmp(arg, "--cokeport") == 0 ) {
				gsCoke_SerialPort = argv[++i];
			}
			else if( strcmp(arg, "--snackport") == 0 ) {
				gsSnack_SerialPort = argv[++i];
			}
			else {
				// Usage error?
			}
		}
		else {
			// Usage Error?
		}
	}
	
	signal(SIGINT, sigint_handler);
	
	Init_Cokebank(gsCokebankPath);

	Init_Handlers();

	Load_Itemlist();
	
	Server_Start();
	

	return 0;
}

int RunRegex(regex_t *regex, const char *string, int nMatches, regmatch_t *matches, const char *errorMessage)
{
	 int	ret;
	
	ret = regexec(regex, string, nMatches, matches, 0);
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
	
	
	fd = open(File, O_RDWR | O_NOCTTY);
	if( fd == -1 )	return -1;
	
	switch(BaudRate)
	{
	case 9600:	baud = B9600;	break;
	default:	return ;
	}
	
	cfmakeraw(&info);	// Sets 8N1
	cfsetspeed(&info, baud);
	
	tcsetattr(fd, TCSANOW, &info);
	
	return fd;
}


