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

#include <unistd.h>	// close
#include <netdb.h>	// gethostbyname
#include <pwd.h>	// getpwuids
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// === TYPES ===
typedef struct sItem {
	char	*Ident;
	char	*Desc;
	 int	Price;
}	tItem;

// === PROTOTYPES ===
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
regex_t	gArrayRegex;
regex_t	gItemRegex;

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
	
	Authenticate(sock);
	
	// and choose what to dispense
	// TODO: ncurses interface (with separation between item classes)
	// - Hmm... that would require standardising the item ID to be <class>:<index>
	// Oh, why not :)
	
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
			
			break;
		}
	}

	close(sock);

	return 0;
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
