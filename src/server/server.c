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

// HACKS
#define HACK_TPG_NOAUTH	1
#define HACK_ROOT_NOAUTH	1

#define	DEBUG_TRACE_CLIENT	0

// Statistics
#define MAX_CONNECTION_QUEUE	5
#define INPUT_BUFFER_SIZE	256

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
void	Server_Cmd_ENUMITEMS(tClient *Client, char *Args);
void	Server_Cmd_ITEMINFO(tClient *Client, char *Args);
void	Server_Cmd_DISPENSE(tClient *Client, char *Args);
void	Server_Cmd_GIVE(tClient *Client, char *Args);
void	Server_Cmd_ADD(tClient *Client, char *Args);
void	Server_Cmd_ENUMUSERS(tClient *Client, char *Args);
void	Server_Cmd_USERINFO(tClient *Client, char *Args);
void	_SendUserInfo(tClient *Client, int UserID);
// --- Helpers ---
 int	sendf(int Socket, const char *Format, ...);
 int	GetUserAuth(const char *Salt, const char *Username, const uint8_t *Hash);
void	HexBin(uint8_t *Dest, char *Src, int BufSize);

// === GLOBALS ===
 int	giServer_Port = 1020;
 int	giServer_NextClientID = 1;
// - Commands
struct sClientCommand {
	char	*Name;
	void	(*Function)(tClient *Client, char *Arguments);
}	gaServer_Commands[] = {
	{"USER", Server_Cmd_USER},
	{"PASS", Server_Cmd_PASS},
	{"AUTOAUTH", Server_Cmd_AUTOAUTH},
	{"ENUM_ITEMS", Server_Cmd_ENUMITEMS},
	{"ITEM_INFO", Server_Cmd_ITEMINFO},
	{"DISPENSE", Server_Cmd_DISPENSE},
	{"GIVE", Server_Cmd_GIVE},
	{"ADD", Server_Cmd_ADD},
	{"ENUM_USERS", Server_Cmd_ENUMUSERS},
	{"USER_INFO", Server_Cmd_USERINFO}
};
#define NUM_COMMANDS	(sizeof(gaServer_Commands)/sizeof(gaServer_Commands[0]))
 int	giServer_Socket;

// === CODE ===
/**
 * \brief Open listenting socket and serve connections
 */
void Server_Start(void)
{
	 int	client_socket;
	struct sockaddr_in	server_addr, client_addr;

	atexit(Server_Cleanup);

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
	
	// Listen
	if( listen(giServer_Socket, MAX_CONNECTION_QUEUE) < 0 ) {
		fprintf(stderr, "ERROR: Unable to listen to socket\n");
		perror("Listen");
		return ;
	}
	
	printf("Listening on 0.0.0.0:%i\n", giServer_Port);
	
	for(;;)
	{
		uint	len = sizeof(client_addr);
		 int	bTrusted = 0;
		
		client_socket = accept(giServer_Socket, (struct sockaddr *) &client_addr, &len);
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

void Server_Cleanup(void)
{
	printf("Close(%i)\n", giServer_Socket);
	close(giServer_Socket);
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
	clientInfo.Socket = Socket;
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
		if(strcmp(CommandString, gaServer_Commands[i].Name) == 0) {
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
	uint8_t	clienthash[HASH_LENGTH] = {0};
	
	// Read user's hash
	HexBin(clienthash, Args, HASH_LENGTH);
	
	// TODO: Decrypt password passed
	
	Client->UID = GetUserAuth(Client->Salt, Client->Username, clienthash);

	if( Client->UID != -1 ) {
		Client->bIsAuthed = 1;
		sendf(Client->Socket, "200 Auth OK\n");
		return ;
	}

	if( giDebugLevel ) {
		 int	i;
		printf("Client %i: Password hash ", Client->ID);
		for(i=0;i<HASH_LENGTH;i++)
			printf("%02x", clienthash[i]&0xFF);
		printf("\n");
	}
	
	sendf(Client->Socket, "401 Auth Failure\n");
}

/**
 * \brief Authenticate as a user without a password
 * 
 * Usage: AUTOAUTH <user>
 */
void Server_Cmd_AUTOAUTH(tClient *Client, char *Args)
{
	char	*spos = strchr(Args, ' ');
	if(spos)	*spos = '\0';	// Remove characters after the ' '
	
	// Check if trusted
	if( !Client->bIsTrusted ) {
		if(giDebugLevel)
			printf("Client %i: Untrusted client attempting to AUTOAUTH\n", Client->ID);
		sendf(Client->Socket, "401 Untrusted\n");
		return ;
	}
	
	// Get UID
	Client->UID = GetUserID( Args );
	if( Client->UID < 0 ) {
		if(giDebugLevel)
			printf("Client %i: Unknown user '%s'\n", Client->ID, Args);
		sendf(Client->Socket, "401 Auth Failure\n");
		return ;
	}
	
	if(giDebugLevel)
		printf("Client %i: Authenticated as '%s' (%i)\n", Client->ID, Args, Client->UID);
	
	sendf(Client->Socket, "200 Auth OK\n");
}

/**
 * \brief Enumerate the items that the server knows about
 */
void Server_Cmd_ENUMITEMS(tClient *Client, char *Args)
{
	 int	i;

	sendf(Client->Socket, "201 Items %i", giNumItems);

	for( i = 0; i < giNumItems; i ++ ) {
		sendf(Client->Socket, " %s:%i", gaItems[i].Handler->Name, gaItems[i].ID);
	}

	sendf(Client->Socket, "\n");
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
	tItem	*item = _GetItemFromString(Args);
	
	if( !item ) {
		sendf(Client->Socket, "406 Bad Item ID\n");
		return ;
	}
	
	sendf(Client->Socket,
		"202 Item %s:%i %i %s\n",
		 item->Handler->Name, item->ID, item->Price, item->Name
		 );
}

void Server_Cmd_DISPENSE(tClient *Client, char *Args)
{
	tItem	*item;
	 int	ret;
	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
		return ;
	}

	item = _GetItemFromString(Args);
	if( !item ) {
		sendf(Client->Socket, "406 Bad Item ID\n");
		return ;
	}

	switch( ret = DispenseItem( Client->UID, item ) )
	{
	case 0:	sendf(Client->Socket, "200 Dispense OK\n");	return ;
	case 1:	sendf(Client->Socket, "501 Unable to dispense\n");	return ;
	case 2:	sendf(Client->Socket, "402 Poor You\n");	return ;
	default:
		sendf(Client->Socket, "500 Dispense Error\n");
		return ;
	}
}

void Server_Cmd_GIVE(tClient *Client, char *Args)
{
	char	*recipient, *ammount, *reason;
	 int	uid, iAmmount;
	
	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
		return ;
	}

	recipient = Args;

	ammount = strchr(Args, ' ');
	if( !ammount ) {
		sendf(Client->Socket, "407 Invalid Argument, expected 3 parameters, 1 encountered\n");
		return ;
	}
	*ammount = '\0';
	ammount ++;

	reason = strchr(ammount, ' ');
	if( !reason ) {
		sendf(Client->Socket, "407 Invalid Argument, expected 3 parameters, 2 encountered\n");
		return ;
	}
	*reason = '\0';
	reason ++;

	// Get recipient
	uid = GetUserID(recipient);
	if( uid == -1 ) {
		sendf(Client->Socket, "404 Invalid target user\n");
		return ;
	}

	// Parse ammount
	iAmmount = atoi(ammount);
	if( iAmmount <= 0 ) {
		sendf(Client->Socket, "407 Invalid Argument, ammount must be > zero\n");
		return ;
	}

	// Do give
	switch( DispenseGive(Client->UID, uid, iAmmount, reason) )
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
	
	if( !Client->bIsAuthed ) {
		sendf(Client->Socket, "401 Not Authenticated\n");
		return ;
	}

	user = Args;

	ammount = strchr(Args, ' ');
	if( !ammount ) {
		sendf(Client->Socket, "407 Invalid Argument, expected 3 parameters, 1 encountered\n");
		return ;
	}
	*ammount = '\0';
	ammount ++;

	reason = strchr(ammount, ' ');
	if( !reason ) {
		sendf(Client->Socket, "407 Invalid Argument, expected 3 parameters, 2 encountered\n");
		return ;
	}
	*reason = '\0';
	reason ++;

	// TODO: Check if the current user is in coke/higher
	if( (GetFlags(Client->UID) & USER_FLAG_TYPEMASK) < USER_TYPE_COKE ) {
		sendf(Client->Socket, "403 Not in coke\n");
		return ;
	}

	// Get recipient
	uid = GetUserID(user);
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
	switch( DispenseAdd(uid, Client->UID, iAmmount, reason) )
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
	 int	maxBal = INT_MAX, minBal = INT_MIN;
	 int	numUsr = GetMaxID();
	
	// Parse arguments
	//minBal = atoi(Args);
	
	// Get return number
	for( i = 0; i < numUsr; i ++ )
	{
		int bal = GetBalance(i);
		
		if( bal == INT_MIN )	continue;
		
		if( bal < minBal )	continue;
		if( bal > maxBal )	continue;
		
		numRet ++;
	}
	
	// Send count
	sendf(Client->Socket, "201 Users %i\n", numRet);
	
	for( i = 0; i < numUsr; i ++ )
	{
		int bal = GetBalance(i);
		
		if( bal == INT_MIN )	continue;
		
		if( bal < minBal )	continue;
		if( bal > maxBal )	continue;
		
		// TODO: User flags
		_SendUserInfo(Client, i);
	}
	
	sendf(Client->Socket, "200 List End\n");
}

void Server_Cmd_USERINFO(tClient *Client, char *Args)
{
	 int	uid;
	char	*user = Args;
	char	*space;
	
	space = strchr(user, ' ');
	if(space)	*space = '\0';
	
	// Get recipient
	uid = GetUserID(user);
	if( uid == -1 ) {
		sendf(Client->Socket, "404 Invalid user");
		return ;
	}
	
	_SendUserInfo(Client, uid);
}

void _SendUserInfo(tClient *Client, int UserID)
{
	char	*type, *disabled="";
	 int	flags = GetFlags(UserID);
	
	switch( flags & USER_FLAG_TYPEMASK )
	{
	default:
	case USER_TYPE_NORMAL:	type = "user";	break;
	case USER_TYPE_COKE:	type = "coke";	break;
	case USER_TYPE_WHEEL:	type = "wheel";	break;
	case USER_TYPE_GOD:	type = "meta";	break;
	}
	
	if( flags & USER_FLAG_DISABLED )
		disabled = ",disabled";
	
	// TODO: User flags/type
	sendf(
		Client->Socket, "202 User %s %i %s%s\n",
		GetUserName(UserID), GetBalance(UserID),
		type, disabled
		);
}

/**
 * \brief Authenticate a user
 * \return User ID, or -1 if authentication failed
 */
int GetUserAuth(const char *Salt, const char *Username, const uint8_t *ProvidedHash)
{
	#if 0
	uint8_t	h[20];
	 int	ofs = strlen(Username) + strlen(Salt);
	char	input[ ofs + 40 + 1];
	char	tmp[4 + strlen(Username) + 1];	// uid=%s
	#endif
	
	#if HACK_TPG_NOAUTH
	if( strcmp(Username, "tpg") == 0 )
		return GetUserID("tpg");
	#endif
	#if HACK_ROOT_NOAUTH
	if( strcmp(Username, "root") == 0 )
		return GetUserID("root");
	#endif
	
	#if 0
	//
	strcpy(input, Username);
	strcpy(input, Salt);
	// TODO: Get user's SHA-1 hash
	sprintf(tmp, "uid=%s", Username);
	ldap_search_s(ld, "", LDAP_SCOPE_BASE, tmp, "userPassword", 0, res);
	
	sprintf(input+ofs, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
		h[ 0], h[ 1], h[ 2], h[ 3], h[ 4], h[ 5], h[ 6], h[ 7], h[ 8], h[ 9],
		h[10], h[11], h[12], h[13], h[14], h[15], h[16], h[17], h[18], h[19]
		);
	// Then create the hash from the provided salt
	// Compare that with the provided hash
	#endif
	
	return -1;
}

// --- INTERNAL HELPERS ---
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
