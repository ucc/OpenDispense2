/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 * - Dispense Client
 *
 * protocol.c
 * - Client/Server communication
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
//#define DEBUG_TRACE_SERVER	2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <netdb.h>	// gethostbyname
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
//#include <openssl/sha.h>	// SHA1
#include <pwd.h>	// getpwuids
#include <unistd.h>	// close/getuid
#include <limits.h>	// INT_MIN/INT_MAX
#include <stdarg.h>
#include <ctype.h>	// isdigit
#include "common.h"

// === PROTOTYPES ===
char	*ReadLine(int Socket);
 int	sendf(int Socket, const char *Format, ...);

// ---------------------
// --- Coke Protocol ---
// ---------------------
int OpenConnection(const char *Host, int Port)
{
	struct hostent	*host;
	struct sockaddr_in	serverAddr;
	 int	sock;
	
	host = gethostbyname(Host);
	if( !host ) {
		fprintf(stderr, "Unable to look up '%s'\n", Host);
		return -1;
	}
	
	memset(&serverAddr, 0, sizeof(serverAddr));
	
	serverAddr.sin_family = AF_INET;	// IPv4
	// NOTE: I have a suspicion that IPv6 will play sillybuggers with this :)
	serverAddr.sin_addr.s_addr = *((unsigned long *) host->h_addr_list[0]);
	serverAddr.sin_port = htons(Port);
	
	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if( sock < 0 ) {
		fprintf(stderr, "Failed to create socket\n");
		return -1;
	}

	if( geteuid() == 0 || getuid() == 0 )
	{
		 int	i;
		struct sockaddr_in	localAddr;
		memset(&localAddr, 0, sizeof(localAddr));
		localAddr.sin_family = AF_INET;	// IPv4
		
		// Loop through all the top ports until one is avaliable
		for( i = 512; i < 1024; i ++)
		{
			localAddr.sin_port = htons(i);	// IPv4
			// Attempt to bind to low port for autoauth
			if( bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr)) == 0 )
				break;
		}
		if( i == 1024 )
			printf("Warning: AUTOAUTH unavaliable\n");
	}
	
	if( connect(sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0 ) {
		fprintf(stderr, "Failed to connect to server\n");
		return -1;
	}

	// We're not authenticated if the connection has just opened
	gbIsAuthenticated = 0;
	
	return sock;
}

int Authenticate_AutoAuth(int Socket, const char *Username)
{
	char	*buf;
	 int	responseCode;
	 int	ret = -1;
	
	// Attempt automatic authentication
	sendf(Socket, "AUTOAUTH %s\n", Username);
	
	// Check if it worked
	buf = ReadLine(Socket);
	
	responseCode = atoi(buf);
	switch( responseCode )
	{
	case 200:	// Autoauth succeeded, return
		ret = 0;
		break;
	
	case 401:	// Untrusted
//		fprintf(stderr, "Untrusted host, AUTOAUTH unavaliable\n");
		ret = RV_PERMISSIONS;
		break;
	case 404:	// Bad Username
		fprintf(stderr, "Bad Username '%s'\n", Username);
		ret = RV_INVALID_USER;
		break;
	
	default:
		fprintf(stderr, "Unkown response code %i from server\n", responseCode);
		printf("%s\n", buf);
		ret = RV_UNKNOWN_ERROR;
		break;;
	}
	
	free(buf);
	return ret;
}

int Authenticate_AuthIdent(int Socket)
{
	char	*buf;
	 int	responseCode;
	 int	ret = -1;
	
	// Attempt automatic authentication
	sendf(Socket, "AUTHIDENT\n");
	
	// Check if it worked
	buf = ReadLine(Socket);
	
	responseCode = atoi(buf);
	switch( responseCode )
	{
	case 200:	// Autoauth succeeded, return
		ret = 0;
		break;
	
	case 401:	// Untrusted
//		fprintf(stderr, "Untrusted host, AUTHIDENT unavaliable\n");
		ret = RV_PERMISSIONS;
		break;
	
	default:
		fprintf(stderr, "Unkown response code %i from server\n", responseCode);
		printf("%s\n", buf);
		ret = RV_UNKNOWN_RESPONSE;
		break;
	}
	
	free(buf);

	return ret;
}

int Authenticate_Password(int Socket, const char *Username)
{
	#if USE_PASSWORD_AUTH
	char	*buf;
	 int	responseCode;	
	char	salt[32];
	 int	i;
	regmatch_t	matches[4];

	sendf(Socket, "USER %s\n", Username);
	printf("Using username %s\n", Username);
	
	buf = ReadLine(Socket);
	
	// TODO: Get Salt
	// Expected format: 100 SALT <something> ...
	// OR             : 100 User Set
	RunRegex(&gSaltRegex, buf, 4, matches, "Malformed server response");
	responseCode = atoi(buf);
	if( responseCode != 100 ) {
		fprintf(stderr, "Unknown repsonse code %i from server\n%s\n", responseCode, buf);
		free(buf);
		return RV_UNKNOWN_ERROR;	// ERROR
	}
	
	// Check for salt
	if( memcmp( buf+matches[2].rm_so, "SALT", matches[2].rm_eo - matches[2].rm_so) == 0) {
		// Store it for later
		memcpy( salt, buf + matches[3].rm_so, matches[3].rm_eo - matches[3].rm_so );
		salt[ matches[3].rm_eo - matches[3].rm_so ] = 0;
	}
	free(buf);
	
	// Give three attempts
	for( i = 0; i < 3; i ++ )
	{
		 int	ofs = strlen(Username)+strlen(salt);
		char	tmpBuf[42];
		char	tmp[ofs+20];
		char	*pass = getpass("Password: ");
		uint8_t	h[20];
		
		// Create hash string
		// <username><salt><hash>
		strcpy(tmp, Username);
		strcat(tmp, salt);
		SHA1( (unsigned char*)pass, strlen(pass), h );
		memcpy(tmp+ofs, h, 20);
		
		// Hash all that
		SHA1( (unsigned char*)tmp, ofs+20, h );
		sprintf(tmpBuf, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
			h[ 0], h[ 1], h[ 2], h[ 3], h[ 4], h[ 5], h[ 6], h[ 7], h[ 8], h[ 9],
			h[10], h[11], h[12], h[13], h[14], h[15], h[16], h[17], h[18], h[19]
			);
	
		// Send password
		sendf(Socket, "PASS %s\n", tmpBuf);
		buf = ReadLine(Socket);
	
		responseCode = atoi(buf);
		// Auth OK?
		if( responseCode == 200 )	break;
		// Bad username/password
		if( responseCode == 401 )	continue;
		
		fprintf(stderr, "Unknown repsonse code %i from server\n%s\n", responseCode, buf);
		free(buf);
		return -1;
	}
	free(buf);
	if( i == 3 )
		return RV_INVALID_USER;	// 2 = Bad Password

	return 0;
	#else
	return RV_INVALID_USER;
	#endif
}

/**
 * \brief Authenticate with the server
 * \return Boolean Failure
 */
int Authenticate(int Socket)
{
	struct passwd	*pwd;
	
	if( gbIsAuthenticated )	return 0;
	
	// Get user name
	pwd = getpwuid( getuid() );

	// Attempt AUTOAUTH
	if( Authenticate_AutoAuth(Socket, pwd->pw_name) == 0 )
		;
	else if( Authenticate_AuthIdent(Socket) == 0 )
		;
	else if( Authenticate_Password(Socket, pwd->pw_name) == 0 )
		return RV_INVALID_USER;

	// Set effective user
	if( gsEffectiveUser ) {
		char	*buf;
		 int	responseCode;
		sendf(Socket, "SETEUSER %s\n", gsEffectiveUser);
		
		buf = ReadLine(Socket);
		responseCode = atoi(buf);
		
		switch(responseCode)
		{
		case 200:
			printf("Running as '%s' by '%s'\n", gsEffectiveUser, pwd->pw_name);
			break;
		
		case 403:
			printf("Only coke members can use `dispense -u`\n");
			free(buf);
			return RV_PERMISSIONS;
		
		case 404:
			printf("Invalid user selected\n");
			free(buf);
			return RV_INVALID_USER;
		
		default:
			fprintf(stderr, "Unkown response code %i from server\n", responseCode);
			printf("%s\n", buf);
			free(buf);
			return RV_UNKNOWN_ERROR;
		}
		
		free(buf);
	}
	
	gbIsAuthenticated = 1;
	
	return 0;
}

int GetUserBalance(int Socket)
{
	regmatch_t	matches[6];
	struct passwd	*pwd;
	char	*buf;
	 int	responseCode;
	
	if( !gsUserName )
	{
		if( gsEffectiveUser ) {
			gsUserName = gsEffectiveUser;
		}
		else {
			pwd = getpwuid( getuid() );
			gsUserName = strdup(pwd->pw_name);
		}
	}
	
	sendf(Socket, "USER_INFO %s\n", gsUserName);
	buf = ReadLine(Socket);
	responseCode = atoi(buf);
	switch(responseCode)
	{
	case 202:	break;	// Ok
	
	case 404:
		printf("Invalid user? (USER_INFO failed)\n");
		free(buf);
		return RV_INVALID_USER;
	
	default:
		fprintf(stderr, "Unkown response code %i from server\n", responseCode);
		printf("%s\n", buf);
		free(buf);
		return RV_UNKNOWN_ERROR;
	}

	RunRegex(&gUserInfoRegex, buf, 6, matches, "Malformed server response");
	
	giUserBalance = atoi( buf + matches[4].rm_so );
	gsUserFlags = strdup( buf + matches[5].rm_so );
	
	free(buf);
	
	return 0;
}

/**
 * \brief Read an item info response from the server
 * \param Dest	Destination for the read item (strings will be on the heap)
 */
int ReadItemInfo(int Socket, tItem *Dest)
{
	char	*buf;
	 int	responseCode;
	
	regmatch_t	matches[8];
	char	*statusStr;
	
	// Get item info
	buf = ReadLine(Socket);
	responseCode = atoi(buf);
	
	switch(responseCode)
	{
	case 202:	break;
	
	case 406:
		printf("Bad item name\n");
		free(buf);
		return RV_BAD_ITEM;
	
	default:
		fprintf(stderr, "Unknown response from dispense server (Response Code %i)\n%s", responseCode, buf);
		free(buf);
		return RV_UNKNOWN_ERROR;
	}
	
	RunRegex(&gItemRegex, buf, 8, matches, "Malformed server response");
	
	buf[ matches[3].rm_eo ] = '\0';
	buf[ matches[5].rm_eo ] = '\0';
	buf[ matches[7].rm_eo ] = '\0';
	
	statusStr = &buf[ matches[5].rm_so ];
	
	Dest->ID = atoi( buf + matches[4].rm_so );
	
	if( strcmp(statusStr, "avail") == 0 )
		Dest->Status = 0;
	else if( strcmp(statusStr, "sold") == 0 )
		Dest->Status = 1;
	else if( strcmp(statusStr, "error") == 0 )
		Dest->Status = -1;
	else {
		fprintf(stderr, "Unknown response from dispense server (status '%s')\n",
			statusStr);
		return RV_UNKNOWN_ERROR;
	}
	Dest->Price = atoi( buf + matches[6].rm_so );
	
	// Hack a little to reduce heap fragmentation
	{
		char	tmpType[strlen(buf + matches[3].rm_so) + 1];
		char	tmpDesc[strlen(buf + matches[7].rm_so) + 1];
		strcpy(tmpType, buf + matches[3].rm_so);
		strcpy(tmpDesc, buf + matches[7].rm_so);
		free(buf);
		Dest->Type = strdup( tmpType );
		Dest->Desc = strdup( tmpDesc );
	}
	
	return 0;
}

/**
 * \brief Fill the item information structure
 * \return Boolean Failure
 */
void PopulateItemList(int Socket)
{
	char	*buf;
	 int	responseCode;
	
	char	*arrayType;
	 int	count, i;
	regmatch_t	matches[4];
	
	// Ask server for stock list
	send(Socket, "ENUM_ITEMS\n", 11, 0);
	buf = ReadLine(Socket);
	
	//printf("Output: %s\n", buf);
	
	responseCode = atoi(buf);
	if( responseCode != 201 ) {
		fprintf(stderr, "Unknown response from dispense server (Response Code %i)\n", responseCode);
		exit(RV_UNKNOWN_ERROR);
	}
	
	// - Get item list -
	
	// Expected format:
	//  201 Items <count>
	//  202 Item <count>
	RunRegex(&gArrayRegex, buf, 4, matches, "Malformed server response");
		
	arrayType = &buf[ matches[2].rm_so ];	buf[ matches[2].rm_eo ] = '\0';
	count = atoi( &buf[ matches[3].rm_so ] );
		
	// Check array type
	if( strcmp(arrayType, "Items") != 0 ) {
		// What the?!
		fprintf(stderr, "Unexpected array type, expected 'Items', got '%s'\n",
			arrayType);
		exit(RV_UNKNOWN_ERROR);
	}
	free(buf);
	
	giNumItems = count;
	gaItems = malloc( giNumItems * sizeof(tItem) );
	
	// Fetch item information
	for( i = 0; i < giNumItems; i ++ )
	{
		ReadItemInfo( Socket, &gaItems[i] );
	}
	
	// Read end of list
	buf = ReadLine(Socket);
	responseCode = atoi(buf);
		
	if( responseCode != 200 ) {
		fprintf(stderr, "Unknown response from dispense server %i\n'%s'",
			responseCode, buf
			);
		exit(-1);
	}
	
	free(buf);
}


/**
 * \brief Get information on an item
 * \return Boolean Failure
 */
int Dispense_ItemInfo(int Socket, const char *Type, int ID)
{
	tItem	item;
	 int	ret;
	
	// Query
	sendf(Socket, "ITEM_INFO %s:%i\n", Type, ID);
	
	ret = ReadItemInfo(Socket, &item);
	if(ret)	return ret;
	
	printf("%8s:%-2i %2i.%02i %s\n",
		item.Type, item.ID,
		item.Price/100, item.Price%100,
		item.Desc);
	
	free(item.Type);
	free(item.Desc);
	
	return 0;
}

int DispenseCheckPin(int Socket, const char *Username, const char *Pin)
{
	 int	ret, responseCode;
	char	*buf;
	
	if( strlen(Pin) != 4 ) {
		fprintf(stderr, "Pin format incorrect (not 4 characters long)\n");
		return RV_ARGUMENTS;
	}
		
	for( int i = 0; i < 4; i ++ ) {
		if( !isdigit(Pin[i]) ) {
			fprintf(stderr, "Pin format incorrect (character %i not a digit)\n", i);
			return RV_ARGUMENTS;
		}
	}
	
	sendf(Socket, "PIN_CHECK %s %s\n", Username, Pin);
	buf = ReadLine(Socket);
	
	responseCode = atoi(buf);
	switch( responseCode )
	{
	case 200:	// Pin correct
		printf("Pin OK\n");
		ret = 0;
		break;
	case 201:
		printf("Pin incorrect\n");
		ret = RV_INVALID_USER;
		break;
	case 401:
		printf("Not authenticated\n");
		ret = RV_PERMISSIONS;
		break;
	case 403:
		printf("Only coke members can check accounts other than their own\n");
		ret = RV_PERMISSIONS;
		break;
	case 404:
		printf("User '%s' not found\n", Username);
		ret = RV_INVALID_USER;
		break;
	case 407:
		printf("Rate limited or client-server disagree on pin format\n");
		ret = RV_SERVER_ERROR;
		break;
	default:
		printf("Unknown response code %i ('%s')\n", responseCode, buf);
		ret = RV_UNKNOWN_ERROR;
		break;
	}
	free(buf);
	return ret;
}

int DispenseSetPin(int Socket, const char *Pin)
{
	 int	ret, responseCode;
	char	*buf;
	
	if( strlen(Pin) != 4 ) {
		fprintf(stderr, "Pin format incorrect (not 4 characters long)\n");
		return RV_ARGUMENTS;
	}
		
	for( int i = 0; i < 4; i ++ ) {
		if( !isdigit(Pin[i]) ) {
			fprintf(stderr, "Pin format incorrect (character %i not a digit)\n", i);
			return RV_ARGUMENTS;
		}
	}
	
	sendf(Socket, "PIN_SET %s\n", Pin);
	buf = ReadLine(Socket);
	
	responseCode = atoi(buf);
	switch(responseCode)
	{
	case 200:
		printf("Pin Updated\n");
		ret = 0;
		break;
	case 401:
		printf("Not authenticated\n");
		ret = RV_PERMISSIONS;
		break;
	case 407:
		printf("Client/server disagreement on pin format\n");
		ret = RV_SERVER_ERROR;
		break;
	default:
		printf("Unknown response code %i ('%s')\n", responseCode, buf);
		ret = RV_UNKNOWN_ERROR;
		break;
	}
	return ret;
}

/**
 * \brief Dispense an item
 * \return Boolean Failure
 */
int DispenseItem(int Socket, const char *Type, int ID)
{
	 int	ret, responseCode;
	char	*buf;
	
	// Check for a dry run
	if( gbDryRun ) {
		printf("Dry Run - No action\n");
		return 0;
	}
	
	// Dispense!
	sendf(Socket, "DISPENSE %s:%i\n", Type, ID);
	buf = ReadLine(Socket);
	
	responseCode = atoi(buf);
	switch( responseCode )
	{
	case 200:
		printf("Dispense OK\n");
		ret = 0;
		break;
	case 401:
		printf("Not authenticated\n");
		ret = RV_PERMISSIONS;
		break;
	case 402:
		printf("Insufficient balance\n");
		ret = RV_BALANCE;
		break;
	case 406:
		printf("Bad item name\n");
		ret = RV_BAD_ITEM;
		break;
	case 500:
		printf("Item failed to dispense, is the slot empty?\n");
		ret = RV_SERVER_ERROR;
		break;
	case 501:
		printf("Dispense not possible (slot empty/permissions)\n");
		ret = RV_SERVER_ERROR;
		break;
	default:
		printf("Unknown response code %i ('%s')\n", responseCode, buf);
		ret = RV_UNKNOWN_ERROR;
		break;
	}
	
	free(buf);
	return ret;
}

/**
 * \brief Alter a user's balance
 */
int Dispense_AlterBalance(int Socket, const char *Username, int Ammount, const char *Reason)
{
	char	*buf;
	 int	responseCode, rv = -1;
	
	// Check for a dry run
	if( gbDryRun ) {
		printf("Dry Run - No action\n");
		return 0;
	}

	// Sanity
	if( Ammount == 0 ) {
		printf("An amount would be nice\n");
		return RV_ARGUMENTS;
	}
	
	sendf(Socket, "ADD %s %i %s\n", Username, Ammount, Reason);
	buf = ReadLine(Socket);
	
	responseCode = atoi(buf);
	
	switch(responseCode)
	{
	case 200:
		rv = 0;	// OK
		break;
	case 402:
		fprintf(stderr, "Insufficient balance\n");
		rv = RV_BAD_ITEM;
		break;
	case 403:	// Not in coke
		fprintf(stderr, "Permissions error: %s\n", buf+4);
		rv = RV_PERMISSIONS;
		break;
	case 404:	// Unknown user
		fprintf(stderr, "Unknown user '%s'\n", Username);
		rv = RV_INVALID_USER;
		break;
	default:
		fprintf(stderr, "Unknown response code %i\n'%s'\n", responseCode, buf);
		rv = RV_UNKNOWN_RESPONSE;
		break;
	}
	free(buf);
	
	return rv;
}

/**
 * \brief Set a user's balance
 * \note Only avaliable to dispense admins
 */
int Dispense_SetBalance(int Socket, const char *Username, int Balance, const char *Reason)
{
	char	*buf;
	 int	responseCode;
	
	// Check for a dry run
	if( gbDryRun ) {
		printf("Dry Run - No action\n");
		return 0;
	}
	
	sendf(Socket, "SET %s %i %s\n", Username, Balance, Reason);
	buf = ReadLine(Socket);
	
	responseCode = atoi(buf);
	free(buf);
	
	switch(responseCode)
	{
	case 200:	return 0;	// OK
	case 403:	// Not an administrator
		fprintf(stderr, "You are not an admin\n");
		return RV_PERMISSIONS;
	case 404:	// Unknown user
		fprintf(stderr, "Unknown user '%s'\n", Username);
		return RV_INVALID_USER;
	default:
		fprintf(stderr, "Unknown response code %i\n", responseCode);
		return RV_UNKNOWN_RESPONSE;
	}
	
	return -1;
}

/**
 * \brief Give money to another user
 */
int Dispense_Give(int Socket, const char *Username, int Ammount, const char *Reason)
{
	char	*buf;
	 int	responseCode;
	
	if( Ammount < 0 ) {
		printf("Sorry, you can only give, you can't take.\n");
		return RV_ARGUMENTS;
	}
	
	// Fast return on zero
	if( Ammount == 0 ) {
		printf("Are you actually going to give any?\n");
		return RV_ARGUMENTS;
	}
	
	// Check for a dry run
	if( gbDryRun ) {
		printf("Dry Run - No action\n");
		return 0;
	}
	
	sendf(Socket, "GIVE %s %i %s\n", Username, Ammount, Reason);

	buf = ReadLine(Socket);
	responseCode = atoi(buf);
	free(buf);	
	switch(responseCode)
	{
	case 200:
		printf("Give succeeded\n");
		return RV_SUCCESS;	// OK
	
	case 402:	
		fprintf(stderr, "Insufficient balance\n");
		return RV_BALANCE;
	
	case 404:	// Unknown user
		fprintf(stderr, "Unknown user '%s'\n", Username);
		return RV_INVALID_USER;
	
	default:
		fprintf(stderr, "Unknown response code %i\n", responseCode);
		return RV_UNKNOWN_RESPONSE;
	}
	
	return -1;
}

int Dispense_Refund(int Socket, const char *Username, const char *Item, int PriceOverride)
{
	char	*buf;
	 int	responseCode, ret = -1;
	
	// Check item id
	if( RunRegex(&gUserItemIdentRegex, Item, 0, NULL, NULL) != 0 )
	{
		fprintf(stderr, "Error: Invalid item ID passed (should be <type>:<num>)\n");
		return RV_ARGUMENTS;
	}

	// Check username (quick)
	if( strchr(Username, ' ') || strchr(Username, '\n') )
	{
		fprintf(stderr, "Error: Username is invalid (no spaces or newlines please)\n");
		return RV_ARGUMENTS;
	}

	// Send the query
	sendf(Socket, "REFUND %s %s %i\n", Username, Item, PriceOverride);

	buf = ReadLine(Socket);
	responseCode = atoi(buf);
	switch(responseCode)
	{
	case 200:
		Dispense_ShowUser(Socket, Username);	// Show destination account
		ret = 0;
		break;
	case 403:
		fprintf(stderr, "Refund access is only avaliable to coke members\n");
		ret = RV_PERMISSIONS;
		break;
	case 404:
		fprintf(stderr, "Unknown user '%s' passed\n", Username);
		ret = RV_INVALID_USER;
		break;
	case 406:
		fprintf(stderr, "Invalid item '%s' passed\n", Item);
		ret = RV_BAD_ITEM;
		break;
	default:
		fprintf(stderr, "Unknown response from server %i\n%s\n", responseCode, buf);
		ret = -1;
		break;
	}
	free(buf);
	return ret;
}

/**
 * \brief Donate money to the club
 */
int Dispense_Donate(int Socket, int Ammount, const char *Reason)
{
	char	*buf;
	 int	responseCode;
	
	if( Ammount < 0 ) {
		printf("Sorry, you can only give, you can't take.\n");
		return -1;
	}
	
	// Fast return on zero
	if( Ammount == 0 ) {
		printf("Are you actually going to give any?\n");
		return 1;
	}
	
	// Check for a dry run
	if( gbDryRun ) {
		printf("Dry Run - No action\n");
		return 0;
	}
	
	sendf(Socket, "DONATE %i %s\n", Ammount, Reason);
	buf = ReadLine(Socket);
	
	responseCode = atoi(buf);
	free(buf);
	
	switch(responseCode)
	{
	case 200:	return 0;	// OK
	
	case 402:	
		fprintf(stderr, "Insufficient balance\n");
		return 1;
	
	default:
		fprintf(stderr, "Unknown response code %i\n", responseCode);
		return -1;
	}
	
	return -1;
}

/**
 * \brief Enumerate users
 */
int Dispense_EnumUsers(int Socket)
{
	char	*buf;
	 int	responseCode;
	 int	nUsers;
	regmatch_t	matches[4];
	
	if( giMinimumBalance != INT_MIN ) {
		if( giMaximumBalance != INT_MAX ) {
			sendf(Socket, "ENUM_USERS min_balance:%i max_balance:%i\n", giMinimumBalance, giMaximumBalance);
		}
		else {
			sendf(Socket, "ENUM_USERS min_balance:%i\n", giMinimumBalance);
		}
	}
	else {
		if( giMaximumBalance != INT_MAX ) {
			sendf(Socket, "ENUM_USERS max_balance:%i\n", giMaximumBalance);
		}
		else {
			sendf(Socket, "ENUM_USERS\n");
		}
	}
	buf = ReadLine(Socket);
	responseCode = atoi(buf);
	
	switch(responseCode)
	{
	case 201:	break;	// Ok, length follows
	
	default:
		fprintf(stderr, "Unknown response code %i\n%s\n", responseCode, buf);
		free(buf);
		return -1;
	}
	
	// Get count (not actually used)
	RunRegex(&gArrayRegex, buf, 4, matches, "Malformed server response");
	nUsers = atoi( buf + matches[3].rm_so );
	printf("%i users returned\n", nUsers);
	
	// Free string
	free(buf);
	
	// Read returned users
	do {
		buf = ReadLine(Socket);
		responseCode = atoi(buf);
		
		if( responseCode != 202 )	break;
		
		_PrintUserLine(buf);
		free(buf);
	} while(responseCode == 202);
	
	// Check final response
	if( responseCode != 200 ) {
		fprintf(stderr, "Unknown response code %i\n%s\n", responseCode, buf);
		free(buf);
		return -1;
	}
	
	free(buf);
	
	return 0;
}

int Dispense_ShowUser(int Socket, const char *Username)
{
	char	*buf;
	 int	responseCode, ret;
	
	sendf(Socket, "USER_INFO %s\n", Username);
	buf = ReadLine(Socket);
	
	responseCode = atoi(buf);
	
	switch(responseCode)
	{
	case 202:
		_PrintUserLine(buf);
		ret = 0;
		break;
	
	case 404:
		printf("Unknown user '%s'\n", Username);
		ret = 1;
		break;
	
	default:
		fprintf(stderr, "Unknown response code %i '%s'\n", responseCode, buf);
		ret = -1;
		break;
	}
	
	free(buf);
	
	return ret;
}

void _PrintUserLine(const char *Line)
{
	regmatch_t	matches[6];
	 int	bal;
	
	RunRegex(&gUserInfoRegex, Line, 6, matches, "Malformed server response");
	// 3: Username
	// 4: Balance
	// 5: Flags
	{
		 int	usernameLen = matches[3].rm_eo - matches[3].rm_so;
		char	username[usernameLen + 1];
		 int	flagsLen = matches[5].rm_eo - matches[5].rm_so;
		char	flags[flagsLen + 1];
		
		memcpy(username, Line + matches[3].rm_so, usernameLen);
		username[usernameLen] = '\0';
		memcpy(flags, Line + matches[5].rm_so, flagsLen);
		flags[flagsLen] = '\0';
		
		bal = atoi(Line + matches[4].rm_so);
		printf("%-15s: $%8.02f (%s)\n", username, ((float)bal)/100, flags);
	}
}

int Dispense_AddUser(int Socket, const char *Username)
{
	char	*buf;
	 int	responseCode, ret;
	
	// Check for a dry run
	if( gbDryRun ) {
		printf("Dry Run - No action\n");
		return 0;
	}
	
	sendf(Socket, "USER_ADD %s\n", Username);
	
	buf = ReadLine(Socket);
	responseCode = atoi(buf);
	
	switch(responseCode)
	{
	case 200:
		printf("User '%s' added\n", Username);
		ret = 0;
		break;
		
	case 403:
		printf("Only wheel can add users\n");
		ret = 1;
		break;
		
	case 404:
		printf("User '%s' already exists\n", Username);
		ret = 0;
		break;
	
	default:
		fprintf(stderr, "Unknown response code %i '%s'\n", responseCode, buf);
		ret = -1;
		break;
	}
	
	free(buf);
	
	return ret;
}

int Dispense_SetUserType(int Socket, const char *Username, const char *TypeString, const char *Reason)
{
	char	*buf;
	 int	responseCode, ret;
	
	// Check for a dry run
	if( gbDryRun ) {
		printf("Dry Run - No action\n");
		return 0;
	}
	
	// TODO: Pre-validate the string
	
	sendf(Socket, "USER_FLAGS %s %s %s\n", Username, TypeString, Reason);
	
	buf = ReadLine(Socket);
	responseCode = atoi(buf);
	
	switch(responseCode)
	{
	case 200:
		printf("User '%s' updated\n", Username);
		ret = 0;
		break;
		
	case 403:
		printf("Only dispense admins can modify users\n");
		ret = RV_PERMISSIONS;
		break;
	
	case 404:
		printf("User '%s' does not exist\n", Username);
		ret = RV_INVALID_USER;
		break;
	
	case 407:
		printf("Flag string is invalid\n");
		ret = RV_ARGUMENTS;
		break;
	
	default:
		fprintf(stderr, "Unknown response code %i '%s'\n", responseCode, buf);
		ret = RV_UNKNOWN_RESPONSE;
		break;
	}
	
	free(buf);
	
	return ret;
}

int Dispense_SetItem(int Socket, const char *Type, int ID, int NewPrice, const char *NewName)
{
	char	*buf;
	 int	responseCode, ret;
	
	// Check for a dry run
	if( gbDryRun ) {
		printf("Dry Run - No action\n");
		return 0;
	}
	
	sendf(Socket, "UPDATE_ITEM %s:%i %i %s\n", Type, ID, NewPrice, NewName);
	
	buf = ReadLine(Socket);
	responseCode = atoi(buf);
	
	switch(responseCode)
	{
	case 200:
		printf("Item %s:%i updated\n", Type, ID);
		ret = 0;
		break;
		
	case 403:
		printf("Only coke members can modify the slots\n");
		ret = RV_PERMISSIONS;
		break;
	
	case 406:
		printf("Invalid item passed\n");
		ret = RV_BAD_ITEM;
		break;
	
	default:
		fprintf(stderr, "Unknown response code %i '%s'\n", responseCode, buf);
		ret = -1;
		break;
	}
	
	free(buf);
	
	return ret;
}

// ===
// Helpers
// ===
/// Read from the input socket until a newline is seen
char *ReadLine(int Socket)
{
	static char	buf[BUFSIZ];
	static int	bufValid = 0;
	 int	len = 0;
	char	*newline = NULL;
	 int	retLen = 0;
	char	*ret = malloc(32);
	
	#if DEBUG_TRACE_SERVER
	printf("ReadLine: ");
	fflush(stdout);
	#endif
	
	ret[0] = '\0';
	
	// While a newline hasn't been seen
	while( !newline )
	{
		assert(bufValid < BUFSIZ);
		// If there is data left over from a previous call, use the data from that for the first pass
		if( bufValid ) {
			len = bufValid;
			bufValid = 0;
		}
		else {
			// Otherwise read some data
			len = recv(Socket, buf, BUFSIZ, 0);
			if( len <= 0 ) {
				free(ret);
				return strdup("599 Client Connection Error\n");
			}
		}
		assert(len < BUFSIZ);
		buf[len] = '\0';
		
		// Search for newline in buffer
		newline = strchr( buf, '\n' );
		if( newline ) {
			*newline = '\0';
		}
		
		// Increment return length by amount of data up to newline (or end of read)
		retLen += strlen(buf);
		ret = realloc(ret, retLen + 1);
		assert(ret);	// evil NULL check
		strcat( ret, buf );	// append buffer data
	}
	
	#if DEBUG_TRACE_SERVER
	printf("%i '%s'\n", retLen, ret);
	#endif

	// If the newline wasn't the last character in the buffer. (I.e. there's extra data for the next call)
	assert(newline - buf + 1 <= len);
	if( newline - buf + 1 < len ) {
		int extra_bytes = len - (newline - buf + 1);
		// Copy `extra_bytes` from end of buffer down to start and set `bufValid` to `extra_bytes`?
		memmove(&buf[0], newline + 1, extra_bytes);
		bufValid = extra_bytes;
		
		#if DEBUG_TRACE_SERVER > 1
		printf("- Caching %i bytes '%.*s'\n", bufValid, bufValid, buf);
		#endif
	}
	
	return ret;
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
		
		#if DEBUG_TRACE_SERVER
		printf("sendf: %s", buf);
		#endif
		
		return send(Socket, buf, len, 0);
	}
}

