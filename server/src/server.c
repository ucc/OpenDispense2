/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * server.c - Client Server Code
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include <sys/socket.h>
#include <string.h>

#define MAX_CONNECTION_QUEUE	5
#define INPUT_BUFFER_SIZE	128

#define MSG_STR_TOO_LONG	"499 Command too long (limit "EXPSTR(INPUT_BUFFER_SIZE)")\n"

// === TYPES ===
typedef struct sClient
{
	 int	ID;	// Client ID
	
	char	*Username;
	char	Salt[9];
	
	 int	UID;
	 int	bIsAuthed;
}	tClient;

// === PROTOTYPES ===
void	Server_Start(void);
void	Server_HandleClient(int Socket);
char	*Server_ParseClientCommand(tClient *Client, char *CommandString);
char	*Server_Cmd_USER(tClient *Client, char *Args);

// === GLOBALS ===
 int	giServer_Port = 1020;
 int	giServer_NextClientID = 1;
// - Commands
struct sClientCommand {
	char	*Name;
	char	*(*Function)(tClient *Client, char *Arguments);
}	gaServer_Commands[] = {
	{"USER", Server_Cmd_USER}
};
#define NUM_COMMANDS	(sizeof(gaServer_Commands)/sizeof(gaServer_Commands[0]))

// === CODE ===
/**
 * \brief Open listenting socket and serve connections
 */
void Server_Start(void)
{
	// Create Server
}

/**
 * \brief Reads from a client socket and parses the command strings
 */
void Server_HandleClient(int Socket)
{
	char	inbuf[INPUT_BUFFER_SIZE];
	char	*buf = inbuf;
	 int	remspace = INPUT_BUFFER_SIZE-1;
	 int	bytes = -1;
	tClient	clientInfo = {0};
	
	// Initialise Client info
	clientInfo.ID = giServer_NextClientID ++;
		
	// Read from client
	/*
	 * Notes:
	 * - The `buf` and `remspace` variables allow a line to span several
	 *   calls to recv(), if a line is not completed in one recv() call
	 *   it is saved to the beginning of `inbuf` and `buf` is updated to
	 *   the end of it.
	 */
	while( (bytes = recv(Socket, buf, remspace, 0)) > 0 )
	{
		char	*eol, *start;
		buf[bytes] = '\0';	// Allow us to use stdlib string functions on it
		
		// Split by lines
		start = inbuf;
		while( (eol = strchr(start, '\n')) )
		{
			char	*ret;
			*eol = '\0';
			ret = Server_ParseClientCommand(&clientInfo, start);
			// `ret` is a string on the heap
			send(Socket, ret, strlen(ret), 0);
			free(ret);
			start = eol + 1;
		}
		
		// Check if there was an incomplete line
		if( *start != '\0' ) {
			 int	tailBytes = bytes - (start-buf);
			// Roll back in buffer
			memcpy(inbuf, start, tailBytes);
			remspace -= tailBytes;
			if(remspace == 0) {
				send(Socket, MSG_STR_TOO_LONG, sizeof(MSG_STR_TOO_LONG), 0);
				buf = inbuf;
				remspace = INPUT_BUFFER_SIZE - 1;
			}
		}
		else {
			buf = inbuf;
			remspace = INPUT_BUFFER_SIZE - 1;
		}
	}
	
	// Check for errors
	if( bytes < 0 ) {
		fprintf(stderr, "ERROR: Unable to recieve from client on socket %i\n", Socket);
		return ;
	}
}

/**
 * \brief Parses a client command and calls the required helper function
 * \param Client	Pointer to client state structure
 */
char *Server_ParseClientCommand(tClient *Client, char *CommandString)
{
	char	*space, *args;
	 int	i;
	
	// Split at first space
	space = strchr(CommandString, ' ');
	if(space == NULL) {
		args = NULL;
	}
	else {
		*space = '\0';
		args = space + 1;
	}
	
	// Find command
	for( i = 0; i < NUM_COMMANDS; i++ )
	{
		if(strcmp(CommandString, gaServer_Commands[i].Name) == 0)
			return gaServer_Commands[i].Function(Client, args);
	}
	
	return strdup("400	Unknown Command\n");
}

// ---
// Commands
// ---
/**
 * \brief Set client username
 */
char *Server_Cmd_USER(tClient *Client, char *Args)
{
	char	*ret;
	
	// Debug!
	if( giDebugLevel )
		printf("Client %i authenticating as '%s'\n", Client->ID, Args);
	
	// Save username
	if(Client->Username)
		free(Client->Username);
	Client->Username = strdup(Args);
	
	// Create a salt (that changes if the username is changed)
	Client->Salt[0] = 0x21 + (rand()&0x3F);
	Client->Salt[1] = 0x21 + (rand()&0x3F);
	Client->Salt[2] = 0x21 + (rand()&0x3F);
	Client->Salt[3] = 0x21 + (rand()&0x3F);
	Client->Salt[4] = 0x21 + (rand()&0x3F);
	Client->Salt[5] = 0x21 + (rand()&0x3F);
	Client->Salt[6] = 0x21 + (rand()&0x3F);
	Client->Salt[7] = 0x21 + (rand()&0x3F);
	
	// "100 Salt xxxxXXXX\n"
	ret = strdup("100 SALT xxxxXXXX\n");
	sprintf(ret, "100 SALT %s\n", Client->Salt);
	
	return ret;
}
