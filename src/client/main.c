/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 * - Dispense Client
 *
 * main.c - Core and Initialisation
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>	// isspace
#include <stdarg.h>
#include <regex.h>
#include <ncurses.h>

#include <unistd.h>	// close
#include <netdb.h>	// gethostbyname
#include <pwd.h>	// getpwuids
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>	// SHA1

#define	USE_NCURSES_INTERFACE	0
#define DEBUG_TRACE_SERVER	0

// === TYPES ===
typedef struct sItem {
	char	*Ident;
	char	*Desc;
	 int	Price;
}	tItem;

// === PROTOTYPES ===
 int	main(int argc, char *argv[]);
void	ShowUsage(void);
// --- GUI ---
 int	ShowNCursesUI(void);
void	ShowItemAt(int Row, int Col, int Width, int Index);
void	PrintAlign(int Row, int Col, int Width, const char *Left, char Pad1, const char *Mid, char Pad2, const char *Right, ...);
// --- Coke Server Communication ---
 int	OpenConnection(const char *Host, int Port);
 int	Authenticate(int Socket);
void	PopulateItemList(int Socket);
 int	DispenseItem(int Socket, int ItemID);
 int	Dispense_AlterBalance(int Socket, const char *Username, int Ammount, const char *Reason);
 int	Dispense_SetBalance(int Socket, const char *Username, int Ammount, const char *Reason);
 int	Dispense_EnumUsers(int Socket);
 int	Dispense_ShowUser(int Socket, const char *Username);
void	_PrintUserLine(const char *Line);
// --- Helpers ---
char	*ReadLine(int Socket);
 int	sendf(int Socket, const char *Format, ...);
char	*trim(char *string);
 int	RunRegex(regex_t *regex, const char *string, int nMatches, regmatch_t *matches, const char *errorMessage);
void	CompileRegex(regex_t *regex, const char *pattern, int flags);

// === GLOBALS ===
char	*gsDispenseServer = "localhost";
 int	giDispensePort = 11020;

tItem	*gaItems;
 int	giNumItems;
regex_t	gArrayRegex, gItemRegex, gSaltRegex, gUserInfoRegex;
 int	gbIsAuthenticated = 0;

char	*gsOverrideUser;	//!< '-u' argument (dispense as another user)
 int	gbUseNCurses = 0;	//!< '-G' Use the NCurses GUI?
 int	giSocket = -1;

// === CODE ===
int main(int argc, char *argv[])
{
	 int	sock;
	 int	i;
	char	buffer[BUFSIZ];
	
	// -- Create regular expressions
	// > Code Type Count ...
	CompileRegex(&gArrayRegex, "^([0-9]{3})\\s+([A-Za-z]+)\\s+([0-9]+)", REG_EXTENDED);	//
	// > Code Type Ident Price Desc
	CompileRegex(&gItemRegex, "^([0-9]{3})\\s+([A-Za-z]+)\\s+([A-Za-z0-9:]+?)\\s+([0-9]+)\\s+(.+)$", REG_EXTENDED);
	// > Code 'SALT' salt
	CompileRegex(&gSaltRegex, "^([0-9]{3})\\s+([A-Za-z]+)\\s+(.+)$", REG_EXTENDED);
	// > Code 'User' Username Balance Flags
	CompileRegex(&gUserInfoRegex, "^([0-9]{3})\\s+([A-Za-z]+)\\s+([^ ]+)\\s+(-?[0-9]+)\\s+(.+)$", REG_EXTENDED);

	// Parse Arguments
	for( i = 1; i < argc; i ++ )
	{
		char	*arg = argv[i];
		
		if( arg[0] == '-' )
		{			
			switch(arg[1])
			{
			case 'h':
			case '?':
				ShowUsage();
				return 0;
			
			case 'u':	// Override User
				gsOverrideUser = argv[++i];
				break;
			
			case 'G':	// Use GUI
				gbUseNCurses = 1;
				break;
			}

			continue;
		}
		
		if( strcmp(arg, "acct") == 0 )
		{

			// Connect to server
			sock = OpenConnection(gsDispenseServer, giDispensePort);
			if( sock < 0 )	return -1;

			// List accounts?
			if( i + 1 == argc ) {
				Dispense_EnumUsers(sock);
				return 0;
			}
			
			// argv[i+1]: Username
			
			// Alter account?
			if( i + 2 < argc ) {
				
				if( i + 3 >= argc ) {
					fprintf(stderr, "Error: `dispense acct' needs a reason\n");
					exit(1);
				}
				
				// Authentication required
				Authenticate(sock);
				
				// argv[i+1]: Username
				// argv[i+2]: Ammount
				// argv[i+3]: Reason
				
				if( argv[i+2][0] == '=' ) {
					// Set balance
					Dispense_SetBalance(sock, argv[i+1], atoi(argv[i+2] + 1), argv[i+3]);
				}
				else {
					// Alter balance
					Dispense_AlterBalance(sock, argv[i+1], atoi(argv[i+2]), argv[i+3]);
				}
			}
			
			Dispense_ShowUser(sock, argv[i+1]);
			
			close(sock);
			return 0;
		}
		else {
			// Item name / pattern
			break;
		}
	}
	
	// Connect to server
	sock = OpenConnection(gsDispenseServer, giDispensePort);
	if( sock < 0 )	return -1;
	
	// Authenticate
	Authenticate(sock);

	// Get items
	PopulateItemList(sock);
	
	if( gbUseNCurses )
	{
		i = ShowNCursesUI();
	}
	else
	{
		for( i = 0; i < giNumItems; i ++ ) {		
			printf("%2i %s\t%3i %s\n", i, gaItems[i].Ident, gaItems[i].Price, gaItems[i].Desc);
		}
		printf(" q Quit\n");
		for(;;)
		{
			char	*buf;
			
			i = -1;
			
			fgets(buffer, BUFSIZ, stdin);
			
			buf = trim(buffer);
			
			if( buf[0] == 'q' )	break;
			
			i = atoi(buf);
			
			if( i != 0 || buf[0] == '0' )
			{
				if( i < 0 || i >= giNumItems ) {
					printf("Bad item %i (should be between 0 and %i)\n", i, giNumItems);
					continue;
				}
				break;
			}
		}
	}
	
	// Check for a valid item ID
	if( i >= 0 )
		DispenseItem(sock, i);

	close(sock);

	return 0;
}

void ShowUsage(void)
{
	printf(
		"Usage:\n"
		"\tdispense\n"
		"\t\tShow interactive list\n"
		"\tdispense <item>\n"
		"\t\tDispense named item\n"
		"\tdispense give <user> <ammount> \"<reason>\"\n"
		"\t\tGive some of your money away\n"
		"\tdispense acct [<user>]\n"
		"\t\tShow user balances\n"
		"\tdispense acct <user> [+-=]<ammount> \"<reason>\"\n"
		"\t\tAlter a account value (Coke members only)\n"
		"\n"
		"General Options:\n"
		"\t-u <username>\n"
		"\t\tSet a different user (Coke members only)\n"
		"\t-h / -?\n"
		"\t\tShow help text\n"
		"\t-G\n"
		"\t\tUse alternate GUI\n"
		);
}

// -------------------
// --- NCurses GUI ---
// -------------------
/**
 * \brief Render the NCurses UI
 */
int ShowNCursesUI(void)
{
	// TODO: ncurses interface (with separation between item classes)
	// - Hmm... that would require standardising the item ID to be <class>:<index>
	// Oh, why not :)
	 int	ch;
	 int	i, times;
	 int	xBase, yBase;
	const int	displayMinWidth = 40;
	const int	displayMinItems = 8;
	char	*titleString = "Dispense";
	 int	itemCount = displayMinItems;
	 int	itemBase = 0;
	 int	currentItem = 0;
	 int	ret = -2;	// -2: Used for marking "no return yet"
	 
	 int	height, width;
	 
	// Enter curses mode
	initscr();
	raw(); noecho();
	
	// Get item count
	// - 6: randomly chosen (Need at least 3)
	itemCount = LINES - 6;
	if( itemCount > giNumItems )
		itemCount = giNumItems;
	
	// Get dimensions
	height = itemCount + 3;
	width = displayMinWidth;
	
	// Get positions
	xBase = COLS/2 - width/2;
	yBase = LINES/2 - height/2;
	
	for( ;; )
	{
		// Header
		PrintAlign(yBase, xBase, width, "/", '-', titleString, '-', "\\");
		
		// Items
		for( i = 0; i < itemCount; i ++ )
		{
			move( yBase + 1 + i, xBase );
			
			if( currentItem == itemBase + i ) {
				printw("| -> ");
			}
			else {
				printw("|    ");
			}
			
			// Check for ... row
			// - Oh god, magic numbers!
			if( i == 0 && itemBase > 0 ) {
				printw("   ...");
				times = width-1 - 8 - 3;
				while(times--)	addch(' ');
			}
			else if( i == itemCount - 1 && itemBase < giNumItems - itemCount ) {
				printw("   ...");
				times = width-1 - 8 - 3;
				while(times--)	addch(' ');
			}
			// Show an item
			else {
				ShowItemAt( yBase + 1 + i, xBase + 5, width - 7, itemBase + i);
				addch(' ');
			}
			
			// Scrollbar (if needed)
			if( giNumItems > itemCount ) {
				if( i == 0 ) {
					addch('A');
				}
				else if( i == itemCount - 1 ) {
					addch('V');
				}
				else {
					 int	percentage = itemBase * 100 / (giNumItems-itemCount);
					if( i-1 == percentage*(itemCount-3)/100 ) {
						addch('#');
					}
					else {
						addch('|');
					}
				}
			}
			else {
				addch('|');
			}
		}
		
		// Footer
		PrintAlign(yBase+height-2, xBase, width, "\\", '-', "", '-', "/");
		
		// Get input
		ch = getch();
		
		if( ch == '\x1B' ) {
			ch = getch();
			if( ch == '[' ) {
				ch = getch();
				
				switch(ch)
				{
				case 'B':
					//if( itemBase < giNumItems - (itemCount) )
					//	itemBase ++;
					if( currentItem < giNumItems - 1 )
						currentItem ++;
					if( itemBase + itemCount - 1 <= currentItem && itemBase + itemCount < giNumItems )
						itemBase ++;
					break;
				case 'A':
					//if( itemBase > 0 )
					//	itemBase --;
					if( currentItem > 0 )
						currentItem --;
					if( itemBase + 1 > currentItem && itemBase > 0 )
						itemBase --;
					break;
				}
			}
			else {
				
			}
		}
		else {
			switch(ch)
			{
			case '\n':
				ret = currentItem;
				break;
			case 'q':
				ret = -1;	// -1: Return with no dispense
				break;
			}
			
			// Check if the return value was changed
			if( ret != -2 )	break;
		}
		
	}
	
	
	// Leave
	endwin();
	return ret;
}

/**
 * \brief Show item \a Index at (\a Col, \a Row)
 * \note Part of the NCurses UI
 */
void ShowItemAt(int Row, int Col, int Width, int Index)
{
	 int	_x, _y, times;
	char	*name;
	 int	price;
	
	move( Row, Col );
	
	if( Index < 0 || Index >= giNumItems ) {
		name = "OOR";
		price = 0;
	}
	else {
		name = gaItems[Index].Desc;
		price = gaItems[Index].Price;
	}

	printw("%02i %s", Index, name);
	
	getyx(stdscr, _y, _x);
	// Assumes max 4 digit prices
	times = Width - 4 - (_x - Col);	// TODO: Better handling for large prices
	while(times--)	addch(' ');
	printw("%4i", price);
}

/**
 * \brief Print a three-part string at the specified position (formatted)
 * \note NCurses UI Helper
 * 
 * Prints \a Left on the left of the area, \a Right on the righthand side
 * and \a Mid in the middle of the area. These are padded with \a Pad1
 * between \a Left and \a Mid, and \a Pad2 between \a Mid and \a Right.
 * 
 * ::printf style format codes are allowed in \a Left, \a Mid and \a Right,
 * and the arguments to these are read in that order.
 */
void PrintAlign(int Row, int Col, int Width, const char *Left, char Pad1,
	const char *Mid, char Pad2, const char *Right, ...)
{
	 int	lLen, mLen, rLen;
	 int	times;
	
	va_list	args;
	
	// Get the length of the strings
	va_start(args, Right);
	lLen = vsnprintf(NULL, 0, Left, args);
	mLen = vsnprintf(NULL, 0, Mid, args);
	rLen = vsnprintf(NULL, 0, Right, args);
	va_end(args);
	
	// Sanity check
	if( lLen + mLen/2 > Width/2 || mLen/2 + rLen > Width/2 ) {
		return ;	// TODO: What to do?
	}
	
	move(Row, Col);
	
	// Render strings
	va_start(args, Right);
	// - Left
	{
		char	tmp[lLen+1];
		vsnprintf(tmp, lLen+1, Left, args);
		addstr(tmp);
	}
	// - Left padding
	times = Width/2 - mLen/2 - lLen;
	while(times--)	addch(Pad1);
	// - Middle
	{
		char	tmp[mLen+1];
		vsnprintf(tmp, mLen+1, Mid, args);
		addstr(tmp);
	}
	// - Right Padding
	times = Width/2 - mLen/2 - rLen;
	while(times--)	addch(Pad2);
	// - Right
	{
		char	tmp[rLen+1];
		vsnprintf(tmp, rLen+1, Right, args);
		addstr(tmp);
	}
}

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
	
	#if USE_AUTOAUTH
	{
		struct sockaddr_in	localAddr;
		memset(&localAddr, 0, sizeof(localAddr));
		localAddr.sin_family = AF_INET;	// IPv4
		localAddr.sin_port = 1023;	// IPv4
		// Attempt to bind to low port for autoauth
		bind(sock, &localAddr, sizeof(localAddr));
	}
	#endif
	
	if( connect(sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0 ) {
		fprintf(stderr, "Failed to connect to server\n");
		return -1;
	}
	
	return sock;
}

/**
 * \brief Authenticate with the server
 * \return Boolean Failure
 */
int Authenticate(int Socket)
{
	struct passwd	*pwd;
	char	*buf;
	 int	responseCode;
	char	salt[32];
	 int	i;
	regmatch_t	matches[4];
	
	if( gbIsAuthenticated )	return 0;
	
	// Get user name
	pwd = getpwuid( getuid() );
	
	// Attempt automatic authentication
	sendf(Socket, "AUTOAUTH %s\n", pwd->pw_name);
	
	// Check if it worked
	buf = ReadLine(Socket);
	
	responseCode = atoi(buf);
	switch( responseCode )
	{
	
	case 200:	// Authenticated, return :)
		gbIsAuthenticated = 1;
		free(buf);
		return 0;
	
	case 401:	// Untrusted, attempt password authentication
		free(buf);
		
		sendf(Socket, "USER %s\n", pwd->pw_name);
		printf("Using username %s\n", pwd->pw_name);
		
		buf = ReadLine(Socket);
		
		// TODO: Get Salt
		// Expected format: 100 SALT <something> ...
		// OR             : 100 User Set
		RunRegex(&gSaltRegex, buf, 4, matches, "Malformed server response");
		responseCode = atoi(buf);
		if( responseCode != 100 ) {
			fprintf(stderr, "Unknown repsonse code %i from server\n%s\n", responseCode, buf);
			free(buf);
			return -1;	// ERROR
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
			 int	ofs = strlen(pwd->pw_name)+strlen(salt);
			char	tmpBuf[42];
			char	tmp[ofs+20];
			char	*pass = getpass("Password: ");
			uint8_t	h[20];
			
			// Create hash string
			// <username><salt><hash>
			strcpy(tmp, pwd->pw_name);
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
		if( i < 3 ) {
			gbIsAuthenticated = 1;
			return 0;
		}
		else
			return 2;	// 2 = Bad Password
	
	case 404:	// Bad Username
		fprintf(stderr, "Bad Username '%s'\n", pwd->pw_name);
		free(buf);
		return 1;
	
	default:
		fprintf(stderr, "Unkown response code %i from server\n", responseCode);
		printf("%s\n", buf);
		free(buf);
		return -1;
	}
}


/**
 * \brief Fill the item information structure
 * \return Boolean Failure
 */
void PopulateItemList(int Socket)
{
	char	*buf;
	 int	responseCode;
	
	char	*itemType, *itemStart;
	 int	count, i;
	regmatch_t	matches[4];
	
	// Ask server for stock list
	send(Socket, "ENUM_ITEMS\n", 11, 0);
	buf = ReadLine(Socket);
	
	//printf("Output: %s\n", buf);
	
	responseCode = atoi(buf);
	if( responseCode != 201 ) {
		fprintf(stderr, "Unknown response from dispense server (Response Code %i)\n", responseCode);
		exit(-1);
	}
	
	// - Get item list -
	
	// Expected format:
	//  201 Items <count>
	//  202 Item <count>
	RunRegex(&gArrayRegex, buf, 4, matches, "Malformed server response");
		
	itemType = &buf[ matches[2].rm_so ];	buf[ matches[2].rm_eo ] = '\0';
	count = atoi( &buf[ matches[3].rm_so ] );
		
	// Check array type
	if( strcmp(itemType, "Items") != 0 ) {
		// What the?!
		fprintf(stderr, "Unexpected array type, expected 'Items', got '%s'\n",
			itemType);
		exit(-1);
	}
		
	itemStart = &buf[ matches[3].rm_eo ];
	
	free(buf);
	
	giNumItems = count;
	gaItems = malloc( giNumItems * sizeof(tItem) );
	
	// Fetch item information
	for( i = 0; i < giNumItems; i ++ )
	{
		regmatch_t	matches[6];
		
		// Get item info
		buf = ReadLine(Socket);
		responseCode = atoi(buf);
		
		if( responseCode != 202 ) {
			fprintf(stderr, "Unknown response from dispense server (Response Code %i)\n", responseCode);
			exit(-1);
		}
		
		RunRegex(&gItemRegex, buf, 6, matches, "Malformed server response");
		
		buf[ matches[3].rm_eo ] = '\0';
		
		gaItems[i].Ident = strdup( buf + matches[3].rm_so );
		gaItems[i].Price = atoi( buf + matches[4].rm_so );
		gaItems[i].Desc = strdup( buf + matches[5].rm_so );
		
		free(buf);
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
 * \brief Dispense an item
 * \return Boolean Failure
 */
int DispenseItem(int Socket, int ItemID)
{
	 int	ret, responseCode;
	char	*buf;
	
	if( ItemID < 0 || ItemID > giNumItems )	return -1;
	
	// Dispense!
	sendf(Socket, "DISPENSE %s\n", gaItems[ItemID].Ident);
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
		ret = 1;
		break;
	case 402:
		printf("Insufficient balance\n");
		ret = 1;
		break;
	case 406:
		printf("Bad item name, bug report\n");
		ret = 1;
		break;
	case 500:
		printf("Item failed to dispense, is the slot empty?\n");
		ret = 1;
		break;
	case 501:
		printf("Dispense not possible (slot empty/permissions)\n");
		ret = 1;
		break;
	default:
		printf("Unknown response code %i ('%s')\n", responseCode, buf);
		ret = -2;
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
	 int	responseCode;
	
	sendf(Socket, "ADD %s %i %s\n", Username, Ammount, Reason);
	buf = ReadLine(Socket);
	
	responseCode = atoi(buf);
	free(buf);
	
	switch(responseCode)
	{
	case 200:	return 0;	// OK
	case 403:	// Not in coke
		fprintf(stderr, "You are not in coke (sucker)\n");
		return 1;
	case 404:	// Unknown user
		fprintf(stderr, "Unknown user '%s'\n", Username);
		return 2;
	default:
		fprintf(stderr, "Unknown response code %i\n", responseCode);
		return -1;
	}
	
	return -1;
}

/**
 * \brief Alter a user's balance
 */
int Dispense_SetBalance(int Socket, const char *Username, int Ammount, const char *Reason)
{
	char	*buf;
	 int	responseCode;
	
	sendf(Socket, "SET %s %i %s\n", Username, Ammount, Reason);
	buf = ReadLine(Socket);
	
	responseCode = atoi(buf);
	free(buf);
	
	switch(responseCode)
	{
	case 200:	return 0;	// OK
	case 403:	// Not in coke
		fprintf(stderr, "You are not in coke (sucker)\n");
		return 1;
	case 404:	// Unknown user
		fprintf(stderr, "Unknown user '%s'\n", Username);
		return 2;
	default:
		fprintf(stderr, "Unknown response code %i\n", responseCode);
		return -1;
	}
	
	return -1;
}

int Dispense_EnumUsers(int Socket)
{
	char	*buf;
	 int	responseCode;
	 int	nUsers;
	regmatch_t	matches[4];
	
	sendf(Socket, "ENUM_USERS\n");
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
		printf("%-15s: $%4i.%02i (%s)\n", username, bal/100, bal%100, flags);
	}
}

// ---------------
// --- Helpers ---
// ---------------
char *ReadLine(int Socket)
{
	static char	buf[BUFSIZ];
	static int	bufPos = 0;
	static int	bufValid = 0;
	 int	len;
	char	*newline = NULL;
	 int	retLen = 0;
	char	*ret = malloc(10);
	
	#if DEBUG_TRACE_SERVER
	printf("ReadLine: ");
	#endif
	fflush(stdout);
	
	ret[0] = '\0';
	
	while( !newline )
	{
		if( bufValid ) {
			len = bufValid;
		}
		else {
			len = recv(Socket, buf+bufPos, BUFSIZ-1-bufPos, 0);
			buf[bufPos+len] = '\0';
		}
		
		newline = strchr( buf+bufPos, '\n' );
		if( newline ) {
			*newline = '\0';
		}
		
		retLen += strlen(buf+bufPos);
		ret = realloc(ret, retLen + 1);
		strcat( ret, buf+bufPos );
		
		if( newline ) {
			 int	newLen = newline - (buf+bufPos) + 1;
			bufValid = len - newLen;
			bufPos += newLen;
		}
		if( len + bufPos == BUFSIZ - 1 )	bufPos = 0;
	}
	
	#if DEBUG_TRACE_SERVER
	printf("%i '%s'\n", retLen, ret);
	#endif
	
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

char *trim(char *string)
{
	 int	i;
	
	while( isspace(*string) )
		string ++;
	
	for( i = strlen(string); i--; )
	{
		if( isspace(string[i]) )
			string[i] = '\0';
		else
			break;
	}
	
	return string;
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
