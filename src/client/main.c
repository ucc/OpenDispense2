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
#include <limits.h>

#include <unistd.h>	// close
#include <netdb.h>	// gethostbyname
#include <pwd.h>	// getpwuids
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
//#include <openssl/sha.h>	// SHA1

#define	USE_NCURSES_INTERFACE	0
#define DEBUG_TRACE_SERVER	0
#define USE_AUTOAUTH	1

#define MAX_TXT_ARGS	5	// Maximum number of textual arguments (including command)
#define DISPENSE_MULTIPLE_MAX	20	// Maximum argument to -c

enum eUI_Modes
{
	UI_MODE_BASIC,	// Non-NCurses
	UI_MODE_STANDARD,
	UI_MODE_DRINKSONLY,
	UI_MODE_ALL,
	NUM_UI_MODES
};

enum eReturnValues
{
	RV_SUCCESS,
	RV_BAD_ITEM,
	RV_INVALID_USER,
	RV_PERMISSIONS,
	RV_ARGUMENTS,
	RV_BALANCE,
	RV_SERVER_ERROR,	// Generic for 5xx codes
	RV_UNKNOWN_ERROR = -1,
	RV_SOCKET_ERROR = -2,
	RV_UNKNOWN_RESPONSE = -3,
};

// === TYPES ===
typedef struct sItem {
	char	*Type;
	 int	ID;
	 int	Status;	// 0: Availiable, 1: Sold out, -1: Error
	char	*Desc;
	 int	Price;
}	tItem;

// === PROTOTYPES ===
void	ShowUsage(void);
 int	main(int argc, char *argv[]);
// --- GUI ---
 int	ShowNCursesUI(void);
 int	ShowItemAt(int Row, int Col, int Width, int Index, int bHilighted);
void	PrintAlign(int Row, int Col, int Width, const char *Left, char Pad1, const char *Mid, char Pad2, const char *Right, ...);
// --- Coke Server Communication ---
 int	OpenConnection(const char *Host, int Port);
 int	Authenticate(int Socket);
 int	GetUserBalance(int Socket);
void	PopulateItemList(int Socket);
 int	Dispense_ItemInfo(int Socket, const char *Type, int ID);
 int	DispenseItem(int Socket, const char *Type, int ID);
 int	Dispense_AlterBalance(int Socket, const char *Username, int Ammount, const char *Reason);
 int	Dispense_SetBalance(int Socket, const char *Username, int Balance, const char *Reason);
 int	Dispense_Give(int Socket, const char *Username, int Ammount, const char *Reason);
 int	Dispense_Refund(int Socket, const char *Username, const char *Item, int PriceOverride);
 int	Dispense_Donate(int Socket, int Ammount, const char *Reason);
 int	Dispense_EnumUsers(int Socket);
 int	Dispense_ShowUser(int Socket, const char *Username);
void	_PrintUserLine(const char *Line);
 int	Dispense_AddUser(int Socket, const char *Username);
 int	Dispense_SetUserType(int Socket, const char *Username, const char *TypeString, const char *Reason);
 int	Dispense_SetItem(int Socket, const char *Type, int ID, int NewPrice, const char *NewName);
// --- Helpers ---
char	*ReadLine(int Socket);
 int	sendf(int Socket, const char *Format, ...);
char	*trim(char *string);
 int	RunRegex(regex_t *regex, const char *string, int nMatches, regmatch_t *matches, const char *errorMessage);
void	CompileRegex(regex_t *regex, const char *pattern, int flags);

// === GLOBALS ===
char	*gsDispenseServer = "merlo.ucc.gu.uwa.edu.au";
 int	giDispensePort = 11020;

tItem	*gaItems;
 int	giNumItems;
regex_t	gArrayRegex, gItemRegex, gSaltRegex, gUserInfoRegex, gUserItemIdentRegex;
 int	gbIsAuthenticated = 0;

char	*gsItemPattern;	//!< Item pattern
char	*gsEffectiveUser;	//!< '-u' Dispense as another user

 int	giUIMode = UI_MODE_STANDARD;
 int	gbDryRun = 0;	//!< '-n' Read-only
 int	gbDisallowSelectWithoutBalance = 1;	//!< Don't allow items to be hilighted if not affordable

 int	giMinimumBalance = INT_MIN;	//!< '-m' Minumum balance for `dispense acct`
 int	giMaximumBalance = INT_MAX;	//!< '-M' Maximum balance for `dispense acct`

 char	*gsUserName;	//!< User that dispense will happen as
char	*gsUserFlags;	//!< User's flag set
 int	giUserBalance = -1;	//!< User balance (set by Authenticate)
 int	giDispenseCount = 1;	//!< Number of dispenses to do

char	*gsTextArgs[MAX_TXT_ARGS];
 int	giTextArgc;

// === CODE ===
void ShowUsage(void)
{
	printf(	"Usage:\n" );
	if( giTextArgc == 0 )
		printf(
			"  == Everyone ==\n"
			"    dispense\n"
			"        Show interactive list\n"
			"    dispense <name>|<index>|<itemid>\n"
			"        Dispense named item (<name> matches if it is a unique prefix)\n"
			);
	if( giTextArgc == 0 || strcmp(gsTextArgs[0], "give") == 0 )
		printf(
			"    dispense give <user> <ammount> \"<reason>\"\n"
			"        Give money to another user\n"
			);
	
	if( giTextArgc == 0 || strcmp(gsTextArgs[0], "donate") == 0 )
		printf(
			"    dispense donate <ammount> \"<reason>\"\n"
			"        Donate to the club\n"
			);
	if( giTextArgc == 0 || strcmp(gsTextArgs[0], "iteminfo") == 0 )
		printf(
			"    dispense iteminfo <itemid>\n"
			"        Get the name and price for an item\n"
			);
//	if( giTextArgc == 0 || strcmp(gsTextArgs[0], "enumitems") == 0 )
//		printf(
//			"    dispense enumitems\n"
//			"        List avaliable items\n"
//			);
	if( giTextArgc == 0 )
		printf("  == Coke members == \n");
	if( giTextArgc == 0 || strcmp(gsTextArgs[0], "acct") == 0 )
		printf(
			"    dispense acct [<user>]\n"
			"        Show user balances\n"
			"    dispense acct <user> [+-]<ammount> \"<reason>\"\n"
			"        Alter a account value\n"
			"    dispense acct <user> =<ammount> \"<reason>\"\n"
			"        Set an account balance\n"
			);
	if( giTextArgc == 0 || strcmp(gsTextArgs[0], "refund") == 0 )
		printf(
			"    dispense refund <user> <itemid> [<price>]\n"
			"        Refund an item to a user (with optional price override)\n"
			"        Item IDs can be seen in the cokelog (in the brackets after the item name)\n"
			"        e.g. coke:6 for a coke, snack:33 for slot 33 of the snack machine\n"
			);
	if( giTextArgc == 0 || strcmp(gsTextArgs[0], "slot") == 0 )
		printf(
			"    dispense slot <itemid> <price> <name>\n"
			"        Rename/Re-price a slot\n"
			);
	if( giTextArgc == 0 )
		printf("  == Dispense administrators ==\n");
	if( giTextArgc == 0 || strcmp(gsTextArgs[0], "user") == 0 )
		printf(
			"    dispense user add <user>\n"
			"        Create new account\n"
			"    dispense user type <user> <flags> <reason>\n"
			"        Alter a user's flags\n"
			"        <flags> is a comma-separated list of user, coke, admin, internal or disabled\n"
			"        Flags are removed by preceding the name with '-' or '!'\n"
			);
	if( giTextArgc == 0 )
		printf(	"\n"
			"General Options:\n"
			"    -c <count>\n"
			"        Dispense multiple times\n"
			"    -u <username>\n"
			"        Set a different user (Coke members only)\n"
			"    -h / -?\n"
			"        Show help text\n"
			"    -G\n"
			"        Use simple textual interface (instead of ncurses)\n"
			"    -n\n"
			"        Dry run - Do not actually do dispenses\n"
			"    -m <min balance>\n"
			"    -M <max balance>\n"
			"        Set the Maximum/Minimum balances shown in `dispense acct`\n"
			"Definitions:\n"
			"    <itemid>\n"
			"        Item ID of the form <type>:<num> where <type> is a non-empty string of alpha-numeric characters, and <num> is a non-negative integer\n"
//			"    <user>\n"
//			"        Account name\n"
			);
}

int main(int argc, char *argv[])
{
	 int	sock;
	 int	i, ret = 0;
	char	buffer[BUFSIZ];
	
	gsTextArgs[0] = "";

	// -- Create regular expressions
	// > Code Type Count ...
	CompileRegex(&gArrayRegex, "^([0-9]{3})\\s+([A-Za-z]+)\\s+([0-9]+)", REG_EXTENDED);	//
	// > Code Type Ident Status Price Desc
	CompileRegex(&gItemRegex, "^([0-9]{3})\\s+([A-Za-z]+)\\s+([A-Za-z]+):([0-9]+)\\s+(avail|sold|error)\\s+([0-9]+)\\s+(.+)$", REG_EXTENDED);
	// > Code 'SALT' salt
	CompileRegex(&gSaltRegex, "^([0-9]{3})\\s+([A-Za-z]+)\\s+(.+)$", REG_EXTENDED);
	// > Code 'User' Username Balance Flags
	CompileRegex(&gUserInfoRegex, "^([0-9]{3})\\s+([A-Za-z]+)\\s+([^ ]+)\\s+(-?[0-9]+)\\s+(.+)$", REG_EXTENDED);
	// > Item Ident
	CompileRegex(&gUserItemIdentRegex, "^([A-Za-z]+):([0-9]+)$", REG_EXTENDED);

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
					
			case 'c':
				if( i + 1 >= argc ) {
					fprintf(stderr, "%s: -c takes an argument\n", argv[0]);
					ShowUsage();
					return -1;
				}
				giDispenseCount = atoi(argv[++i]);
				if( giDispenseCount < 1 || giDispenseCount > DISPENSE_MULTIPLE_MAX ) {
					fprintf(stderr, "Sorry, only 1-20 can be passed to -c (safety)\n");
					return -1;
				}
				
				break ;
	
			case 'm':	// Minimum balance
				if( i + 1 >= argc ) {
					fprintf(stderr, "%s: -m takes an argument\n", argv[0]);
					ShowUsage();
					return RV_ARGUMENTS;
				}
				giMinimumBalance = atoi(argv[++i]);
				break;
			case 'M':	// Maximum balance
				if( i + 1 >= argc ) {
					fprintf(stderr, "%s: -M takes an argument\n", argv[0]);
					ShowUsage();
					return RV_ARGUMENTS;
				}
				giMaximumBalance = atoi(argv[++i]);
				break;
			
			case 'u':	// Override User
				if( i + 1 >= argc ) {
					fprintf(stderr, "%s: -u takes an argument\n", argv[0]);
					ShowUsage();
					return RV_ARGUMENTS;
				}
				gsEffectiveUser = argv[++i];
				break;
			
			case 'H':	// Override remote host
				if( i + 1 >= argc ) {
					fprintf(stderr, "%s: -H takes an argument\n", argv[0]);
					ShowUsage();
					return RV_ARGUMENTS;
				}
				gsDispenseServer = argv[++i];
				break;
			case 'P':	// Override remote port
				if( i + 1 >= argc ) {
					fprintf(stderr, "%s: -P takes an argument\n", argv[0]);
					ShowUsage();
					return RV_ARGUMENTS;
				}
				giDispensePort = atoi(argv[++i]);
				break;
			
			// Set slot name/price
			case 's':
				if( giTextArgc != 0 ) {
					fprintf(stderr, "%s: -s must appear before other arguments\n", argv[0]);
					ShowUsage();
					return RV_ARGUMENTS;
				}
				gsTextArgs[0] = "slot";	// HACK!!
				giTextArgc ++;
				break;
			
			case 'G':	// Don't use GUI
				giUIMode = UI_MODE_BASIC;
				break;
			case 'D':	// Drinks only
				giUIMode = UI_MODE_DRINKSONLY;
				break;
			case 'n':	// Dry Run / read-only
				gbDryRun = 1;
				break;
			case '-':
				if( strcmp(argv[i], "--help") == 0 ) {
					ShowUsage();
					return 0;
				}
				else if( strcmp(argv[i], "--dry-run") == 0 ) {
					gbDryRun = 1;
				}
				else if( strcmp(argv[i], "--drinks-only") == 0 ) {
					giUIMode = UI_MODE_DRINKSONLY;
				}
				else if( strcmp(argv[i], "--can-select-all") == 0 ) {
					gbDisallowSelectWithoutBalance = 0;
				}
				else {
					fprintf(stderr, "%s: Unknown switch '%s'\n", argv[0], argv[i]);
					ShowUsage();
					return RV_ARGUMENTS;
				}
				break;
			default:
				// The first argument is not allowed to begin with 'i'
				// (catches most bad flags)
				if( giTextArgc == 0 ) {
					fprintf(stderr, "%s: Unknown switch '%s'\n", argv[0], argv[i]);
					ShowUsage();
					return RV_ARGUMENTS;
				}
				if( giTextArgc == MAX_TXT_ARGS )
				{
					fprintf(stderr, "ERROR: Too many arguments\n");
					return RV_ARGUMENTS;
				}
				gsTextArgs[giTextArgc++] = argv[i];
				break;
			}

			continue;
		}

		if( giTextArgc == MAX_TXT_ARGS )
		{
			fprintf(stderr, "ERROR: Too many arguments\n");
			return RV_ARGUMENTS;
		}
	
		gsTextArgs[giTextArgc++] = argv[i];
	
	}

	//
	// `dispense acct`
	// - 
	if( strcmp(gsTextArgs[0], "acct") == 0 )
	{
		// Connect to server
		sock = OpenConnection(gsDispenseServer, giDispensePort);
		if( sock < 0 )	return RV_SOCKET_ERROR;
		// List accounts?
		if( giTextArgc == 1 ) {
			ret = Dispense_EnumUsers(sock);
			close(sock);
			return ret;
		}
			
		// gsTextArgs[1]: Username
		
		// Alter account?
		if( giTextArgc != 2 )
		{
			if( giTextArgc != 4 ) {
				fprintf(stderr, "`dispense acct` requires a reason\n");
				ShowUsage();
				return RV_ARGUMENTS;
			}
			
			// Authentication required
			ret = Authenticate(sock);
			if(ret)	return ret;
			
			// gsTextArgs[1]: Username
			// gsTextArgs[2]: Ammount
			// gsTextArgs[3]: Reason
			
			if( gsTextArgs[2][0] == '=' ) {
				// Set balance
				if( gsTextArgs[2][1] != '0' && atoi(gsTextArgs[2]+1) == 0 ) {
					fprintf(stderr, "Error: Invalid balance to be set\n");
					exit(1);
				}
				
				ret = Dispense_SetBalance(sock, gsTextArgs[1], atoi(gsTextArgs[2]+1), gsTextArgs[3]);
			}
			else {
				// Alter balance
				ret = Dispense_AlterBalance(sock, gsTextArgs[1], atoi(gsTextArgs[2]), gsTextArgs[3]);
			}
		}
		// On error, quit
		if( ret ) {
			close(sock);
			return ret;
		}
		
		// Show user information
		ret = Dispense_ShowUser(sock, gsTextArgs[1]);
		
		close(sock);
		return ret;
	}
	//
	// `dispense give`
	// - "Here, have some money."
	else if( strcmp(gsTextArgs[0], "give") == 0 )
	{
		if( giTextArgc != 4 ) {
			fprintf(stderr, "`dispense give` takes three arguments\n");
			ShowUsage();
			return RV_ARGUMENTS;
		}
		
		// gsTextArgs[1]: Destination
		// gsTextArgs[2]: Ammount
		// gsTextArgs[3]: Reason
		
		// Connect to server
		sock = OpenConnection(gsDispenseServer, giDispensePort);
		if( sock < 0 )	return RV_SOCKET_ERROR;
		
		// Authenticate
		ret = Authenticate(sock);
		if(ret)	return ret;
		
		ret = Dispense_Give(sock, gsTextArgs[1], atoi(gsTextArgs[2]), gsTextArgs[3]);

		close(sock);
	
		return ret;
	}
	// 
	// `dispense user`
	// - User administration (Admin Only)
	if( strcmp(gsTextArgs[0], "user") == 0 )
	{
		// Check argument count
		if( giTextArgc == 1 ) {
			fprintf(stderr, "Error: `dispense user` requires arguments\n");
			ShowUsage();
			return RV_ARGUMENTS;
		}
		
		// Connect to server
		sock = OpenConnection(gsDispenseServer, giDispensePort);
		if( sock < 0 )	return RV_SOCKET_ERROR;
		
		// Attempt authentication
		ret = Authenticate(sock);
		if(ret)	return ret;
		
		// Add new user?
		if( strcmp(gsTextArgs[1], "add") == 0 )
		{
			if( giTextArgc != 3 ) {
				fprintf(stderr, "Error: `dispense user add` requires an argument\n");
				ShowUsage();
				return RV_ARGUMENTS;
			}
			
			ret = Dispense_AddUser(sock, gsTextArgs[2]);
		}
		// Update a user
		else if( strcmp(gsTextArgs[1], "type") == 0 || strcmp(gsTextArgs[1], "flags") == 0 )
		{
			if( giTextArgc < 4 || giTextArgc > 5 ) {
				fprintf(stderr, "Error: `dispense user type` requires two arguments\n");
				ShowUsage();
				return RV_ARGUMENTS;
			}
			
			if( giTextArgc == 4 )
				ret = Dispense_SetUserType(sock, gsTextArgs[2], gsTextArgs[3], "");
			else
				ret = Dispense_SetUserType(sock, gsTextArgs[2], gsTextArgs[3], gsTextArgs[4]);
		}
		else
		{
			fprintf(stderr, "Error: Unknown sub-command for `dispense user`\n");
			ShowUsage();
			return RV_ARGUMENTS;
		}
		close(sock);
		return ret;
	}
	// Donation!
	else if( strcmp(gsTextArgs[0], "donate") == 0 )
	{
		// Check argument count
		if( giTextArgc != 3 ) {
			fprintf(stderr, "Error: `dispense donate` requires two arguments\n");
			ShowUsage();
			return RV_ARGUMENTS;
		}
		
		// Connect to server
		sock = OpenConnection(gsDispenseServer, giDispensePort);
		if( sock < 0 )	return RV_SOCKET_ERROR;
		
		// Attempt authentication
		ret = Authenticate(sock);
		if(ret)	return ret;
		
		// Do donation
		ret = Dispense_Donate(sock, atoi(gsTextArgs[1]), gsTextArgs[2]);
				
		close(sock);

		return ret;
	}
	// Refund an item
	else if( strcmp(gsTextArgs[0], "refund") == 0 )
	{
		 int	 price = 0;
		// Check argument count
		if( giTextArgc != 3 && giTextArgc != 4 ) {
			fprintf(stderr, "Error: `dispense refund` takes 2 or 3 arguments\n");
			ShowUsage();
			return RV_ARGUMENTS;
		}
	
		// Connect to server
		sock = OpenConnection(gsDispenseServer, giDispensePort);
		if(sock < 0)	return RV_SOCKET_ERROR;	

		// Attempt authentication
		ret = Authenticate(sock);
		if(ret)	return ret;

		if( giTextArgc == 4 ) {
			price = atoi(gsTextArgs[3]);
			if( price <= 0 ) {
				fprintf(stderr, "Error: Override price is invalid (should be > 0)\n");
				return RV_ARGUMENTS;
			}
		}

		// Username, Item, cost
		ret = Dispense_Refund(sock, gsTextArgs[1], gsTextArgs[2], price);

		// TODO: More
		close(sock);
		return ret;
	}
	// Query an item price
	else if( strcmp(gsTextArgs[0], "iteminfo") == 0 )
	{
		regmatch_t matches[3];
		char	*type;
		 int	id;
		// Check argument count
		if( giTextArgc != 2 ) {
			fprintf(stderr, "Error: `dispense iteminfo` requires an argument\n");
			ShowUsage();
			return RV_ARGUMENTS;
		}
		// Parse item ID
		if( RunRegex(&gUserItemIdentRegex, gsTextArgs[1], 3, matches, NULL) != 0 ) {
			fprintf(stderr, "Error: Invalid item ID passed (<type>:<id> expected)\n");
			return RV_ARGUMENTS;
		}
		type = gsTextArgs[1] + matches[1].rm_so;
		gsTextArgs[1][ matches[1].rm_eo ] = '\0';
		id = atoi( gsTextArgs[1] + matches[2].rm_so );

		sock = OpenConnection(gsDispenseServer, giDispensePort);
		if( sock < 0 )	return RV_SOCKET_ERROR;
		
		ret = Dispense_ItemInfo(sock, type, id);
		close(sock);
		return ret;
	}
	// Set slot
	else if( strcmp(gsTextArgs[0], "slot") == 0 )
	{
		regmatch_t matches[3];
		char	*item_type, *newname;
		 int	item_id, price;
		
		// Check arguments
		if( giTextArgc != 4 ) {
			fprintf(stderr, "Error: `dispense slot` takes three arguments\n");
			ShowUsage();
			return RV_ARGUMENTS;
		}
		
		// Parse arguments
		if( RunRegex(&gUserItemIdentRegex, gsTextArgs[1], 3, matches, NULL) != 0 ) {
			fprintf(stderr, "Error: Invalid item ID passed (<type>:<id> expected)\n");
			return RV_ARGUMENTS;
		}
		item_type = gsTextArgs[1] + matches[1].rm_so;
		gsTextArgs[1][ matches[1].rm_eo ] = '\0';
		item_id = atoi( gsTextArgs[1] + matches[2].rm_so );

		// - Price
		price = atoi( gsTextArgs[2] );
		if( price <= 0 && gsTextArgs[2][0] != '0' ) {
			fprintf(stderr, "Error: Invalid price passed (must be >= 0)\n");
			return RV_ARGUMENTS;
		}
		
		// - New name
		newname = gsTextArgs[3];
		// -- Sanity
		{
			char *pos;
			for( pos = newname; *pos; pos ++ )
			{
				if( !isalnum(*pos) && *pos != ' ' ) {
					fprintf(stderr, "Error: You should only have letters, numbers and spaces in an item name\n");
					return RV_ARGUMENTS;
				}
			}
		}
		
		// Connect & Authenticate
		sock = OpenConnection(gsDispenseServer, giDispensePort);
		if( sock < 0 )	return RV_SOCKET_ERROR;
		ret = Authenticate(sock);
		if(ret)	return ret;
		// Update the slot
		ret = Dispense_SetItem(sock, item_type, item_id, price, newname);
		
		close(sock);
		return ret;
	}
	// Item name / pattern
	else
	{
		gsItemPattern = gsTextArgs[0];
	}
	
	// Connect to server
	sock = OpenConnection(gsDispenseServer, giDispensePort);
	if( sock < 0 )	return RV_SOCKET_ERROR;

	// Get the user's balance
	ret = GetUserBalance(sock);
	if(ret)	return ret;

	// Get items
	PopulateItemList(sock);
	
	// Disconnect from server
	close(sock);
	
	if( gsItemPattern && gsItemPattern[0] )
	{
		regmatch_t matches[3];
		// Door (hard coded)
		if( strcmp(gsItemPattern, "door") == 0 )
		{
			// Connect, Authenticate, dispense and close
			sock = OpenConnection(gsDispenseServer, giDispensePort);
			if( sock < 0 )	return RV_SOCKET_ERROR;
			ret = Authenticate(sock);
			if(ret)	return ret;
			ret = DispenseItem(sock, "door", 0);
			close(sock);
			return ret;
		}
		// Item id (<type>:<num>)
		else if( RunRegex(&gUserItemIdentRegex, gsItemPattern, 3, matches, NULL) == 0 )
		{
			char	*ident;
			 int	id;
			
			// Get and finish ident
			ident = gsItemPattern + matches[1].rm_so;
			gsItemPattern[matches[1].rm_eo] = '\0';
			// Get ID
			id = atoi( gsItemPattern + matches[2].rm_so );
			
			// Connect, Authenticate, dispense and close
			sock = OpenConnection(gsDispenseServer, giDispensePort);
			if( sock < 0 )	return RV_SOCKET_ERROR;
			
			Dispense_ItemInfo(sock, ident, id);
			
			ret = Authenticate(sock);
			if(ret)	return ret;
			ret = DispenseItem(sock, ident, id);
			close(sock);
			return ret;
		}
		// Item number (6 = coke)
		else if( strcmp(gsItemPattern, "0") == 0 || atoi(gsItemPattern) > 0 )
		{
			i = atoi(gsItemPattern);
		}
		// Item prefix
		else
		{
			 int	j;
			 int	best = -1;
			for( i = 0; i < giNumItems; i ++ )
			{
				// Prefix match (with case-insensitive match)
				for( j = 0; gsItemPattern[j]; j ++ )
				{
					if( gaItems[i].Desc[j] == gsItemPattern[j] )
						continue;
					if( tolower(gaItems[i].Desc[j]) == tolower(gsItemPattern[j]) )
						continue;
					break;
				}
				// Check if the prefix matched
				if( gsItemPattern[j] != '\0' )
					continue;
				
				// Prefect match
				if( gaItems[i].Desc[j] == '\0' ) {
					best = i;
					break;
				}
				
				// Only one match allowed
				if( best == -1 ) {
					best = i;
				}
				else {
					// TODO: Allow ambiguous matches?
					// or just print a wanrning
					printf("Warning - Ambiguous pattern, stopping\n");
					return RV_BAD_ITEM;
				}
			}
			
			// Was a match found?
			if( best == -1 )
			{
				fprintf(stderr, "No item matches the passed string\n");
				return RV_BAD_ITEM;
			}
			
			i = best;
		}
	}
	else if( giUIMode != UI_MODE_BASIC )
	{
		i = ShowNCursesUI();
	}
	else
	{
		// Very basic dispense interface
		for( i = 0; i < giNumItems; i ++ ) {
			// Add a separator
			if( i && strcmp(gaItems[i].Type, gaItems[i-1].Type) != 0 )
				printf("   ---\n");
			
			printf("%2i %s:%i\t%3i %s\n", i, gaItems[i].Type, gaItems[i].ID,
				gaItems[i].Price, gaItems[i].Desc);
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
	{
		 int j;
		// Connect, Authenticate, dispense and close
		sock = OpenConnection(gsDispenseServer, giDispensePort);
		if( sock < 0 )	return RV_SOCKET_ERROR;
			
		ret = Dispense_ItemInfo(sock, gaItems[i].Type, gaItems[i].ID);
		if(ret)	return ret;
		
		ret = Authenticate(sock);
		if(ret)	return ret;
		
		for( j = 0; j < giDispenseCount; j ++ ) {
			ret = DispenseItem(sock, gaItems[i].Type, gaItems[i].ID);
			if( ret )	break;
		}
		if( j > 1 ) {
			printf("%i items dispensed\n", j);
		}
		Dispense_ShowUser(sock, gsUserName);
		close(sock);

	}

	return ret;
}

// -------------------
// --- NCurses GUI ---
// -------------------
/**
 * \brief Render the NCurses UI
 */
int ShowNCursesUI(void)
{
	 int	ch;
	 int	i, times;
	 int	xBase, yBase;
	const int	displayMinWidth = 40;
	char	*titleString = "Dispense";
	 int	items_in_view;
	 int	maxItemIndex;
	 int	itemBase = 0;
	 int	currentItem;
	 int	ret = -2;	// -2: Used for marking "no return yet"
	
	char	balance_str[5+1+2+1];	// If $9999.99 is too little, something's wrong
	char	*username;
	struct passwd *pwd;
	 
	 int	height, width;
	 
	// Get Username
	if( gsEffectiveUser )
		username = gsEffectiveUser;
	else {
		pwd = getpwuid( getuid() );
		username = pwd->pw_name;
	}
	// Get balance
	snprintf(balance_str, sizeof(balance_str), "$%i.%02i", giUserBalance/100, abs(giUserBalance)%100);
	
	// Enter curses mode
	initscr();
	cbreak(); noecho();
	
	// Get max index
	maxItemIndex = ShowItemAt(0, 0, 0, -1, 0);
	// Get item count per screen
	// - 6: randomly chosen (Need at least 3)
	items_in_view = LINES - 6;
	if( items_in_view > maxItemIndex )
		items_in_view = maxItemIndex;
	// Get first index
	currentItem = 0;
	while( ShowItemAt(0, 0, 0, currentItem, 0) == -1 )
		currentItem ++;
	
	
	// Get dimensions
	height = items_in_view + 3;
	width = displayMinWidth;
	
	// Get positions
	xBase = COLS/2 - width/2;
	yBase = LINES/2 - height/2;
	
	for( ;; )
	{
		// Header
		PrintAlign(yBase, xBase, width, "/", '-', titleString, '-', "\\");
		
		// Items
		for( i = 0; i < items_in_view; i ++ )
		{
			 int	pos = 0;
			
			move( yBase + 1 + i, xBase );
			printw("| ");
			
			pos += 2;
			
			// Check for the '...' row
			// - Oh god, magic numbers!
			if( (i == 0 && itemBase > 0)
			 || (i == items_in_view - 1 && itemBase < maxItemIndex - items_in_view) )
			{
				printw("     ...");	pos += 8;
				times = (width - pos) - 1;
				while(times--)	addch(' ');
			}
			// Show an item
			else {
				ShowItemAt(
					yBase + 1 + i, xBase + pos,	// Position
					(width - pos) - 3,	// Width
					itemBase + i,	// Index
					!!(currentItem == itemBase + i)	// Hilighted
					);
				printw("  ");
			}
			
			// Scrollbar (if needed)
			if( maxItemIndex > items_in_view ) {
				if( i == 0 ) {
					addch('A');
				}
				else if( i == items_in_view - 1 ) {
					addch('V');
				}
				else {
					 int	percentage = itemBase * 100 / (maxItemIndex-items_in_view);
					if( i-1 == percentage*(items_in_view-3)/100 ) {
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
		
		// User line
		// - Username, balance, flags
		PrintAlign(yBase+height-1, xBase+1, width-2,
			username, ' ', balance_str, ' ', gsUserFlags);
		
		
		// Get input
		ch = getch();
		
		if( ch == '\x1B' ) {
			ch = getch();
			if( ch == '[' ) {
				ch = getch();
				
				switch(ch)
				{
				case 'B':
					currentItem ++;
					// Skip over spacers
					while( ShowItemAt(0, 0, 0, currentItem, 0) == -1 )
						currentItem ++;
					
					if( currentItem >= maxItemIndex ) {
						currentItem = 0;
						// Skip over spacers
						while( ShowItemAt(0, 0, 0, currentItem, 0) == -1 )
							currentItem ++;
					}
					break;
				case 'A':
					currentItem --;
					// Skip over spacers
					while( ShowItemAt(0, 0, 0, currentItem, 0) == -1 )
						currentItem --;
					
					if( currentItem < 0 ) {
						currentItem = maxItemIndex - 1;
						// Skip over spacers
						while( ShowItemAt(0, 0, 0, currentItem, 0) == -1 )
							currentItem --;
					}
					break;
				}
			}
			else {
				
			}
		
			// Scroll only if needed
			if( items_in_view < maxItemIndex )
			{
				// - If the current item is above the second item shown, and we're not at the top
				if( currentItem < itemBase + 2 && itemBase > 0 ) {
					itemBase = currentItem - 2;
					if(itemBase < 0)	itemBase = 0;
				}
				// - If the current item is below the second item show, and we're not at the bottom
				if( currentItem > itemBase + items_in_view - 2 && itemBase + items_in_view < maxItemIndex ) {
					itemBase = currentItem - items_in_view + 2;
					if( itemBase > maxItemIndex - items_in_view )
						itemBase = maxItemIndex - items_in_view;
				}
			}
		}
		else {
			switch(ch)
			{
			case '\n':
				ret = ShowItemAt(0, 0, 0, currentItem, 0);
				break;
			case 0x1b:	// Escape
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
 * \return Dispense index of item
 * \note Part of the NCurses UI
 */
int ShowItemAt(int Row, int Col, int Width, int Index, int bHilighted)
{
	char	*name = NULL;
	 int	price = 0;
	 int	status = -1;
	
	switch(giUIMode)
	{
	// Standard UI
	// - This assumes that 
	case UI_MODE_STANDARD:
		// Bounds check
		// Index = -1, request limit
		if( Index < 0 || Index >= giNumItems+2 )
			return giNumItems+2;
		// Drink label
		if( Index == 0 )
		{
			price = 0;
			name = "Coke Machine";
			Index = -1;	// -1 indicates a label
			break;
		}
		Index --;
		// Drinks 0 - 6
		if( Index <= 6 )
		{
			name = gaItems[Index].Desc;
			price = gaItems[Index].Price;
			status = gaItems[Index].Status;
			break;
		}
		Index -= 7;
		// EPS label
		if( Index == 0 )
		{
			price = 0;
			name = "Electronic Payment System";
			Index = -1;	// -1 indicates a label
			break;
		}
		Index --;
		Index += 7;
		name = gaItems[Index].Desc;
		price = gaItems[Index].Price;
		status = gaItems[Index].Status;
		break;
	default:
		return -1;
	}
	
	// Width = 0, don't print
	if( Width > 0 )
	{
		// 4 preceding, 5 price
		int nameWidth = Width - 4 - 5;
		move( Row, Col );
		
		if( Index >= 0 )
		{
			// Show hilight and status
			switch( status )
			{
			case 0:
				if( bHilighted )
					printw("->  ");
				else
					printw("    ");
				break;
			case 1:
				printw("SLD ");
				break;
			
			default:
			case -1:
				printw("ERR ");
				break;
			}
			
			if( price > 100*100 ) {
				nameWidth --;
			}
			if( price > 1000*100 ) {
				nameWidth --;
			}
			
			printw("%-*.*s", nameWidth, nameWidth, name);
		
			// 99.99 should be enough
			printw(" %4i", price);
		}
		else
		{
			printw("-- %-*.*s ", Width-4, Width-4, name);
		}
	}
	
	// If the item isn't availiable for sale, return -1 (so it's skipped)
	if( status || (price > giUserBalance && gbDisallowSelectWithoutBalance) )
		Index = -1;
	
	return Index;
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
	times = (Width - mLen)/2 - lLen;
	while(times--)	addch(Pad1);
	// - Middle
	{
		char	tmp[mLen+1];
		vsnprintf(tmp, mLen+1, Mid, args);
		addstr(tmp);
	}
	// - Right Padding
	times = (Width - mLen)/2 - rLen;
	if( (Width - mLen) % 2 )	times ++;
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

//	printf("geteuid() = %i, getuid() = %i\n", geteuid(), getuid());
	
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
//		else
//			printf("Bound to 0.0.0.0:%i\n", i);
	}
	
	if( connect(sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0 ) {
		fprintf(stderr, "Failed to connect to server\n");
		return -1;
	}

	// We're not authenticated if the connection has just opened
	gbIsAuthenticated = 0;
	
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
	#if ATTEMPT_PASSWORD_AUTH
	char	salt[32];
	 int	i;
	regmatch_t	matches[4];
	#endif
	
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
	case 200:	// Autoauth succeeded, return
		free(buf);
		break;
	
	case 401:	// Untrusted, attempt password authentication
		free(buf);

		#if ATTEMPT_PASSWORD_AUTH	
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
			return RV_UNKNOWN_ERROR;
		}
		free(buf);
		if( i == 3 )
			return RV_INVALID_USER;	// 2 = Bad Password
		
		#else
		fprintf(stderr, "Untrusted host, AUTOAUTH unavaliable\n");
		return RV_INVALID_USER;
		#endif
		break;
	
	case 404:	// Bad Username
		fprintf(stderr, "Bad Username '%s'\n", pwd->pw_name);
		free(buf);
		return RV_INVALID_USER;
	
	default:
		fprintf(stderr, "Unkown response code %i from server\n", responseCode);
		printf("%s\n", buf);
		free(buf);
		return RV_UNKNOWN_ERROR;
	}
	
	// Set effective user
	if( gsEffectiveUser ) {
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
		printf("An ammount would be nice\n");
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
		fprintf(stderr, "You are not in coke (sucker)\n");
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
	case 403:	// Not in coke
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
	fflush(stdout);
	#endif
	
	ret[0] = '\0';
	
	while( !newline )
	{
		if( bufValid ) {
			len = bufValid;
		}
		else {
			len = recv(Socket, buf+bufPos, BUFSIZ-1-bufPos, 0);
			if( len <= 0 ) {
				free(ret);
				return strdup("599 Client Connection Error\n");
			}
		}
		buf[bufPos+len] = '\0';
		
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
	if( ret && errorMessage ) {
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
