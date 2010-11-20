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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#define MAX_CONNECTION_QUEUE	5
#define INPUT_BUFFER_SIZE	256

#define HASH_TYPE	SHA512
#define HASH_LENGTH	64

#define MSG_STR_TOO_LONG	"499 Command too long (limit "EXPSTR(INPUT_BUFFER_SIZE)")\n"

// === TYPES ===
typedef struct sClient
{
	 int	ID;	// Client ID
	 
	 int	bIsTrusted;	// Is the connection from a trusted host/port
	
	char	*Username;
	char	Salt[9];
	
	 int	UID;
	 int	bIsAuthed;
}	tClient;

// === PROTOTYPES ===
void	Server_Start(void);
void	Server_HandleClient(int Socket, int bTrusted);
char	*Server_ParseClientCommand(tClient *Client, char *CommandString);
// --- Commands ---
char	*Server_Cmd_USER(tClient *Client, char *Args);
char	*Server_Cmd_PASS(tClient *Client, char *Args);
char	*Server_Cmd_AUTOAUTH(tClient *Client, char *Args);
// --- Helpers ---
void	HexBin(uint8_t *Dest, char *Src, int BufSize);

// === GLOBALS ===
 int	giServer_Port = 1020;
 int	giServer_NextClientID = 1;
// - Commands
struct sClientCommand {
	char	*Name;
	char	*(*Function)(tClient *Client, char *Arguments);
}	gaServer_Commands[] = {
	{"USER", Server_Cmd_USER},
	{"PASS", Server_Cmd_PASS},
	{"AUTOAUTH", Server_Cmd_AUTOAUTH}
};
#define NUM_COMMANDS	(sizeof(gaServer_Commands)/sizeof(gaServer_Commands[0]))

// === CODE ===
/**
 * \brief Open listenting socket and serve connections
 */
void Server_Start(void)
{
	 int	server_socket, client_socket;
	struct sockaddr_in	server_addr, client_addr;

	// Create Server
	server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if( server_socket < 0 ) {
		fprintf(stderr, "ERROR: Unable to create server socket\n");
		return ;
	}
	
	// Make listen address
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;	// Internet Socket
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);	// Listen on all interfaces
	server_addr.sin_port = htons(giServer_Port);	// Port

	// Bind
	if( bind(server_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0 ) {
		fprintf(stderr, "ERROR: Unable to bind to 0.0.0.0:%i\n", giServer_Port);
		return ;
	}
	
	// Listen
	if( listen(server_socket, MAX_CONNECTION_QUEUE) < 0 ) {
		fprintf(stderr, "ERROR: Unable to listen to socket\n");
		return ;
	}
	
	printf("Listening on 0.0.0.0:%i\n", giServer_Port);
	
	for(;;)
	{
		uint	len = sizeof(client_addr);
		 int	bTrusted = 0;
		
		client_socket = accept(server_socket, (struct sockaddr *) &client_addr, &len);
		if(client_socket < 0) {
			fprintf(stderr, "ERROR: Unable to accept client connection\n");
			return ;
		}
		
		if(giDebugLevel >= 2) {
			char	ipstr[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &client_addr.sin_addr, ipstr, INET_ADDRSTRLEN);
			printf("Client connection from %s:%i\n",
				ipstr, ntohs(client_addr.sin_port));
		}
		
		// Trusted Connections
		if( ntohs(client_addr.sin_port) < 1024 )
		{
			// TODO: Make this runtime configurable
			switch( ntohl( client_addr.sin_addr.s_addr ) )
			{
			case 0x7F000001:	// 127.0.0.1	localhost
			//case 0x825E0D00:	// 130.95.13.0
			case 0x825E0D12:	// 130.95.13.18	mussel
			case 0x825E0D17:	// 130.95.13.23	martello
				bTrusted = 1;
				break;
			default:
				break;
			}
		}
		
		// TODO: Multithread this?
		Server_HandleClient(client_socket, bTrusted);
		
		close(client_socket);
	}
}

/**
 * \brief Reads from a client socket and parses the command strings
 * \param Socket	Client socket number/handle
 * \param bTrusted	Is the client trusted?
 */
void Server_HandleClient(int Socket, int bTrusted)
{
	char	inbuf[INPUT_BUFFER_SIZE];
	char	*buf = inbuf;
	 int	remspace = INPUT_BUFFER_SIZE-1;
	 int	bytes = -1;
	tClient	clientInfo = {0};
	
	// Initialise Client info
	clientInfo.ID = giServer_NextClientID ++;
	clientInfo.bIsTrusted = bTrusted;
	
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
	
	if(giDebugLevel >= 2) {
		printf("Client %i: Disconnected\n", clientInfo.ID);
	}
}

/**
 * \brief Parses a client command and calls the required helper function
 * \param Client	Pointer to client state structure
 * \param CommandString	Command from client (single line of the command)
 * \return Heap String to return to the client
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
	
	return strdup("400 Unknown Command\n");
}

// ---
// Commands
// ---
/**
 * \brief Set client username
 * 
 * Usage: USER <username>
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
	
	#if USE_SALT
	// Create a salt (that changes if the username is changed)
	// Yes, I know, I'm a little paranoid, but who isn't?
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
	#else
	ret = strdup("100 User Set\n");
	#endif
	return ret;
}

/**
 * \brief Authenticate as a user
 * 
 * Usage: PASS <hash>
 */
char *Server_Cmd_PASS(tClient *Client, char *Args)
{
	uint8_t	clienthash[HASH_LENGTH] = {0};
	
	// Read user's hash
	HexBin(clienthash, Args, HASH_LENGTH);
	
	if( giDebugLevel ) {
		 int	i;
		printf("Client %i: Password hash ", Client->ID);
		for(i=0;i<HASH_LENGTH;i++)
			printf("%02x", clienthash[i]&0xFF);
		printf("\n");
	}
	
	return strdup("401 Auth Failure\n");
}

/**
 * \brief Authenticate as a user without a password
 * 
 * Usage: AUTOAUTH <user>
 */
char *Server_Cmd_AUTOAUTH(tClient *Client, char *Args)
{
	char	*spos = strchr(Args, ' ');
	if(spos)	*spos = '\0';	// Remove characters after the ' '
	
	// Check if trusted
	if( !Client->bIsTrusted ) {
		if(giDebugLevel)
			printf("Client %i: Untrusted client attempting to AUTOAUTH\n", Client->ID);
		return strdup("401 Untrusted\n");
	}
	
	// Get UID
	Client->UID = GetUserID( Args );
	if( Client->UID < 0 ) {
		if(giDebugLevel)
			printf("Client %i: Unknown user '%s'\n", Client->ID, Args);
		return strdup("401 Auth Failure\n");
	}
	
	if(giDebugLevel)
		printf("Client %i: Authenticated as '%s' (%i)\n", Client->ID, Args, Client->UID);
	
	return strdup("200 Auth OK\n");
}

// --- INTERNAL HELPERS ---
// TODO: Move to another file
void HexBin(uint8_t *Dest, char *Src, int BufSize)
{
	 int	i;
	for( i = 0; i < BufSize; i ++ )
	{
		uint8_t	val = 0;
		
		if('0' <= *Src && *Src <= '9')
			val |= (*Src-'0') << 4;
		else if('A' <= *Src && *Src <= 'F')
			val |= (*Src-'A'+10) << 4;
		else if('a' <= *Src && *Src <= 'f')
			val |= (*Src-'a'+10) << 4;
		else
			break;
		Src ++;
		
		if('0' <= *Src && *Src <= '9')
			val |= (*Src-'0');
		else if('A' <= *Src && *Src <= 'F')
			val |= (*Src-'A'+10);
		else if('a' <= *Src && *Src <= 'f')
			val |= (*Src-'a'+10);
		else
			break;
		Src ++;
		
		Dest[i] = val;
	}
	for( ; i < BufSize; i++ )
		Dest[i] = 0;
}

/**
 * \brief Decode a Base64 value
 */
int UnBase64(uint8_t *Dest, char *Src, int BufSize)
{
	uint32_t	val;
	 int	i, j;
	char	*start_src = Src;
	
	for( i = 0; i+2 < BufSize; i += 3 )
	{
		val = 0;
		for( j = 0; j < 4; j++, Src ++ ) {
			if('A' <= *Src && *Src <= 'Z')
				val |= (*Src - 'A') << ((3-j)*6);
			else if('a' <= *Src && *Src <= 'z')
				val |= (*Src - 'a' + 26) << ((3-j)*6);
			else if('0' <= *Src && *Src <= '9')
				val |= (*Src - '0' + 52) << ((3-j)*6);
			else if(*Src == '+')
				val |= 62 << ((3-j)*6);
			else if(*Src == '/')
				val |= 63 << ((3-j)*6);
			else if(!*Src)
				break;
			else if(*Src != '=')
				j --;	// Ignore invalid characters
		}
		Dest[i  ] = (val >> 16) & 0xFF;
		Dest[i+1] = (val >> 8) & 0xFF;
		Dest[i+2] = val & 0xFF;
		if(j != 4)	break;
	}
	
	// Finish things off
	if(i   < BufSize)
		Dest[i] = (val >> 16) & 0xFF;
	if(i+1 < BufSize)
		Dest[i+1] = (val >> 8) & 0xFF;
	
	return Src - start_src;
}
