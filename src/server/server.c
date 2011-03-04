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
#include <limits.h>
#include <stdarg.h>
#include <signal.h>

#define	DEBUG_TRACE_CLIENT	0

// Statistics
#define MAX_CONNECTION_QUEUE	5
#define INPUT_BUFFER_SIZE	256
#define CLIENT_TIMEOUT	10	// Seconds

#define HASH_TYPE	SHA1
#define HASH_LENGTH	20

#define MSG_STR_TOO_LONG	"499 Command too long (limit "EXPSTR(INPUT_BUFFER_SIZE)")\n"

// === TYPES ===
typedef struct sClient
{
	 int	Socket;	// Client socket ID
	 int	ID;	// Client ID
	 
	 int	bIsTrusted;	// Is the connection from a trusted host/port
	
	char	*Username;
	char	Salt[9];
	
	 int	UID;
	 int	EffectiveUID;
	 int	bIsAuthed;
}	tClient;

// === PROTOTYPES ===
void	Server_Start(void);
void	Server_Cleanup(void);
void	Server_HandleClient(int Socket, int bTrusted);
void	Server_ParseClientCommand(tClient *Client, char *CommandString);
// --- Commands ---
void	Server_Cmd_USER(tClient *Client, char *Args);
void	Server_Cmd_PASS(tClient *Client, char *Args);
void	Server_Cmd_AUTOAUTH(tClient *Client, char *Args);
void	Server_Cmd_SETEUSER(tClient *Client, char *Args);
void	Server_Cmd_ENUMITEMS(tClient *Client, char *Args);
void	Server_Cmd_ITEMINFO(tClient *Client, char *Args);
void	Server_Cmd_DISPENSE(tClient *Client, char *Args);
void	Server_Cmd_REFUND(tClient *Client, char *Args);
void	Server_Cmd_GIVE(tClient *Client, char *Args);
void	Server_Cmd_DONATE(tClient *Client, char *Args);
void	Server_Cmd_ADD(tClient *Client, char *Args);
void	Server_Cmd_SET(tClient *Client, char *Args);
void	Server_Cmd_ENUMUSERS(tClient *Client, char *Args);
void	Server_Cmd_USERINFO(tClient *Client, char *Args);
void	_SendUserInfo(tClient *Client, int UserID);
void	Server_Cmd_USERADD(tClient *Client, char *Args);
void	Server_Cmd_USERFLAGS(tClient *Client, char *Args);
void	Server_Cmd_UPDATEITEM(tClient *Client, char *Args);
// --- Helpers ---
void	Debug(tClient *Client, const char *Format, ...);
 int	sendf(int Socket, const char *Format, ...);
 int	Server_int_ParseArgs(int bUseLongArg, char *ArgStr, ...);
 int	Server_int_ParseFlags(tClient *Client, const char *Str, int *Mask, int *Value);

// === CONSTANTS ===
// - Commands
const struct sClientCommand {
	const char	*Name;
	void	(*Function)(tClient *Client, char *Arguments);
}	gaServer_Commands[] = {
	{"USER", Server_Cmd_USER},
	{"PASS", Server_Cmd_PASS},
	{"AUTOAUTH", Server_Cmd_AUTOAUTH},
	{"SETEUSER", Server_Cmd_SETEUSER},
	{"ENUM_ITEMS", Server_Cmd_ENUMITEMS},
	{"ITEM_INFO", Server_Cmd_ITEMINFO},
	{"DISPENSE", Server_Cmd_DISPENSE},
	{"REFUND", Server_Cmd_REFUND},
	{"GIVE", Server_Cmd_GIVE},
	{"DONATE", Server_Cmd_DONATE},
	{"ADD", Server_Cmd_ADD},
	{"SET", Server_Cmd_SET},
	{"ENUM_USERS", Server_Cmd_ENUMUSERS},
	{"USER_INFO", Server_Cmd_USERINFO},
	{"USER_ADD", Server_Cmd_USERADD},
	{"USER_FLAGS", Server_Cmd_USERFLAGS},
	{"UPDATE_ITEM", Server_Cmd_UPDATEITEM}
};
#define NUM_COMMANDS	((int)(sizeof(gaServer_Commands)/sizeof(gaServer_Commands[0])))

// === GLOBALS ===
// - Configuration
 int	giServer_Port = 11020;
 int	gbServer_RunInBackground = 0;
char	*gsServer_LogFile = "/var/log/dispsrv.log";
char	*gsServer_ErrorLog = "/var/log/dispsrv.err";
// - State variables
 int	giServer_Socket;	// Server socket
 int	giServer_NextClientID = 1;	// Debug client ID
 

// === CODE ===
/**
 * \brief Open listenting socket and serve connections
 */
void Server_Start(void)
{
	 int	client_socket;
	struct sockaddr_in	server_addr, client_addr;

	atexit(Server_Cleanup);
	// Ignore SIGPIPE (stops crashes when the client exits early)
	signal(SIGPIPE, SIG_IGN);

	// Create Server
	giServer_Socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if( giServer_Socket < 0 ) {
		fprintf(stderr, "ERROR: Unable to create server socket\n");
		return ;
	}
	
	// Make listen address
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;	// Internet Socket
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);	// Listen on all interfaces
	server_addr.sin_port = htons(giServer_Port);	// Port

	// Bind
	if( bind(giServer_Socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0 ) {
		fprintf(stderr, "ERROR: Unable to bind to 0.0.0.0:%i\n", giServer_Port);
		perror("Binding");
		return ;
	}

#if 0
	if( gbServer_RunInBackground )
	{
		int pid = fork();
		if( pid == -1 ) {
			fprintf(stderr, "ERROR: Unable to fork\n");
			perror("fork background");
			exit(-1);
		}
		if( pid != 0 ) {
			// Parent, quit
			exit(0);
		}
		// In child, sort out stdin/stdout
		reopen(0, "/dev/null", O_READ);
		reopen(1, gsServer_LogFile, O_CREAT|O_APPEND);
		reopen(2, gsServer_ErrorLog, O_CREAT|O_APPEND);
	}
#endif
	
	// Listen
	if( listen(giServer_Socket, MAX_CONNECTION_QUEUE) < 0 ) {
		fprintf(stderr, "ERROR: Unable to listen to socket\n");
		perror("Listen");
		return ;
	}
	
	printf("Listening on 0.0.0.0:%i\n", giServer_Port);
	
	// write pidfile
	{
		FILE *fp = fopen("/var/run/dispsrv.pid", "w");
		if( fp ) {
			fprintf(fp, "%i", getpid());
			fclose(fp);
		}
	}

	for(;;)
	{
		uint	len = sizeof(client_addr);
		 int	bTrusted = 0;
		
		// Accept a connection
		client_socket = accept(giServer_Socket, (struct sockaddr *) &client_addr, &len);
		if(client_socket < 0) {
			fprintf(stderr, "ERROR: Unable to accept client connection\n");
			return ;
		}
		
		// Set a timeout on the user conneciton
		{
			struct timeval tv;
			tv.tv_sec = CLIENT_TIMEOUT;
			tv.tv_usec = 0;
			if( setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) )
			{
				perror("setsockopt");
				return ;
			}
		}
		
		// Debug: Print the connection string
		if(giDebugLevel >= 2) {
			char	ipstr[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &client_addr.sin_addr, ipstr, INET_ADDRSTRLEN);
			printf("Client connection from %s:%i\n",
				ipstr, ntohs(client_addr.sin_port));
		}
		
		// Doesn't matter what, localhost is trusted
		if( ntohl( client_addr.sin_addr.s_addr ) == 0x7F000001 )
			bTrusted = 1;
		
		// Trusted Connections
		if( ntohs(client_addr.sin_port) < 1024 )
		{
			// TODO: Make this runtime configurable
			switch( ntohl( client_addr.sin_addr.s_addr ) )
			{
			case 0x7F000001:	// 127.0.0.1	localhost
		//	case 0x825F0D00:	// 130.95.13.0
			case 0x825F0D07:	// 130.95.13.7	motsugo
			case 0x825F0D11:	// 130.95.13.17	mermaid
			case 0x825F0D12:	// 130.95.13.18	mussel
			case 0x825F0D17:	// 130.95.13.23	martello
			case 0x825F0D2A:	// 130.95.13.42 meersau
			case 0x825F0D42:	// 130.95.13.66	heathred
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

void Server_Cleanup(void)
{
	printf("\nClose(%i)\n", giServer_Socket);
	close(giServer_Socket);
	unlink("/var/run/dispsrv");
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
	tClient	clientInfo;
	
	memset(&clientInfo, 0, sizeof(clientInfo));
	
	// Initialise Client info
	clientInfo.Socket = Socket;
	clientInfo.ID = giServer_NextClientID ++;
	clientInfo.bIsTrusted = bTrusted;
	clientInfo.EffectiveUID = -1;
	
	// Read from client
	/*
	 * Notes:
	 * - The `buf` and `remspace` variables allow a line to span several
	 *   calls to recv(), if a line is not completed in one recv() call
	 *   it is saved to the beginning of `inbuf` and `buf` is updated to
	 *   the end of it.
	 */
	// TODO: Use select() instead (to give a timeout)
	while( (bytes = recv(Socket, buf, remspace, 0)) > 0 )
	{
		char	*eol, *start;
		buf[bytes] = '\0';	// Allow us to use stdlib string functions on it
		
		// Split by lines
		start = inbuf;
		while( (eol = strchr(start, '\n')) )
		{
			*eol = '\0';
			
			Server_ParseClientCommand(&clientInfo, start);
			
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
void Server_ParseClientCommand(tClient *Client, char *CommandString)
{
	char	*command, *args;
	 int	i;
	
	if( giDebugLevel >= 2 )
		Debug(Client, "Server_ParseClientCommand: (CommandString = '%s')", CommandString);
	
	if( Server_int_ParseArgs(1, CommandString, &command, &args, NULL) )
	{
		if( command == NULL )	return ;
//		printf("command=%s, args=%s\n", command, args);
		// Is this an error? (just ignore for now)
		//args = "";
	}
	
	
	// Find command
	for( i = 0; i < NUM_COMMANDS; i++ )
	{
		if(strcmp(command, gaServer_Commands[i].Name) == 0) {
			if( giDebugLevel >= 2 )
				Debug(Client, "CMD %s - \"%s\"", command, args);
			gaServer_Commands[i].Function(Client, args);
			return ;
		}
	}
	
	sendf(Client->Socket, "400 Unknown Command\n");
}

// ---
// Commands
// ---
/**
 * \brief Set client username
 * 
 * Usage: USER <username>
 */
void Server_Cmd_USER(tClient *Client, char *Args)
{
	char	*username;
	
	if( Server_int_ParseArgs(0, Args, &username, NULL) )
	{
		sendf(Client->Socket, "407 USER takes 1 argument\n");
		return ;
	}
	
	// Debug!
	if( giDebugLevel )
		Debug(Client, "Authenticating as '%s'", username);
	
	// Save username
	if(Client->Username)
		free(Client->Username);
	Client->Username = strdup(username);
	
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
	
	// TODO: Also send hash type to use, (SHA1 or crypt according to [DAA])
	sendf(Client->Socket, "100 SALT %s\n", Client->Salt);
	#else
	sendf(Client->Socket, "100 User Set\n");
	#endif
}

/**
 * \brief Authenticate as a user
 * 
 * Usage: PASS <hash>
 */
void Server_Cmd_PASS(tClient *Client, char *Args)
{
	char	*passhash;
	 int	flags;

	if( Server_int_ParseArgs(0, Args, &passhash, NULL) )
	{
		sendf(Client->Socket, "407 PASS takes 1 argument\n");
		return ;
	}
	
	// Pass on to cokebank
	Client->UID = Bank_GetUserAuth(Client->Salt, Client->Username, passhash);

	if( Client->UID == -1 ) {
		sendf(Client->Socket, "401 Auth Failure\n");
		return ;
	}

	flags = Bank_GetFlags(Client->UID);
	if( flags & USER_FLAG_DISABLED ) {
		Client->UID = -1;
		sendf(Client->Socket, "403 Account Disabled\n");
		return ;
	}
	if( flags & USER_FLAG_INTERNAL ) {
		Client->UID = -1;
		sendf(Client->Socket, "403 Internal account\n");
		return ;
	}
	
	Client->bIsAuthed = 1;
	sendf(Client->Socket, "200 Auth OK\n");
}

/**
 * \brief Authenticate as a user without a password
 * 
 * Usage: AUTOAUTH <user>
 */
void Server_Cmd_AUTOAUTH(tClient *Client, char *Args)
{
	char	*username;
	 int	userflags;
	
	if( Server_int_ParseArgs(0, Args, &username, NULL) )
	{
		sendf(Client->Socket, "407 AUTOAUTH takes 1 argument\n");
		return ;
	}
	
	// Check if trusted
	if( !Client->bIsTrusted ) {
		if(giDebugLevel)
			Debug(Client, "Untrusted client attempting to AUTOAUTH");
		sendf(Client->Socket, "401 Untrusted\n");
		return ;
	}
	
	// Get UID
	Client->UID = Bank_GetAcctByName( username );	
	if( Client->UID < 0 ) {
		if(giDebugLevel)
			Debug(Client, "Unknown user '%s'", username);
		sendf(Client->Socket, "403 Auth Failure\n");
		return ;
	}
	
	userflags = Bank_GetFlags(Client->UID);
	// You can't be an internal account
	if( userflags & USER_FLAG_INTERNAL ) {
		if(giDebugLevel)
			Debug(Client, "Autoauth as '%s', not allowed", username);
		Client->UID = -1;
		sendf(Client->Socket, "403 Account is internal\n");
		return ;
	}

	// Disabled accounts
	if( userflags & USER_FLAG_DISABLED ) {
		Client->UID = -1;
		sendf(Client->Socket, "403 Account disabled\n");
		return ;
	}

	Client->bIsAuthed = 1;
	
	if(giDebugLevel)
		Debug(Client, "Auto authenticated as '%s' (%i)", username, Client->UID);
	
	sendf(Client->Socket, "200 Auth OK\n");
}

/**
 * \brief Set effective user
 */
void Server_Cmd_SETEUSER(tClient *Client, char *Args)
{
	char	*username;
	 int	eUserFlags, userFlags;
	
	if( Server_int_ParseArgs(0, Args, &username, NULL) )
	{
		sendf(Client->Socket, "407 SETEUSER takes 1 argument\n");
		return ;
	}
	
	if( !strlen(Args) ) {
		sendf(Client->Socket, "407 SETEUSER expects an argument\n");
		return ;
	}

	// Check user permissions
	userFlags = Bank_GetFlags(Client->UID);
	if( !(userFlags & (USER_FLAG_COKE|USER_FLAG_ADMIN)) ) {
		sendf(Client->Socket, "403 Not in coke\n");
		return ;
	}
	
	// Set id
	Client->EffectiveUID = Bank_GetAcctByName(username);
	if( Client->EffectiveUID == -1 ) {
		sendf(Client->Socket, "404 User not found\n");
		return ;
	}
	
	// You can't be an internal account
	if( !(userFlags & USER_FLAG_ADMIN) )
	{
		eUserFlags = Bank_GetFlags(Client->EffectiveUID);
		if( eUserFlags & USER_FLAG_INTERNAL ) {
			Client->EffectiveUID = -1;
			sendf(Client->Socket, "404 User not found\n");
			return ;
		}
		// Disabled only avaliable to admins
		if( eUserFlags & USER_FLAG_DISABLED ) {
			Client->EffectiveUID = -1;
			sendf(Client->Socket, "403 Account disabled\n");
			return ;
		}
	}
	
	sendf(Client->Socket, "200 User set\n");
}

/**
 * \brief Send an item status to the client
 * \param Client	Who to?
 * \param Item	Item to send
 */
void Server_int_SendItem(tClient *Client, tItem *Item)
{
	char	*status = "avail";
	
	if( Item->Handler->CanDispense )
	{
		switch(Item->Handler->CanDispense(Client->UID, Item->ID))
		{
		case  0:	status = "avail";	break;
		case  1:	status = "sold";	break;
		default:
		case -1:	status = "error";	break;
		}
	}
	
	sendf(Client->Socket,
		"202 Item %s:%i %s %i %s\n",
		Item->Handler->Name, Item->ID, status, Item->Price, Item->Name
		);
}

/**
 * \brief Enumerate the items that the server knows about
 */
void Server_Cmd_ENUMITEMS(tClient *Client, char *Args)
{
	 int	i, count;

	if( Args != NULL && strlen(Args) ) {
		sendf(Client->Socket, "407 ENUM_ITEMS takes no arguments\n");
		return ;
	}
	
	// Count shown items
	count = 0;
	for( i = 0; i < giNumItems; i ++ ) {
		if( gaItems[i].bHidden )	continue;
		count ++;
	}

	sendf(Client->Socket, "201 Items %i\n", count);

	for( i = 0; i < giNumItems; i ++ ) {
		if( gaItems[i].bHidden )	continue;
		Server_int_SendItem( Client, &gaItems[i] );
	}

	sendf(Client->Socket, "200 List end\n");
}

tItem *_GetItemFromString(char *String)
{
	tHandler	*handler;
	char	*type = String;
	char	*colon = strchr(String, ':');
	 int	num, i;
	
	if( !colon ) {
		return NULL;
	}

	num = atoi(colon+1);
	*colon = '\0';

	// Find handler
	handler = NULL;
	for( i = 0; i < giNumHandlers; i ++ )
	{
		if( strcmp(gaHandlers[i]->Name, type) == 0) {
			handler = gaHandlers[i];
			break;
		}
	}
	if( !handler ) {
		return NULL;
	}

	// Find item
	for( i = 0; i < giNumItems; i ++ )
	{
		if( gaItems[i].Handler != handler )	continue;
		if( gaItems[i].ID != num )	continue;
		return &gaItems[i];
	}
	return NULL;
}

/**
 * \brief Fetch information on a specific item
 */
void Server_Cmd_ITEMINFO(tClient *Client, char *Args)
{
	tItem	*item;
	char	*itemname;
	
	if( Server_int_ParseArgs(0, Args, &itemname, NULL) ) {
		sendf(Client->Socket, "407 ITEMINFO takes 1 argument\n");
		return ;
	}
	item = _GetItemFromString(Args);
	
	if( !item ) {
		sendf(Client->Socket, "406 Bad Item ID\n");
		return ;
	}
	
	Server_int_SendItem( Client, item );
}

void Server_Cmd_DISPENSE(tClient *Client, char *Args)
{
	tItem	*item;
	 int	ret;
	 int	uid;
	char	*itemname;
	
	if( Server_int_ParseArgs(0, Args, &itemname, NULL) ) {
		sendf(Client->Socket, "407 DISPENSE takes only 1 argument\n");
		return ;
	}
	 
	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
		return ;
	}

	item = _GetItemFromString(itemname);
	if( !item ) {
		sendf(Client->Socket, "406 Bad Item ID\n");
		return ;
	}
	
	if( Client->EffectiveUID != -1 ) {
		uid = Client->EffectiveUID;
	}
	else {
		uid = Client->UID;
	}

	switch( ret = DispenseItem( Client->UID, uid, item ) )
	{
	case 0:	sendf(Client->Socket, "200 Dispense OK\n");	return ;
	case 1:	sendf(Client->Socket, "501 Unable to dispense\n");	return ;
	case 2:	sendf(Client->Socket, "402 Poor You\n");	return ;
	default:
		sendf(Client->Socket, "500 Dispense Error\n");
		return ;
	}
}

void Server_Cmd_REFUND(tClient *Client, char *Args)
{
	tItem	*item;
	 int	uid, price_override = 0;
	char	*username, *itemname, *price_str;

	if( Server_int_ParseArgs(0, Args, &username, &itemname, &price_str, NULL) ) {
		if( !itemname || price_str ) {
			sendf(Client->Socket, "407 REFUND takes 2 or 3 arguments\n");
			return ;
		}
	}

	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
		return ;
	}

	// Check user permissions
	if( !(Bank_GetFlags(Client->UID) & (USER_FLAG_COKE|USER_FLAG_ADMIN))  ) {
		sendf(Client->Socket, "403 Not in coke\n");
		return ;
	}

	uid = Bank_GetAcctByName(username);
	if( uid == -1 ) {
		sendf(Client->Socket, "404 Unknown user\n");
		return ;
	}
	
	item = _GetItemFromString(itemname);
	if( !item ) {
		sendf(Client->Socket, "406 Bad Item ID\n");
		return ;
	}

	if( price_str )
		price_override = atoi(price_str);

	switch( DispenseRefund( Client->UID, uid, item, price_override ) )
	{
	case 0:	sendf(Client->Socket, "200 Item Refunded\n");	return ;
	default:
		sendf(Client->Socket, "500 Dispense Error\n");
		return;
	}
}

void Server_Cmd_GIVE(tClient *Client, char *Args)
{
	char	*recipient, *ammount, *reason;
	 int	uid, iAmmount;
	 int	thisUid;
	
	// Parse arguments
	if( Server_int_ParseArgs(1, Args, &recipient, &ammount, &reason, NULL) ) {
		sendf(Client->Socket, "407 GIVE takes only 3 arguments\n");
		return ;
	}
	// Check for authed
	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
		return ;
	}

	// Get recipient
	uid = Bank_GetAcctByName(recipient);
	if( uid == -1 ) {
		sendf(Client->Socket, "404 Invalid target user\n");
		return ;
	}
	
	// You can't alter an internal account
//	if( Bank_GetFlags(uid) & USER_FLAG_INTERNAL ) {
//		sendf(Client->Socket, "404 Invalid target user\n");
//		return ;
//	}

	// Parse ammount
	iAmmount = atoi(ammount);
	if( iAmmount <= 0 ) {
		sendf(Client->Socket, "407 Invalid Argument, ammount must be > zero\n");
		return ;
	}
	
	if( Client->EffectiveUID != -1 ) {
		thisUid = Client->EffectiveUID;
	}
	else {
		thisUid = Client->UID;
	}

	// Do give
	switch( DispenseGive(Client->UID, thisUid, uid, iAmmount, reason) )
	{
	case 0:
		sendf(Client->Socket, "200 Give OK\n");
		return ;
	case 2:
		sendf(Client->Socket, "402 Poor You\n");
		return ;
	default:
		sendf(Client->Socket, "500 Unknown error\n");
		return ;
	}
}

void Server_Cmd_DONATE(tClient *Client, char *Args)
{
	char	*ammount, *reason;
	 int	iAmmount;
	 int	thisUid;
	
	// Parse arguments
	if( Server_int_ParseArgs(1, Args, &ammount, &reason, NULL) ) {
		sendf(Client->Socket, "407 DONATE takes 2 arguments\n");
		return ;
	}
	
	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
		return ;
	}

	// Parse ammount
	iAmmount = atoi(ammount);
	if( iAmmount <= 0 ) {
		sendf(Client->Socket, "407 Invalid Argument, ammount must be > zero\n");
		return ;
	}
	
	// Handle effective users
	if( Client->EffectiveUID != -1 ) {
		thisUid = Client->EffectiveUID;
	}
	else {
		thisUid = Client->UID;
	}

	// Do give
	switch( DispenseDonate(Client->UID, thisUid, iAmmount, reason) )
	{
	case 0:
		sendf(Client->Socket, "200 Give OK\n");
		return ;
	case 2:
		sendf(Client->Socket, "402 Poor You\n");
		return ;
	default:
		sendf(Client->Socket, "500 Unknown error\n");
		return ;
	}
}

void Server_Cmd_ADD(tClient *Client, char *Args)
{
	char	*user, *ammount, *reason;
	 int	uid, iAmmount;
	
	// Parse arguments
	if( Server_int_ParseArgs(1, Args, &user, &ammount, &reason, NULL) ) {
		sendf(Client->Socket, "407 ADD takes 3 arguments\n");
		return ;
	}
	
	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
		return ;
	}

	// Check user permissions
	if( !(Bank_GetFlags(Client->UID) & (USER_FLAG_COKE|USER_FLAG_ADMIN))  ) {
		sendf(Client->Socket, "403 Not in coke\n");
		return ;
	}

	// Get recipient
	uid = Bank_GetAcctByName(user);
	if( uid == -1 ) {
		sendf(Client->Socket, "404 Invalid user\n");
		return ;
	}
	
	// You can't alter an internal account
	if( !(Bank_GetFlags(Client->UID) & USER_FLAG_ADMIN) )
	{
		if( Bank_GetFlags(uid) & USER_FLAG_INTERNAL ) {
			sendf(Client->Socket, "404 Invalid user\n");
			return ;
		}
		// TODO: Maybe disallow changes to disabled?
	}

	// Parse ammount
	iAmmount = atoi(ammount);
	if( iAmmount == 0 && ammount[0] != '0' ) {
		sendf(Client->Socket, "407 Invalid Argument\n");
		return ;
	}

	// Do give
	switch( DispenseAdd(Client->UID, uid, iAmmount, reason) )
	{
	case 0:
		sendf(Client->Socket, "200 Add OK\n");
		return ;
	case 2:
		sendf(Client->Socket, "402 Poor Guy\n");
		return ;
	default:
		sendf(Client->Socket, "500 Unknown error\n");
		return ;
	}
}

void Server_Cmd_SET(tClient *Client, char *Args)
{
	char	*user, *ammount, *reason;
	 int	uid, iAmmount;
	
	// Parse arguments
	if( Server_int_ParseArgs(1, Args, &user, &ammount, &reason, NULL) ) {
		sendf(Client->Socket, "407 SET takes 3 arguments\n");
		return ;
	}
	
	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
		return ;
	}

	// Check user permissions
	if( !(Bank_GetFlags(Client->UID) & USER_FLAG_ADMIN)  ) {
		sendf(Client->Socket, "403 Not an admin\n");
		return ;
	}

	// Get recipient
	uid = Bank_GetAcctByName(user);
	if( uid == -1 ) {
		sendf(Client->Socket, "404 Invalid user\n");
		return ;
	}

	// Parse ammount
	iAmmount = atoi(ammount);
	if( iAmmount == 0 && ammount[0] != '0' ) {
		sendf(Client->Socket, "407 Invalid Argument\n");
		return ;
	}

	// Do give
	switch( DispenseSet(Client->UID, uid, iAmmount, reason) )
	{
	case 0:
		sendf(Client->Socket, "200 Add OK\n");
		return ;
	case 2:
		sendf(Client->Socket, "402 Poor Guy\n");
		return ;
	default:
		sendf(Client->Socket, "500 Unknown error\n");
		return ;
	}
}

void Server_Cmd_ENUMUSERS(tClient *Client, char *Args)
{
	 int	i, numRet = 0;
	tAcctIterator	*it;
	 int	maxBal = INT_MAX, minBal = INT_MIN;
	 int	flagMask = 0, flagVal = 0;
	 int	sort = BANK_ITFLAG_SORT_NAME;
	time_t	lastSeenAfter=0, lastSeenBefore=0;
	
	 int	flags;	// Iterator flags
	 int	balValue;	// Balance value for iterator
	time_t	timeValue;	// Time value for iterator
	
	// Parse arguments
	if( Args && strlen(Args) )
	{
		char	*space = Args, *type, *val;
		do
		{
			type = space;
			while(*type == ' ')	type ++;
			// Get next space
			space = strchr(space, ' ');
			if(space)	*space = '\0';
			
			// Get type
			val = strchr(type, ':');
			if( val ) {
				*val = '\0';
				val ++;
				
				// Types
				// - Minium Balance
				if( strcmp(type, "min_balance") == 0 ) {
					minBal = atoi(val);
				}
				// - Maximum Balance
				else if( strcmp(type, "max_balance") == 0 ) {
					maxBal = atoi(val);
				}
				// - Flags
				else if( strcmp(type, "flags") == 0 ) {
					if( Server_int_ParseFlags(Client, val, &flagMask, &flagVal) )
						return ;
				}
				// - Last seen before timestamp
				else if( strcmp(type, "last_seen_before") == 0 ) {
					lastSeenAfter = atoll(val);
				}
				// - Last seen after timestamp
				else if( strcmp(type, "last_seen_after") == 0 ) {
					lastSeenAfter = atoll(val);
				}
				// - Sorting 
				else if( strcmp(type, "sort") == 0 ) {
					char	*dash = strchr(val, '-');
					if( dash ) {
						*dash = '\0';
						dash ++;
					}
					if( strcmp(val, "name") == 0 ) {
						sort = BANK_ITFLAG_SORT_NAME;
					}
					else if( strcmp(val, "balance") == 0 ) {
						sort = BANK_ITFLAG_SORT_BAL;
					}
					else if( strcmp(val, "lastseen") == 0 ) {
						sort = BANK_ITFLAG_SORT_LASTSEEN;
					}
					else {
						sendf(Client->Socket, "407 Unknown sort field ('%s')\n", val);
						return ;
					}
					// Handle sort direction
					if( dash ) {
						if( strcmp(dash, "desc") == 0 ) {
							sort |= BANK_ITFLAG_REVSORT;
						}
						else {
							sendf(Client->Socket, "407 Unknown sort direction '%s'\n", dash);
							return ;
						}
						dash[-1] = '-';
					}
				}
				else {
					sendf(Client->Socket, "407 Unknown argument to ENUM_USERS '%s:%s'\n", type, val);
					return ;
				}
				
				val[-1] = ':';
			}
			else {
				sendf(Client->Socket, "407 Unknown argument to ENUM_USERS '%s'\n", type);
				return ;
			}
			
			// Eat whitespace
			if( space ) {
				*space = ' ';	// Repair (to be nice)
				space ++;
				while(*space == ' ')	space ++;
			}
		}	while(space);
	}
	
	// Create iterator
	if( maxBal != INT_MAX ) {
		flags = sort|BANK_ITFLAG_MAXBALANCE;
		balValue = maxBal;
	}
	else if( minBal != INT_MIN ) {
		flags = sort|BANK_ITFLAG_MINBALANCE;
		balValue = minBal;
	}
	else {
		flags = sort;
		balValue = 0;
	}
	if( lastSeenBefore ) {
		timeValue = lastSeenBefore;
		flags |= BANK_ITFLAG_SEENBEFORE;
	}
	else if( lastSeenAfter ) {
		timeValue = lastSeenAfter;
		flags |= BANK_ITFLAG_SEENAFTER;
	}
	else {
		timeValue = 0;
	}
	it = Bank_Iterator(flagMask, flagVal, flags, balValue, timeValue);
	
	// Get return number
	while( (i = Bank_IteratorNext(it)) != -1 )
	{
		int bal = Bank_GetBalance(i);
		
		if( bal == INT_MIN )	continue;
		
		if( bal < minBal )	continue;
		if( bal > maxBal )	continue;
		
		numRet ++;
	}
	
	Bank_DelIterator(it);
	
	// Send count
	sendf(Client->Socket, "201 Users %i\n", numRet);
	
	
	// Create iterator
	it = Bank_Iterator(flagMask, flagVal, flags, balValue, timeValue);
	
	while( (i = Bank_IteratorNext(it)) != -1 )
	{
		int bal = Bank_GetBalance(i);
		
		if( bal == INT_MIN )	continue;
		
		if( bal < minBal )	continue;
		if( bal > maxBal )	continue;
		
		_SendUserInfo(Client, i);
	}
	
	Bank_DelIterator(it);
	
	sendf(Client->Socket, "200 List End\n");
}

void Server_Cmd_USERINFO(tClient *Client, char *Args)
{
	 int	uid;
	char	*user;
	
	// Parse arguments
	if( Server_int_ParseArgs(0, Args, &user, NULL) ) {
		sendf(Client->Socket, "407 USER_INFO takes 1 argument\n");
		return ;
	}
	
	if( giDebugLevel )	Debug(Client, "User Info '%s'", user);
	
	// Get recipient
	uid = Bank_GetAcctByName(user);
	
	if( giDebugLevel >= 2 )	Debug(Client, "uid = %i", uid);
	if( uid == -1 ) {
		sendf(Client->Socket, "404 Invalid user\n");
		return ;
	}
	
	_SendUserInfo(Client, uid);
}

void _SendUserInfo(tClient *Client, int UserID)
{
	char	*type, *disabled="", *door="";
	 int	flags = Bank_GetFlags(UserID);
	
	if( flags & USER_FLAG_INTERNAL ) {
		type = "internal";
	}
	else if( flags & USER_FLAG_COKE ) {
		if( flags & USER_FLAG_ADMIN )
			type = "coke,admin";
		else
			type = "coke";
	}
	else if( flags & USER_FLAG_ADMIN ) {
		type = "admin";
	}
	else {
		type = "user";
	}
	
	if( flags & USER_FLAG_DISABLED )
		disabled = ",disabled";
	if( flags & USER_FLAG_DOORGROUP )
		door = ",door";
	
	// TODO: User flags/type
	sendf(
		Client->Socket, "202 User %s %i %s%s%s\n",
		Bank_GetAcctName(UserID), Bank_GetBalance(UserID),
		type, disabled, door
		);
}

void Server_Cmd_USERADD(tClient *Client, char *Args)
{
	char	*username;
	
	// Parse arguments
	if( Server_int_ParseArgs(0, Args, &username, NULL) ) {
		sendf(Client->Socket, "407 USER_ADD takes 1 argument\n");
		return ;
	}
	
	// Check permissions
	if( !(Bank_GetFlags(Client->UID) & USER_FLAG_ADMIN) ) {
		sendf(Client->Socket, "403 Not a coke admin\n");
		return ;
	}
	
	// Try to create user
	if( Bank_CreateAcct(username) == -1 ) {
		sendf(Client->Socket, "404 User exists\n");
		return ;
	}
	
	{
		char	*thisName = Bank_GetAcctName(Client->UID);
		Log_Info("Account '%s' created by '%s'", username, thisName);
		free(thisName);
	}
	
	sendf(Client->Socket, "200 User Added\n");
}

void Server_Cmd_USERFLAGS(tClient *Client, char *Args)
{
	char	*username, *flags;
	 int	mask=0, value=0;
	 int	uid;
	
	// Parse arguments
	if( Server_int_ParseArgs(0, Args, &username, &flags, NULL) ) {
		sendf(Client->Socket, "407 USER_FLAGS takes 2 arguments\n");
		return ;
	}
	
	// Check permissions
	if( !(Bank_GetFlags(Client->UID) & USER_FLAG_ADMIN) ) {
		sendf(Client->Socket, "403 Not a coke admin\n");
		return ;
	}
	
	// Get UID
	uid = Bank_GetAcctByName(username);
	if( uid == -1 ) {
		sendf(Client->Socket, "404 User '%s' not found\n", username);
		return ;
	}
	
	// Parse flags
	if( Server_int_ParseFlags(Client, flags, &mask, &value) )
		return ;
	
	if( giDebugLevel )
		Debug(Client, "Set %i(%s) flags to %x (masked %x)\n",
			uid, username, mask, value);
	
	// Apply flags
	Bank_SetFlags(uid, mask, value);
	
	// Return OK
	sendf(Client->Socket, "200 User Updated\n");
}

void Server_Cmd_UPDATEITEM(tClient *Client, char *Args)
{
	char	*itemname, *price_str, *description;
	 int	price;
	tItem	*item;
	
	if( Server_int_ParseArgs(1, Args, &itemname, &price_str, &description, NULL) ) {
		sendf(Client->Socket, "407 UPDATE_ITEM takes 3 arguments\n");
		return ;
	}
	
	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
		return ;
	}

	// Check user permissions
	if( !(Bank_GetFlags(Client->UID) & (USER_FLAG_COKE|USER_FLAG_ADMIN))  ) {
		sendf(Client->Socket, "403 Not in coke\n");
		return ;
	}
	
	item = _GetItemFromString(itemname);
	if( !item ) {
		// TODO: Create item?
		sendf(Client->Socket, "406 Bad Item ID\n");
		return ;
	}
	
	price = atoi(price_str);
	if( price <= 0 && price_str[0] != '0' ) {
		sendf(CLient->Socket, "407 Invalid price set\n");
	}
	
	// Update the item
	free(item->Name);
	item->Name = strdup(description);
	item->Price = price;
	
	// Update item file
	Items_UpdateFile();
	
	// Return OK
	sendf(Client->Socket, "200 Item updated\n");
}

// --- INTERNAL HELPERS ---
void Debug(tClient *Client, const char *Format, ...)
{
	va_list	args;
	//printf("%010i [%i] ", (int)time(NULL), Client->ID);
	printf("[%i] ", Client->ID);
	va_start(args, Format);
	vprintf(Format, args);
	va_end(args);
	printf("\n");
}

int sendf(int Socket, const char *Format, ...)
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
		
		#if DEBUG_TRACE_CLIENT
		printf("sendf: %s", buf);
		#endif
		
		return send(Socket, buf, len, 0);
	}
}

// Takes a series of char *'s in
/**
 * \brief Parse space-separated entries into 
 */
int Server_int_ParseArgs(int bUseLongLast, char *ArgStr, ...)
{
	va_list args;
	char	savedChar;
	char	**dest;
	va_start(args, ArgStr);

	// Check for null
	if( !ArgStr )
	{
		while( (dest = va_arg(args, char **)) )
			*dest = NULL;
		va_end(args);
		return 1;
	}

	savedChar = *ArgStr;
	
	while( (dest = va_arg(args, char **)) )
	{
		// Trim leading spaces
		while( *ArgStr == ' ' || *ArgStr == '\t' )
			ArgStr ++;
		
		// ... oops, not enough arguments
		if( *ArgStr == '\0' )
		{
			// NULL unset arguments
			do {
				*dest = NULL;
			}	while( (dest = va_arg(args, char **)) );
		va_end(args);
			return -1;
		}
		
		if( *ArgStr == '"' )
		{
			ArgStr ++;
			*dest = ArgStr;
			// Read until quote
			while( *ArgStr && *ArgStr != '"' )
				ArgStr ++;
		}
		else
		{
			// Set destination
			*dest = ArgStr;
			// Read until a space
			while( *ArgStr && *ArgStr != ' ' && *ArgStr != '\t' )
				ArgStr ++;
		}
		savedChar = *ArgStr;	// savedChar is used to un-mangle the last string
		*ArgStr = '\0';
		ArgStr ++;
	}
	va_end(args);
	
	// Oops, extra arguments, and greedy not set
	if( (savedChar == ' ' || savedChar == '\t') && !bUseLongLast ) {
		return -1;
	}
	
	// Un-mangle last
	if(bUseLongLast) {
		ArgStr --;
		*ArgStr = savedChar;
	}
	
	return 0;	// Success!
}

int Server_int_ParseFlags(tClient *Client, const char *Str, int *Mask, int *Value)
{
	struct {
		const char	*Name;
		 int	Mask;
		 int	Value;
	}	cFLAGS[] = {
		 {"disabled", USER_FLAG_DISABLED, USER_FLAG_DISABLED}
		,{"door", USER_FLAG_DOORGROUP, USER_FLAG_DOORGROUP}
		,{"coke", USER_FLAG_COKE, USER_FLAG_COKE}
		,{"admin", USER_FLAG_ADMIN, USER_FLAG_ADMIN}
		,{"internal", USER_FLAG_INTERNAL, USER_FLAG_INTERNAL}
	};
	const int	ciNumFlags = sizeof(cFLAGS)/sizeof(cFLAGS[0]);
	
	char	*space;
	
	*Mask = 0;
	*Value = 0;
	
	do {
		 int	bRemove = 0;
		 int	i;
		 int	len;
		
		while( *Str == ' ' )	Str ++;	// Eat whitespace
		space = strchr(Str, ',');	// Find the end of the flag
		if(space)
			len = space - Str;
		else
			len = strlen(Str);
		
		// Check for inversion/removal
		if( *Str == '!' || *Str == '-' ) {
			bRemove = 1;
			Str ++;
		}
		else if( *Str == '+' ) {
			Str ++;
		}
		
		// Check flag values
		for( i = 0; i < ciNumFlags; i ++ )
		{
			if( strncmp(Str, cFLAGS[i].Name, len) == 0 ) {
				*Mask |= cFLAGS[i].Mask;
				*Value &= ~cFLAGS[i].Mask;
				if( !bRemove )
					*Value |= cFLAGS[i].Value;
				break;
			}
		}
		
		// Error check
		if( i == ciNumFlags ) {
			char	val[len+1];
			strncpy(val, Str, len+1);
			sendf(Client->Socket, "407 Unknown flag value '%s'\n", val);
			return -1;
		}
		
		Str = space + 1;
	} while(space);
	
	return 0;
}
