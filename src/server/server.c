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
#include "../common/config.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>	// O_*
#include <string.h>
#include <limits.h>
#include <stdarg.h>
#include <signal.h>	// Signal handling
#include <ident.h>	// AUTHIDENT
#include <time.h>	// time(2)
#include <ctype.h>

#define	DEBUG_TRACE_CLIENT	0
#define HACK_NO_REFUNDS	1

#define PIDFILE	"/var/run/dispsrv.pid"

// Statistics
#define MAX_CONNECTION_QUEUE	5
#define INPUT_BUFFER_SIZE	256
#define CLIENT_TIMEOUT	10	// Seconds

#define HASH_TYPE	SHA1
#define HASH_LENGTH	20

#define MSG_STR_TOO_LONG	"499 Command too long (limit "EXPSTR(INPUT_BUFFER_SIZE)")\n"

#define IDENT_TRUSTED_NETWORK 0x825F0D00
#define IDENT_TRUSTED_NETMASK 0xFFFFFFC0

// === TYPES ===
typedef struct sClient
{
	 int	Socket;	// Client socket ID
	 int	ID;	// Client ID
	 
	 int	bTrustedHost;
	 int	bCanAutoAuth;	// Is the connection from a trusted host/port
	
	char	*Username;
	char	Salt[9];
	
	 int	UID;
	 int	EffectiveUID;
	 int	bIsAuthed;
}	tClient;

// === PROTOTYPES ===
void	Server_Start(void);
void	Server_Cleanup(void);
void	Server_HandleClient(int Socket, int bTrustedHost, int bRootPort);
void	Server_ParseClientCommand(tClient *Client, char *CommandString);
// --- Commands ---
void	Server_Cmd_USER(tClient *Client, char *Args);
void	Server_Cmd_PASS(tClient *Client, char *Args);
void	Server_Cmd_AUTOAUTH(tClient *Client, char *Args);
void	Server_Cmd_AUTHIDENT(tClient *Client, char *Args);
void	Server_Cmd_AUTHCARD(tClient* Client, char *Args);
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
void	Server_Cmd_PINCHECK(tClient *Client, char *Args);
void	Server_Cmd_PINSET(tClient *Client, char *Args);
void	Server_Cmd_CARDADD(tClient *Client, char *Args);
// --- Helpers ---
void	Debug(tClient *Client, const char *Format, ...);
 int	sendf(int Socket, const char *Format, ...);
 int	Server_int_ParseArgs(int bUseLongArg, char *ArgStr, ...);
 int	Server_int_ParseFlags(tClient *Client, const char *Str, int *Mask, int *Value);

#define CLIENT_DEBUG_LOW(Client, ...)	do { if(giDebugLevel>1) Debug(Client, __VA_ARGS__); } while(0)
#define CLIENT_DEBUG(Client, ...)	do { if(giDebugLevel) Debug(Client, __VA_ARGS__); } while(0)

// === CONSTANTS ===
// - Commands
const struct sClientCommand {
	const char	*Name;
	void	(*Function)(tClient *Client, char *Arguments);
}	gaServer_Commands[] = {
	{"USER", Server_Cmd_USER},
	{"PASS", Server_Cmd_PASS},
	{"AUTOAUTH", Server_Cmd_AUTOAUTH},
	{"AUTHIDENT", Server_Cmd_AUTHIDENT},
	{"AUTHCARD", Server_Cmd_AUTHCARD},
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
	{"UPDATE_ITEM", Server_Cmd_UPDATEITEM},
	{"PIN_CHECK", Server_Cmd_PINCHECK},
	{"PIN_SET", Server_Cmd_PINSET},
	{"CARD_ADD", Server_Cmd_CARDADD},
};
#define NUM_COMMANDS	((int)(sizeof(gaServer_Commands)/sizeof(gaServer_Commands[0])))

// === GLOBALS ===
// - Configuration
 int	giServer_Port = 11020;
 int	gbServer_RunInBackground = 0;
char	*gsServer_LogFile = "/var/log/dispsrv.log";
char	*gsServer_ErrorLog = "/var/log/dispsrv.err";
 int	giServer_NumTrustedHosts;
struct in_addr	*gaServer_TrustedHosts;
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

	// Parse trusted hosts list
	giServer_NumTrustedHosts = Config_GetValueCount("trusted_host");
	gaServer_TrustedHosts = malloc(giServer_NumTrustedHosts * sizeof(*gaServer_TrustedHosts));
	for( int i = 0; i < giServer_NumTrustedHosts; i ++ )
	{
		const char	*addr = Config_GetValue_Idx("trusted_host", i);
		
		if( inet_aton(addr, &gaServer_TrustedHosts[i]) == 0 ) {
			fprintf(stderr, "Invalid IP address '%s'\n", addr);
			continue ;
		}
	}

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
		close(giServer_Socket);
		return ;
	}

	// Fork into background
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
			Debug_Notice("Forked child server as PID %i\n", pid);
			exit(0);
		}
		// In child
		// - Sort out stdin/stdout
		#if 0
		dup2( open("/dev/null", O_RDONLY, 0644), STDIN_FILENO );
		dup2( open(gsServer_LogFile, O_CREAT|O_APPEND, 0644), STDOUT_FILENO );
		dup2( open(gsServer_ErrorLog, O_CREAT|O_APPEND, 0644), STDERR_FILENO );
		#else
		freopen("/dev/null", "r", stdin);
		freopen(gsServer_LogFile, "a", stdout);
		freopen(gsServer_ErrorLog, "a", stderr);
		fprintf(stdout, "OpenDispense 2 Server Started at %lld\n", (long long)time(NULL));
		fprintf(stderr, "OpenDispense 2 Server Started at %lld\n", (long long)time(NULL));
		#endif
	}
	atexit(Server_Cleanup);

	// Start the helper thread
	StartPeriodicThread();
	
	// Listen
	if( listen(giServer_Socket, MAX_CONNECTION_QUEUE) < 0 ) {
		fprintf(stderr, "ERROR: Unable to listen to socket\n");
		perror("Listen");
		return ;
	}
	
	Debug_Notice("Listening on 0.0.0.0:%i", giServer_Port);
	
	// write pidfile
	{
		FILE *fp = fopen(PIDFILE, "w");
		if( fp ) {
			fprintf(fp, "%i", getpid());
			fclose(fp);
		}
	}

	for(;;)
	{
		uint	len = sizeof(client_addr);
		 int	bTrusted = 0;
		 int	bRootPort = 0;
		
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
			Debug_Debug("Client connection from %s:%i",
				ipstr, ntohs(client_addr.sin_port));
		}
		
		// Doesn't matter what, localhost is trusted
		if( ntohl( client_addr.sin_addr.s_addr ) == 0x7F000001 )
			bTrusted = 1;
	
		// Check if the host is on the trusted list	
		for( int i = 0; i < giServer_NumTrustedHosts; i ++ )
		{
			if( memcmp(&client_addr.sin_addr, &gaServer_TrustedHosts[i], sizeof(struct in_addr)) == 0 )
			{
				bTrusted = 1;
				break;
			}
		}

		// Root port (can AUTOAUTH if also a trusted machine
		if( ntohs(client_addr.sin_port) < 1024 )
			bRootPort = 1;
		
		#if 0
		{
			// TODO: Make this runtime configurable
			switch( ntohl( client_addr.sin_addr.s_addr ) )
			{
			case 0x7F000001:	// 127.0.0.1	localhost
		//	case 0x825F0D00:	// 130.95.13.0
			case 0x825F0D04:	// 130.95.13.4  merlo
		//	case 0x825F0D05:	// 130.95.13.5  heathred (MR)
			case 0x825F0D07:	// 130.95.13.7	motsugo
			case 0x825F0D11:	// 130.95.13.17	mermaid
			case 0x825F0D12:	// 130.95.13.18	mussel
			case 0x825F0D17:	// 130.95.13.23	martello
			case 0x825F0D2A:	// 130.95.13.42 meersau
		//	case 0x825F0D42:	// 130.95.13.66	heathred (Clubroom)
				bTrusted = 1;
				break;
			default:
				break;
			}
		}
		#endif
		
		// TODO: Multithread this?
		Server_HandleClient(client_socket, bTrusted, bRootPort);
		
		close(client_socket);
	}
}

void Server_Cleanup(void)
{
	Debug_Debug("Close(%i)", giServer_Socket);
	close(giServer_Socket);
	unlink(PIDFILE);
}

/**
 * \brief Reads from a client socket and parses the command strings
 * \param Socket	Client socket number/handle
 * \param bTrusted	Is the client trusted?
 */
void Server_HandleClient(int Socket, int bTrusted, int bRootPort)
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
	clientInfo.bTrustedHost = bTrusted;
	clientInfo.bCanAutoAuth = bTrusted && bRootPort;
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
		// Is this an error? (just ignore for now)
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

/// UID: User ID (must be valid)
/// username: Optional username
bool authenticate(tClient* Client, int UID, const char* username)
{
	Client->UID = UID;

	int flags = Bank_GetFlags(Client->UID);
	if( flags & USER_FLAG_DISABLED ) {
		Client->UID = -1;
		sendf(Client->Socket, "403 Authentication failure: account disabled\n");
		return false;
	}
	// You can't be an internal account
	if( flags & USER_FLAG_INTERNAL ) {
		if(giDebugLevel)
			Debug(Client, "IDENT auth as '%s', not allowed", username);
		Client->UID = -1;
		sendf(Client->Socket, "403 Authentication failure: that account is internal\n");
		return false;
	}

	// Save username
	if(Client->Username != username)
	{
		if(Client->Username)
		{
			free(Client->Username);
		}

		// Fetch username (if not provided)
		if( username )
		{
			Client->Username = strdup(username);
		}
		else
		{
			Client->Username = Bank_GetAcctName(UID);
		}
	}

	Client->bIsAuthed = 1;
	
	if(giDebugLevel)
		Debug(Client, "Auto authenticated as '%s' (%i)", Client->Username, Client->UID);
	return true;
}
bool require_auth(tClient* Client)
{
	// Check authentication
	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
		return false;
	}
	return true;
}

/**
 * \brief Authenticate as a user
 * 
 * Usage: PASS <hash>
 */
void Server_Cmd_PASS(tClient *Client, char *Args)
{
	char	*passhash;

	if( Server_int_ParseArgs(0, Args, &passhash, NULL) )
	{
		sendf(Client->Socket, "407 PASS takes 1 argument\n");
		return ;
	}
	
	// Pass on to cokebank
	int uid = Bank_GetUserAuth(Client->Salt, Client->Username, passhash);
	if( uid < 0 ) {
		if(giDebugLevel)
			Debug(Client, "Unknown user '%s'", Client->Username);
		sendf(Client->Socket, "403 Authentication failure: unknown account\n");
		return ;
	}
	if( ! authenticate(Client, uid, Client->Username) )
	{
		return;
	}
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
	
	if( Server_int_ParseArgs(0, Args, &username, NULL) )
	{
		sendf(Client->Socket, "407 AUTOAUTH takes 1 argument\n");
		return ;
	}
	
	// Check if trusted
	if( !Client->bCanAutoAuth ) {
		if(giDebugLevel)
			Debug(Client, "Untrusted client attempting to AUTOAUTH");
		sendf(Client->Socket, "401 Untrusted\n");
		return ;
	}
	
	// Get UID
	int uid = Bank_GetAcctByName( username, /*bCreate=*/0 );
	if( uid < 0 ) {
		if(giDebugLevel)
			Debug(Client, "Unknown user '%s'", username);
		sendf(Client->Socket, "403 Authentication failure: unknown account\n");
		return ;
	}
	if( ! authenticate(Client, uid, username) )
	{
		return;
	}
	
	sendf(Client->Socket, "200 Auth OK\n");
}

/**
 * \brief Authenticate as a user using the IDENT protocol
 *
 * Usage: AUTHIDENT
 */
void Server_Cmd_AUTHIDENT(tClient *Client, char *Args)
{
	char	*username;
	const int IDENT_TIMEOUT = 5;

	if( Args != NULL && strlen(Args) ) {
		sendf(Client->Socket, "407 AUTHIDENT takes no arguments\n");
		return ;
	}

	// Check if trusted
	if( !Client->bTrustedHost ) {
		if(giDebugLevel)
			Debug(Client, "Untrusted client attempting to AUTHIDENT");
		sendf(Client->Socket, "401 Untrusted\n");
		return ;
	}

	// Get username via IDENT
	username = ident_id(Client->Socket, IDENT_TIMEOUT);
	if( !username ) {
		perror("AUTHIDENT - IDENT timed out");
		sendf(Client->Socket, "403 Authentication failure: IDENT auth timed out\n");
		return ;
	}

	int uid = Bank_GetAcctByName(username, /*bCreate=*/0);
	if( uid < 0 ) {
		if(giDebugLevel)
			Debug(Client, "Unknown user '%s'", username);
		sendf(Client->Socket, "403 Authentication failure: unknown account\n");
		free(username);
		return ;
	}
	if( ! authenticate(Client, uid, username) )
	{
		free(username);
		return ;
	}
	free(username);

	sendf(Client->Socket, "200 Auth OK\n");
}

void Server_Cmd_AUTHCARD(tClient* Client, char *Args)
{
	char* card_id;
	if( Server_int_ParseArgs(0, Args, &card_id, NULL) )
	{
		sendf(Client->Socket, "407 AUTHCARD takes 1 argument\n");
		return ;
	}

	// Check if trusted (has to be root)
	if( Client->UID != 1 )
	{
		if(giDebugLevel)
			Debug(Client, "Attempting to use AUTHCARD as non-root");
		sendf(Client->Socket, "401 Untrusted\n");
		return ;
	}

	CLIENT_DEBUG(Client, "MIFARE auth with '%s'", card_id);
	int uid = Bank_GetAcctByCard(card_id);
	if( uid < 0 )
	{
		if(giDebugLevel)
			Debug(Client, "Unknown MIFARE '%s'", card_id);
		sendf(Client->Socket, "403 Authentication failure: unknown MIFARE ID\n");
		return ;
	}
	if( ! authenticate(Client, uid, NULL) )
	{
		return ;
	}

	sendf(Client->Socket, "200 Auth Ok, username=%s\n", Client->Username);
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
	
	// Check authentication
	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
		return ;
	}

	// Check user permissions
	userFlags = Bank_GetFlags(Client->UID);
	if( !(userFlags & (USER_FLAG_COKE|USER_FLAG_ADMIN)) ) {
		sendf(Client->Socket, "403 Not in coke\n");
		return ;
	}
	
	// Set id
	Client->EffectiveUID = Bank_GetAcctByName(username, 0);
	if( Client->EffectiveUID == -1 ) {
		sendf(Client->Socket, "404 User not found\n");
		return ;
	}
	// You can't be an internal account (unless you're an admin)
	if( !(userFlags & USER_FLAG_ADMIN) )
	{
		eUserFlags = Bank_GetFlags(Client->EffectiveUID);
		if( eUserFlags & USER_FLAG_INTERNAL ) {
			Client->EffectiveUID = -1;
			sendf(Client->Socket, "404 User not found\n");
			return ;
		}
	}

	// Disabled accounts
	// - If disabled and the actual user is not an admin (and not root)
	//   return 403
	if( (eUserFlags & USER_FLAG_DISABLED) && (Client->UID == 0 || !(userFlags & USER_FLAG_ADMIN)) ) {
		Client->EffectiveUID = -1;
		sendf(Client->Socket, "403 Account disabled\n");
		return ;
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
	
	if( !gbNoCostMode && Item->Price == 0 )
		status = "error";
	// KNOWN HACK: Naming a slot 'dead' disables it
	if( strcmp(Item->Name, "dead") == 0 )
		status = "sold";	// Another status?
	
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
 *
 * Usage: ITEMINFO <item ID>
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

/**
 * \brief Dispense an item
 *
 * Usage: DISPENSE <Item ID>
 */
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

//	if( Bank_GetFlags(Client->UID) & USER_FLAG_DISABLED  ) {
//	}

	switch( ret = DispenseItem( Client->UID, uid, item ) )
	{
	case 0:	sendf(Client->Socket, "200 Dispense OK\n");	return ;
	case 1:	sendf(Client->Socket, "501 Unable to dispense\n");	return ;
	case 2:	sendf(Client->Socket, "402 Poor You\n");	return ;
	default:
		sendf(Client->Socket, "500 Dispense Error (%i)\n", ret);
		return ;
	}
}

/**
 * \brief Refund an item to a user
 *
 * Usage: REFUND <user> <item id> [<price>]
 */
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

	uid = Bank_GetAcctByName(username, 0);
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

/**
 * \brief Transfer money to another account
 *
 * Usage: GIVE <dest> <ammount> <reason...>
 */
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
	uid = Bank_GetAcctByName(recipient, 0);
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

	#if !ROOT_CAN_ADD
	if( strcmp( Client->Username, "root" ) == 0 ) {
		// Allow adding for new users
		if( strcmp(reason, "treasurer: new user") != 0 ) {
			sendf(Client->Socket, "403 Root may not add\n");
			return ;
		}
	}
	#endif

	#if HACK_NO_REFUNDS
	if( strstr(reason, "refund") != NULL || strstr(reason, "misdispense") != NULL )
	{
		sendf(Client->Socket, "499 Don't use `dispense acct` for refunds, use `dispense refund` (and `dispense -G` to get item IDs)\n");
		return ;
	}
	#endif

	// Get recipient
	uid = Bank_GetAcctByName(user, 0);
	if( uid == -1 ) {
		sendf(Client->Socket, "404 Invalid user\n");
		return ;
	}
	
	// You can't alter an internal account
	if( !(Bank_GetFlags(Client->UID) & USER_FLAG_ADMIN) )
	{
		if( Bank_GetFlags(uid) & USER_FLAG_INTERNAL ) {
			sendf(Client->Socket, "403 Admin only\n");
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
	uid = Bank_GetAcctByName(user, 0);
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

	int origBalance, rv;
	// Do give
	switch( rv = DispenseSet(Client->UID, uid, iAmmount, reason, &origBalance) )
	{
	case 0:
		sendf(Client->Socket, "200 Add OK (%i)\n", origBalance);
		return ;
	default:
		sendf(Client->Socket, "500 Unknown error (%i)\n", rv);
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
	uid = Bank_GetAcctByName(user, 0);
	
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
	
	// Check authentication
	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
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
	char	*username, *flags, *reason=NULL;
	 int	mask=0, value=0;
	 int	uid;
	
	// Parse arguments
	if( Server_int_ParseArgs(1, Args, &username, &flags, &reason, NULL) ) {
		if( !flags ) {
			sendf(Client->Socket, "407 USER_FLAGS takes at least 2 arguments\n");
			return ;
		}
		reason = "";
	}
	
	// Check authentication
	if(!require_auth(Client))	return;
	
	// Check permissions
	if( !(Bank_GetFlags(Client->UID) & USER_FLAG_ADMIN) ) {
		sendf(Client->Socket, "403 Not a coke admin\n");
		return ;
	}
	
	// Get UID
	uid = Bank_GetAcctByName(username, 0);
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

	// Log the change
	Log_Info("Updated '%s' with flag set '%s' by '%s' - Reason: %s",
		username, flags, Client->Username, reason);
	
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

	if(!require_auth(Client))	return;

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
		sendf(Client->Socket, "407 Invalid price set\n");
	}
	
	switch( DispenseUpdateItem( Client->UID, item, description, price ) )
	{
	case 0:
		// Return OK
		sendf(Client->Socket, "200 Item updated\n");
		break;
	default:
		break;
	}
}

void Server_Cmd_PINCHECK(tClient *Client, char *Args)
{
	char	*username, *pinstr;
	 int	pin;

	if( Server_int_ParseArgs(0, Args, &username, &pinstr, NULL) ) {
		sendf(Client->Socket, "407 PIN_CHECK takes 2 arguments\n");
		return ;
	}
	
	if( !isdigit(pinstr[0]) || !isdigit(pinstr[1]) || !isdigit(pinstr[2]) || !isdigit(pinstr[3]) || pinstr[4] != '\0' ) {
		sendf(Client->Socket, "407 PIN should be four digits\n");
		return ;
	}
	pin = atoi(pinstr);

	if(!require_auth(Client))	return;
	
	// Get user
	int uid = Bank_GetAcctByName(username, 0);
	if( uid == -1 ) {
		sendf(Client->Socket, "404 User '%s' not found\n", username);
		return ;
	}
	
	// Check user permissions
	if( uid != Client->UID && !(Bank_GetFlags(Client->UID) & (USER_FLAG_COKE|USER_FLAG_ADMIN))  ) {
		sendf(Client->Socket, "403 Not in coke\n");
		return ;
	}
	
	// Get the pin
	static time_t	last_wrong_pin_time;
	static int	backoff = 1;
	if( time(NULL) - last_wrong_pin_time < backoff ) {
		sendf(Client->Socket, "407 Rate limited (%i seconds remaining)\n",
			backoff - (time(NULL) - last_wrong_pin_time));
		return ;
	}	
	last_wrong_pin_time = time(NULL);
	if( !Bank_IsPinValid(uid, pin) )
	{
		sendf(Client->Socket, "401 Pin incorrect\n");
		struct sockaddr_storage	addr;
		socklen_t len = sizeof(addr);
		char ipstr[INET6_ADDRSTRLEN];
		getpeername(Client->Socket, (void*)&addr, &len);
		struct sockaddr_in *s = (struct sockaddr_in *)&addr;
		inet_ntop(addr.ss_family, &s->sin_addr, ipstr, sizeof(ipstr));
		Debug_Notice("Bad pin from %s for %s by %i", ipstr, username, Client->UID);
		if( backoff < 5)
			backoff ++;
		return ;
	}

	last_wrong_pin_time = 0;
	backoff = 1;
	sendf(Client->Socket, "200 Pin correct\n");
	return ;
}
void Server_Cmd_PINSET(tClient *Client, char *Args)
{
	char	*pinstr;
	 int	pin;
	

	if( Server_int_ParseArgs(0, Args, &pinstr, NULL) ) {
		sendf(Client->Socket, "407 PIN_SET takes 1 argument\n");
		return ;
	}
	
	if( !isdigit(pinstr[0]) || !isdigit(pinstr[1]) || !isdigit(pinstr[2]) || !isdigit(pinstr[3]) || pinstr[4] != '\0' ) {
		sendf(Client->Socket, "407 PIN should be four digits\n");
		return ;
	}
	pin = atoi(pinstr);

	if(!require_auth(Client))	return;
	
	int uid = Client->EffectiveUID > 0 ? Client->EffectiveUID : Client->UID;
	CLIENT_DEBUG(Client, "Setting PIN for UID %i", uid);
	// Can only pinset yourself (well, the effective user)
	Bank_SetPin(uid, pin);
	sendf(Client->Socket, "200 Pin updated\n");
	return ;
}
void Server_Cmd_CARDADD(tClient* Client, char* Args)
{
	char* card_id;
	if( Server_int_ParseArgs(0, Args, &card_id, NULL) ) {
		sendf(Client->Socket, "407 CARD_ADD takes 1 argument\n");
		return ;
	}

	if(!require_auth(Client))	return;

	int uid = Client->EffectiveUID > 0 ? Client->EffectiveUID : Client->UID;
	CLIENT_DEBUG(Client, "Add card '%s' to UID %i", card_id, uid);
	if( Bank_AddAcctCard(uid, card_id) )
	{
		sendf(Client->Socket, "408 Card already exists\n");
		return ;
	}
	sendf(Client->Socket, "200 Card added\n");
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

