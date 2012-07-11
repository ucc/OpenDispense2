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
#include <unistd.h>	// close
#include <limits.h>	// INT_MIN/INT_MAX
#include "common.h"

#define	USE_NCURSES_INTERFACE	0
#define DEBUG_TRACE_SERVER	0
#define USE_AUTOAUTH	1

#define MAX_TXT_ARGS	5	// Maximum number of textual arguments (including command)
#define DISPENSE_MULTIPLE_MAX	20	// Maximum argument to -c

// === TYPES ===

// === PROTOTYPES ===
void	ShowUsage(void);
 int	main(int argc, char *argv[]);
// --- Coke Server Communication ---
// --- Helpers ---
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

enum eUI_Modes	giUIMode = UI_MODE_STANDARD;
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
			"    dispense finger\n"
			"        Show the finger output\n"
			);
	if( giTextArgc == 0 || strcmp(gsTextArgs[0], "give") == 0 )
		printf(
			"    dispense give <user> <amount> \"<reason>\"\n"
			"        Give money to another user\n"
			);
	
	if( giTextArgc == 0 || strcmp(gsTextArgs[0], "donate") == 0 )
		printf(
			"    dispense donate <amount> \"<reason>\"\n"
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
			"    dispense acct <user> [+-]<amount> \"<reason>\"\n"
			"        Alter a account value\n"
			"    dispense acct <user> =<amount> \"<reason>\"\n"
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
				if( i > 2 && strcmp(argv[i-1], "type") == 0 )
					goto _default;
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
			default: _default:
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
	// `dispense finger`
	// -
	if( strcmp(gsTextArgs[0], "finger") == 0 )
	{
		// Connect to server
		sock = OpenConnection(gsDispenseServer, giDispensePort);
		if( sock < 0 )	return RV_SOCKET_ERROR;

		// Get items
		PopulateItemList(sock);

		printf("The UCC Coke machine.\n\n");

		// Only get coke slot statuses
		for( i = 0; i <= 6; i ++ )
		{
			const char *status;
			switch(gaItems[i].Status)
			{
			case 0:	status = "Avail";	break;
			case 1:	status = "Sold ";	break;
			default:
				status = "Error";
				break;
			}
			printf("%i - %s %3i %s\n", gaItems[i].ID, status, gaItems[i].Price, gaItems[i].Desc);
			
		}

		printf("\nMay your pink fish bing into the distance.\n");

		return 0;
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

// ---------------
// --- Helpers ---
// ---------------
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
