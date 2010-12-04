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

// === TYPES ===
typedef struct sItem {
	char	*Ident;
	char	*Desc;
	 int	Price;
}	tItem;

// === PROTOTYPES ===
 int	ShowNCursesUI(void);
void	PrintAlign(int Row, int Col, int Width, const char *Left, char Pad1, const char *Mid, char Pad2, const char *Right, ...);

 int	sendf(int Socket, const char *Format, ...);
 int	OpenConnection(const char *Host, int Port);
void	Authenticate(int Socket);
char	*trim(char *string);
 int	RunRegex(regex_t *regex, const char *string, int nMatches, regmatch_t *matches, const char *errorMessage);
void	CompileRegex(regex_t *regex, const char *pattern, int flags);

// === GLOBALS ===
char	*gsDispenseServer = "localhost";
 int	giDispensePort = 11020;
tItem	*gaItems;
 int	giNumItems;
regex_t	gArrayRegex, gItemRegex, gSaltRegex;

// === CODE ===
int main(int argc, char *argv[])
{
	 int	sock;
	 int	i, responseCode, len;
	char	buffer[BUFSIZ];
	
	// -- Create regular expressions
	// > Code Type Count ...
	CompileRegex(&gArrayRegex, "^([0-9]{3})\\s+([A-Za-z]+)\\s+([0-9]+)", REG_EXTENDED);	//
	// > Code Type Ident Price Desc
	CompileRegex(&gItemRegex, "^([0-9]{3})\\s+(.+?)\\s+(.+?)\\s+([0-9]+)\\s+(.+)$", REG_EXTENDED);
	// > Code 'SALT' salt
	CompileRegex(&gSaltRegex, "^([0-9]{3})\\s+(.+)\\s+(.+)$", REG_EXTENDED);
	
	// Connect to server
	sock = OpenConnection(gsDispenseServer, giDispensePort);
	if( sock < 0 )	return -1;

	// Determine what to do
	if( argc > 1 )
	{
		if( strcmp(argv[1], "acct") == 0 )
		{
			// Alter account
			// List accounts
			return 0;
		}
	}

	// Ask server for stock list
	send(sock, "ENUM_ITEMS\n", 11, 0);
	len = recv(sock, buffer, BUFSIZ-1, 0);
	buffer[len] = '\0';
	
	trim(buffer);
	
	printf("Output: %s\n", buffer);
	
	responseCode = atoi(buffer);
	if( responseCode != 201 )
	{
		fprintf(stderr, "Unknown response from dispense server (Response Code %i)\n", responseCode);
		return -1;
	}
	
	// Get item list
	{
		char	*itemType, *itemStart;
		 int	count;
		regmatch_t	matches[4];
		
		// Expected format: 201 Items <count> <item1> <item2> ...
		RunRegex(&gArrayRegex, buffer, 4, matches, "Malformed server response");
		
		itemType = &buffer[ matches[2].rm_so ];	buffer[ matches[2].rm_eo ] = '\0';
		count = atoi( &buffer[ matches[3].rm_so ] );
		
		// Check array type
		if( strcmp(itemType, "Items") != 0 ) {
			// What the?!
			fprintf(stderr, "Unexpected array type, expected 'Items', got '%s'\n",
				itemType);
			return -1;
		}
		
		itemStart = &buffer[ matches[3].rm_eo ];
		
		gaItems = malloc( count * sizeof(tItem) );
		
		for( giNumItems = 0; giNumItems < count && itemStart; giNumItems ++ )
		{
			char	*next = strchr( ++itemStart, ' ' );
			if( next )	*next = '\0';
			gaItems[giNumItems].Ident = strdup(itemStart);
			itemStart = next;
		}
	}
	
	// Get item information
	for( i = 0; i < giNumItems; i ++ )
	{
		regmatch_t	matches[6];
		
		// Print item Ident
		printf("%2i %s\t", i, gaItems[i].Ident);
		
		// Get item info
		sendf(sock, "ITEM_INFO %s\n", gaItems[i].Ident);
		len = recv(sock, buffer, BUFSIZ-1, 0);
		buffer[len] = '\0';
		trim(buffer);
		
		responseCode = atoi(buffer);
		if( responseCode != 202 ) {
			fprintf(stderr, "Unknown response from dispense server (Response Code %i)\n", responseCode);
			return -1;
		}
		
		RunRegex(&gItemRegex, buffer, 6, matches, "Malformed server response");
		
		buffer[ matches[3].rm_eo ] = '\0';
		
		gaItems[i].Price = atoi( buffer + matches[4].rm_so );
		gaItems[i].Desc = strdup( buffer + matches[5].rm_so );
		
		printf("%3i %s\n", gaItems[i].Price, gaItems[i].Desc);
	}
	
	// and choose what to dispense
	// TODO: ncurses interface (with separation between item classes)
	// - Hmm... that would require standardising the item ID to be <class>:<index>
	// Oh, why not :)
	
	#if 1
	i = ShowNCursesUI();
	#else
	
	for(;;)
	{
		char	*buf;
		
		fgets(buffer, BUFSIZ, stdin);
		
		buf = trim(buffer);
		
		if( buf[0] == 'q' )	break;
		
		i = atoi(buf);
		
		printf("buf = '%s', atoi(buf) = %i\n", buf, i);
		
		if( i != 0 || buf[0] == '0' )
		{
			printf("i = %i\n", i);
			
			if( i < 0 || i >= giNumItems ) {
				printf("Bad item (should be between 0 and %i)\n", giNumItems);
				continue;
			}
			break;
		}
	}
	#endif
	
	Authenticate(sock);
	
	if( i >= 0 )
	{	
		// Dispense!
		sendf(sock, "DISPENSE %s\n", gaItems[i].Ident);
		
		len = recv(sock, buffer, BUFSIZ-1, 0);
		buffer[len] = '\0';
		trim(buffer);
		
		responseCode = atoi(buffer);
		switch( responseCode )
		{
		case 200:
			printf("Dispense OK\n");
			break;
		case 401:
			printf("Not authenticated\n");
			break;
		case 402:
			printf("Insufficient balance\n");
			break;
		case 406:
			printf("Bad item name, bug report\n");
			break;
		case 500:
			printf("Item failed to dispense, is the slot empty?\n");
			break;
		default:
			printf("Unknown response code %i\n", responseCode);
			break;
		}
	}

	close(sock);

	return 0;
}

void ShowItemAt(int Row, int Col, int Width, int Index)
{
	 int	_x, _y, times;
	
	move( Row, Col );
	
	if( Index < 0 || Index >= giNumItems ) {
		printw("%02i OOR", Index);
		return ;
	}
	printw("%02i %s", Index, gaItems[Index].Desc);
	
	getyx(stdscr, _y, _x);
	// Assumes max 4 digit prices
	times = Width - 4 - (_x - Col);	// TODO: Better handling for large prices
	while(times--)	addch(' ');
	printw("%4i", gaItems[Index].Price);
}

/**
 */
int ShowNCursesUI(void)
{
	 int	ch;
	 int	i, times;
	 int	xBase, yBase;
	const int	displayMinWidth = 34;
	const int	displayMinItems = 8;
	char	*titleString = "Dispense";
	 int	itemCount = displayMinItems;
	 int	itemBase = 0;
	 
	 int	height = itemCount + 3;
	 int	width = displayMinWidth;
	 
	// Enter curses mode
	initscr();
	raw(); noecho();
	
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
			addch('|');
			addch(' ');
			
			// Check for ... row
			if( i == 0 && itemBase > 0 ) {
				printw("   ...");
				times = width - 1 - 8;
				while(times--)	addch(' ');
			}
			else if( i == itemCount - 1 && itemBase < giNumItems - itemCount ) {
				printw("   ...");
				times = width - 1 - 8;
				while(times--)	addch(' ');
			}
			// Show an item
			else {
				ShowItemAt( yBase + 1 + i, xBase + 2, width - 4, itemBase + i);
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
					if( itemBase < giNumItems - (itemCount) )
						itemBase ++;
					break;
				case 'A':
					if( itemBase > 0 )
						itemBase --;
					break;
				}
			}
			else {
				
			}
		}
		else {
			break;
		}
		
	}
	
	
	// Leave
	endwin();
	return -1;
}

void PrintAlign(int Row, int Col, int Width, const char *Left, char Pad1, const char *Mid, char Pad2, const char *Right, ...)
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

// === HELPERS ===
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
		
		return send(Socket, buf, len, 0);
	}
}

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

void Authenticate(int Socket)
{
	struct passwd	*pwd;
	char	buf[512];
	 int	responseCode;
	char	salt[32];
	regmatch_t	matches[4];
	
	// Get user name
	pwd = getpwuid( getuid() );
	
	// Attempt automatic authentication
	sendf(Socket, "AUTOAUTH %s\n", pwd->pw_name);
	
	// Check if it worked
	recv(Socket, buf, 511, 0);
	trim(buf);
	
	responseCode = atoi(buf);
	switch( responseCode )
	{
	case 200:	// Authenticated, return :)
		return ;
	case 401:	// Untrusted, attempt password authentication
		sendf(Socket, "USER %s\n", pwd->pw_name);
		printf("Using username %s\n", pwd->pw_name);
		
		recv(Socket, buf, 511, 0);
		trim(buf);
		// TODO: Get Salt
		// Expected format: 100 SALT <something> ...
		// OR             : 100 User Set
		printf("string = '%s'\n", buf);
		RunRegex(&gSaltRegex, buf, 4, matches, "Malformed server response");
		if( atoi(buf) != 100 ) {
			exit(-1);	// ERROR
		}
		if( memcmp( buf+matches[2].rm_so, "SALT", matches[2].rm_eo - matches[2].rm_so) == 0) {
			// Set salt
			memcpy( salt, buf + matches[3].rm_so, matches[3].rm_eo - matches[3].rm_so );
			salt[ matches[3].rm_eo - matches[3].rm_so ] = 0;
			printf("Salt: '%s'\n", salt);
		}
		
		fflush(stdout);
		{
			 int	ofs = strlen(pwd->pw_name)+strlen(salt);
			char	tmp[ofs+20];
			char	*pass = getpass("Password: ");
			uint8_t	h[20];
			
			strcpy(tmp, pwd->pw_name);
			strcat(tmp, salt);
			SHA1( (unsigned char*)pass, strlen(pass), h );
			memcpy(tmp+ofs, h, 20);
			
			// Hash all that
			SHA1( (unsigned char*)tmp, ofs+20, h );
			sprintf(buf, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
				h[ 0], h[ 1], h[ 2], h[ 3], h[ 4], h[ 5], h[ 6], h[ 7], h[ 8], h[ 9],
				h[10], h[11], h[12], h[13], h[14], h[15], h[16], h[17], h[18], h[19]
				);
			printf("Final hash: '%s'\n", buf);
			fflush(stdout);	// Debug
		}
		
		sendf(Socket, "PASS %s\n", buf);
		recv(Socket, buf, 511, 0);
		break;
	case 404:	// Bad Username
		fprintf(stderr, "Bad Username '%s'\n", pwd->pw_name);
		exit(-1);
	default:
		fprintf(stderr, "Unkown response code %i from server\n", responseCode);
		printf("%s\n", buf);
		exit(-1);
	}
	
	printf("%s\n", buf);
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
