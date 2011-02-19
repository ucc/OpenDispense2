/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * SQLite Coke Bank (Accounts Database)
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
#include <inttypes.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../cokebank.h"
#include <sqlite3.h>

#define DEBUG	1

const char * const csBank_DatabaseSetup = 
"CREATE TABLE IF NOT EXISTS accounts ("
"	acct_id INTEGER PRIMARY KEY NOT NULL,"
"	acct_balance INTEGER NOT NULL DEFAULT 0,"
"	acct_last_seen DATETIME NOT NULL DEFAULT (datetime('now')),"
"	acct_name STRING UNIQUE,"
"	acct_uid INTEGER UNIQUE DEFAULT NULL,"
"	acct_pin INTEGER DEFAULT NULL," //"	acct_pin INTEGER CHECK (acct_pin > 0 AND acct_pin < 10000) DEFAULT NULL,"
"	acct_is_disabled BOOLEAN NOT NULL DEFAULT false,"
"	acct_is_coke BOOLEAN NOT NULL DEFAULT false,"
"	acct_is_admin BOOLEAN NOT NULL DEFAULT false,"
"	acct_is_door BOOLEAN NOT NULL DEFAULT false,"
"	acct_is_internal BOOLEAN NOT NULL DEFAULT false"
");"
"CREATE TABLE IF NOT EXISTS cards ("
"	acct_id INTEGER NOT NULL,"
"	card_name STRING NOT NULL UNIQUE,"
"	FOREIGN KEY (acct_id) REFERENCES accounts (acct_id) ON DELETE CASCADE"
//	                 Deletion of the account frees the card  ^ ^ ^
");"
"CREATE TABLE IF NOT EXISTS items ("
"	item_id INTEGER PRIMARY KEY NOT NULL,"
"	item_handler STRING NOT NULL,"
"	item_index INTEGER NOT NULL,"
"	item_name STRING NOT NULL,"
"	item_price INTEGER NOT NULL,"
"	item_is_enabled BOOLEAN NOT NULL DEFAULT true"
");"
"INSERT INTO accounts (acct_name,acct_is_admin,acct_uid) VALUES ('root',1,0);"
"INSERT INTO accounts (acct_name,acct_is_internal,acct_uid) VALUES ('"COKEBANK_SALES_ACCT"',1,-1);"
"INSERT INTO accounts (acct_name,acct_is_internal,acct_uid) VALUES ('"COKEBANK_DEBT_ACCT"',1,-2);"
"INSERT INTO accounts (acct_name,acct_is_internal,acct_uid) VALUES ('"COKEBANK_FREE_ACCT"',1,-3);"
;

// === TYPES ===
struct sAcctIterator	// Unused really, just used as a void type
{
};

// === PROTOYPES ===
 int	Bank_Initialise(const char *Argument);
 int	Bank_Transfer(int SourceAcct, int DestAcct, int Ammount, const char *Reason);
 int	Bank_GetFlags(int AcctID);
 int	Bank_SetFlags(int AcctID, int Mask, int Value);
 int	Bank_GetBalance(int AcctID);
char	*Bank_GetAcctName(int AcctID);
sqlite3_stmt	*Bank_int_MakeStatemnt(sqlite3 *Database, const char *Query);
sqlite3_stmt	*Bank_int_QuerySingle(sqlite3 *Database, const char *Query);
 int	Bank_int_IsValidName(const char *Name);

// === GLOBALS ===
sqlite3	*gBank_Database;

// === CODE ===
int Bank_Initialise(const char *Argument)
{
	 int	rv;
	char	*errmsg;
	// Open database
	rv = sqlite3_open(Argument, &gBank_Database);
	if(rv != 0)
	{
		fprintf(stderr, "CokeBank: Unable to open database '%s'\n", Argument);
		fprintf(stderr, "Reason: %s\n", sqlite3_errmsg(gBank_Database));
		sqlite3_close(gBank_Database);
		return 1;
	}

	// Check structure
	rv = sqlite3_exec(gBank_Database, "SELECT acct_id FROM accounts LIMIT 1", NULL, NULL, &errmsg);
	if( rv == SQLITE_OK )
	{
		// NOP
	}
	else if( rv == SQLITE_NOTFOUND || rv == SQLITE_ERROR )
	{
		sqlite3_free(errmsg);
		// Create tables
		rv = sqlite3_exec(gBank_Database, csBank_DatabaseSetup, NULL, NULL, &errmsg);
		if( rv != SQLITE_OK ) {
			fprintf(stderr, "Bank_Initialise - SQLite Error: %s\n", errmsg);
			sqlite3_free(errmsg);
			return 1;
		}
		
		Log_Info("SQLite database rebuilt");
	}
	else
	{
		// Unknown error
		fprintf(stderr, "Bank_Initialise - SQLite Error: %s (rv = %i)\n", errmsg, rv);
		sqlite3_free(errmsg);
		return 1;
	}

	return 0;
}

/*
 * Move Money
 */
int Bank_Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason)
{
	char	*query;
	 int	rv;
	char	*errmsg;
	
	Reason = "";	// Shut GCC up
	
	// Begin SQL Transaction
	sqlite3_exec(gBank_Database, "BEGIN TRANSACTION", NULL, NULL, NULL);

	// Take from the source
	query = mkstr("UPDATE accounts SET acct_balance=acct_balance-%i,acct_last_seen=datetime('now') WHERE acct_id=%i", Ammount, SourceUser);
	printf("query = \"%s\"\n", query);
	rv = sqlite3_exec(gBank_Database, query, NULL, NULL, &errmsg);
	free(query);
	if( rv != SQLITE_OK )
	{
		fprintf(stderr, "Bank_Transfer - SQLite Error: %s\n", errmsg);
		sqlite3_free(errmsg);
		sqlite3_exec(gBank_Database, "ROLLBACK", NULL, NULL, NULL);
		return 1;
	}

	// Give to the destination
	query = mkstr("UPDATE accounts SET acct_balance=acct_balance+%i,acct_last_seen=datetime('now') WHERE acct_id=%i", Ammount, DestUser);
	printf("query = \"%s\"\n", query);
	rv = sqlite3_exec(gBank_Database, query, NULL, NULL, &errmsg);
	free(query);
	if( rv != SQLITE_OK )
	{
		fprintf(stderr, "Bank_Transfer - SQLite Error: %s\n", errmsg);
		sqlite3_free(errmsg);
		sqlite3_exec(gBank_Database, "ROLLBACK", NULL, NULL, NULL);
		return 1;
	}

	// Commit transaction
	sqlite3_exec(gBank_Database, "COMMIT", NULL, NULL, NULL);

	return 0;
}

/*
 * Get user flags
 */
int Bank_GetFlags(int UserID)
{
	sqlite3_stmt	*statement;
	char	*query;
	 int	ret;

	// Build Query
	query = mkstr(
		"SELECT acct_is_disabled,acct_is_coke,acct_is_admin,acct_is_door,acct_is_internal"
		" FROM accounts WHERE acct_id=%i LIMIT 1",
		UserID
		);
	statement = Bank_int_QuerySingle(gBank_Database, query);
	free(query);
	if( !statement )	return -1;

	// Get Flags
	ret = 0;
	// - Disabled
	if( sqlite3_column_int(statement, 0) )	ret |= USER_FLAG_DISABLED;
	// - Coke
	if( sqlite3_column_int(statement, 1) )	ret |= USER_FLAG_COKE;
	// - Wheel
	if( sqlite3_column_int(statement, 2) )	ret |= USER_FLAG_ADMIN;
	// - Door
	if( sqlite3_column_int(statement, 3) )	ret |= USER_FLAG_DOORGROUP;
	// - Internal
	if( sqlite3_column_int(statement, 4) )	ret |= USER_FLAG_INTERNAL;
	
	// Destroy and return
	sqlite3_finalize(statement);
	
	return ret;
}

/*
 * Set user flags
 */
int Bank_SetFlags(int UserID, int Mask, int Value)
{
	char	*query;
	 int	rv;
	char	*errmsg;

	#define MAP_FLAG(name, flag)	(Mask&(flag)?(Value&(flag)?","name"=1":","name"=0"):"")
	query = mkstr(
		"UPDATE accounts SET acct_id=acct_id%s%s%s%s%s WHERE acct_id=%i",// LIMIT 1",
		MAP_FLAG("acct_is_coke", USER_FLAG_COKE),
		MAP_FLAG("acct_is_admin", USER_FLAG_ADMIN),
		MAP_FLAG("acct_is_door", USER_FLAG_DOORGROUP),
		MAP_FLAG("acct_is_internal", USER_FLAG_INTERNAL),
		MAP_FLAG("acct_is_disabled", USER_FLAG_DISABLED),
		UserID
		);
	#undef MAP_FLAG

	// Execute Query
	rv = sqlite3_exec(gBank_Database, query, NULL, NULL, &errmsg);
	if( rv != SQLITE_OK )
	{
		fprintf(stderr, "Bank_SetUserFlags - SQLite Error: %s\n", errmsg);
		fprintf(stderr, "query = '%s'\n", query);
		free(query);
		sqlite3_free(errmsg);
		return -1;
	}
	free(query);
	
	return 0;
}

/*
 * Get user balance
 */
int Bank_GetBalance(int AcctID)
{
	sqlite3_stmt	*statement;
	char	*query;
	 int	ret;
	
	query = mkstr("SELECT acct_balance FROM accounts WHERE acct_id=%i LIMIT 1", AcctID);
	statement = Bank_int_QuerySingle(gBank_Database, query);
	free(query);
	if( !statement )	return INT_MIN;
	
	// Read return value
	ret = sqlite3_column_int(statement, 0);
	
	// Clean up and return
	sqlite3_finalize(statement);
	return ret;
}

/*
 * Get the name of an account
 */
char *Bank_GetAcctName(int AcctID)
{
	sqlite3_stmt	*statement;
	char	*query;
	char	*ret;
	
	query = mkstr("SELECT acct_name FROM accounts WHERE acct_id=%i LIMIT 1", AcctID);
	statement = Bank_int_QuerySingle(gBank_Database, query);
	free(query);
	if( !statement )	return NULL;
	
	// Read return value
	ret = strdup( (const char*)sqlite3_column_text(statement, 0) );
	
	// Clean up and return
	sqlite3_finalize(statement);
	return ret;
}

/*
 * Get an account ID from a name
 */
int Bank_GetAcctByName(const char *Name)
{
	char	*query;
	sqlite3_stmt	*statement;
	 int	ret;
	
	if( !Bank_int_IsValidName(Name) )	return -1;
	
	query = mkstr("SELECT acct_id FROM accounts WHERE acct_name='%s' LIMIT 1", Name);
	statement = Bank_int_QuerySingle(gBank_Database, query);
	free(query);
	if( !statement )	return -1;
	
	ret = sqlite3_column_int(statement, 0);
	sqlite3_finalize(statement);
	
	if( ret == 0 )	return -1;
	return ret;
}

/*
 * Create a new named account
 */
int Bank_CreateAcct(const char *Name)
{
	char	*query;
	char	*errmsg;
	 int	rv;
	
	if( Name )
	{
		if( !Bank_int_IsValidName(Name) )	return -1;
		query = mkstr("INSERT INTO accounts (acct_name) VALUES ('%s')", Name);
	}
	else
	{
		query = strdup("INSERT INTO accounts (acct_name) VALUES (NULL)");
	}
		
	rv = sqlite3_exec(gBank_Database, query, NULL, NULL, &errmsg);
	if( rv != SQLITE_OK )
	{
		fprintf(stderr, "Bank_CreateAcct - SQLite Error: '%s'\n", errmsg);
		fprintf(stderr, "Query = '%s'\n", query);
		sqlite3_free(errmsg);
		free(query);
		return -1;
	}
	
	free(query);
	
	return sqlite3_last_insert_rowid(gBank_Database);
}

/*
 * Create an iterator for user accounts
 */
tAcctIterator *Bank_Iterator(int FlagMask, int FlagValues, int Flags, int MinMaxBalance, time_t LastSeen)
{
	char	*query;
	const char	*balanceClause;
	const char	*lastSeenClause;
	const char	*orderClause;
	const char	*revSort;
	sqlite3_stmt	*ret;
	
	// Balance condtion
	if( Flags & BANK_ITFLAG_MINBALANCE )
		balanceClause = " AND acct_balance>=";
	else if( Flags & BANK_ITFLAG_MAXBALANCE )
		balanceClause = " AND acct_balance<=";
	else {
		balanceClause = " AND 1!=";
		MinMaxBalance = 0;
	}
	
	// Last seen condition
	if( Flags & BANK_ITFLAG_SEENAFTER )
		lastSeenClause = " AND acct_last_seen>=";
	else if( Flags & BANK_ITFLAG_SEENBEFORE )
		lastSeenClause = " AND acct_last_seen<=";
	else {
		lastSeenClause = " AND datetime(-1,'unixepoch')!=";
	}
	
	// Sorting clause
	switch( Flags & BANK_ITFLAG_SORTMASK )
	{
	case BANK_ITFLAG_SORT_NONE:
		orderClause = "";
		revSort = "";
		break;
	case BANK_ITFLAG_SORT_NAME:
		orderClause = "ORDER BY acct_name";
		revSort = " DESC";
		break;
	case BANK_ITFLAG_SORT_BAL:
		orderClause = "ORDER BY acct_balance";
		revSort = " DESC";
		break;
	case BANK_ITFLAG_SORT_LASTSEEN:
		orderClause = "ORDER BY acct_balance";
		revSort = " DESC";
		break;
	default:
		fprintf(stderr, "BUG: Unknown sort (%x) in SQLite CokeBank\n", Flags & BANK_ITFLAG_SORTMASK);
		return NULL;
	}
	if( !(Flags & BANK_ITFLAG_REVSORT) )
		revSort = "";
	
	#define MAP_FLAG(name, flag)	(FlagMask&(flag)?(FlagValues&(flag)?" AND "name"=1":" AND "name"=0"):"")
	query = mkstr("SELECT acct_id FROM accounts WHERE 1=1"
		"%s%s%s%s%s"	// Flags
		"%s%i"	// Balance
		"%sdatetime(%"PRIu64",'unixepoch')"	// Last seen
		"%s%s"	// Sort and direction
		,
		MAP_FLAG("acct_is_coke", USER_FLAG_COKE),
		MAP_FLAG("acct_is_admin", USER_FLAG_ADMIN),
		MAP_FLAG("acct_is_door", USER_FLAG_DOORGROUP),
		MAP_FLAG("acct_is_internal", USER_FLAG_INTERNAL),
		MAP_FLAG("acct_is_disabled", USER_FLAG_DISABLED),
		balanceClause, MinMaxBalance,
		lastSeenClause, (uint64_t)LastSeen,
		orderClause, revSort
		);
	//printf("query = \"%s\"\n", query);
	#undef MAP_FLAG
	
	ret = Bank_int_MakeStatemnt(gBank_Database, query);
	if( !ret )	return NULL;
	
	free(query);
	
	return (void*)ret;
}

/*
 * Get the next account in an iterator
 */
int Bank_IteratorNext(tAcctIterator *It)
{
	 int	rv;
	rv = sqlite3_step( (sqlite3_stmt*)It );
	
	if( rv == SQLITE_DONE )	return -1;
	if( rv != SQLITE_ROW ) {
		fprintf(stderr, "Bank_IteratorNext - SQLite Error: %s\n", sqlite3_errmsg(gBank_Database));
		return -1;
	}
	
	return sqlite3_column_int( (sqlite3_stmt*)It, 0 );
}

/*
 * Free an interator
 */
void Bank_DelIterator(tAcctIterator *It)
{
	sqlite3_finalize( (sqlite3_stmt*)It );
}

/*
 * Check user authentication token
 */
int Bank_GetUserAuth(const char *Salt, const char *Username, const char *Password)
{
	Salt = Password = Username;	// Shut up GCC
	// DEBUG HACKS!
	#if 1
	return Bank_GetAcctByName(Username);
	#else
	return -1;
	#endif
}

/*
 * Get an account number given a card ID
 * NOTE: Actually ends up just being an alternate authentication token,
 *       as no checking is done on the ID's validity, save for SQL sanity.
 */
int Bank_GetAcctByCard(const char *CardID)
{
	char	*query;
	sqlite3_stmt	*statement;
	 int	ret;
	
	if( !Bank_int_IsValidName(CardID) )
		return -1;
	
	query = mkstr("SELECT acct_id FROM cards WHERE card_name='%s' LIMIT 1", CardID);
	statement = Bank_int_QuerySingle(gBank_Database, query);
	free(query);
	if( !statement )	return -1;
	
	ret = sqlite3_column_int(statement, 0);
	
	sqlite3_finalize(statement);
	
	return ret;
}

/*
 * Add a card to an account
 */
int Bank_AddAcctCard(int AcctID, const char *CardID)
{
	char	*query;
	 int	rv;
	char	*errmsg;
	
	if( !Bank_int_IsValidName(CardID) )
		return -1;
	
	// TODO: Check the AcctID too
	
	// Insert card
	query = mkstr("INSERT INTO cards (acct_id,card_name) VALUES (%i,'%s')",
		AcctID, CardID);
	rv = sqlite3_exec(gBank_Database, query, NULL, NULL, &errmsg);
	if( rv == SQLITE_CONSTRAINT )
	{
		sqlite3_free(errmsg);
		free(query);
		return 2;	// Card in use
	}
	if( rv != SQLITE_OK )
	{
		fprintf(stderr, "Bank_AddAcctCard - SQLite Error: '%s'\n", errmsg);
		fprintf(stderr, "Query = '%s'\n", query);
		sqlite3_free(errmsg);
		free(query);
		return -1;
	}
	free(query);
	
	return 0;
}

/*
 * Create a SQLite Statement
 */
sqlite3_stmt *Bank_int_MakeStatemnt(sqlite3 *Database, const char *Query)
{
	 int	rv;
	sqlite3_stmt	*ret;
	rv = sqlite3_prepare_v2(Database, Query, strlen(Query)+1, &ret, NULL);
	if( rv != SQLITE_OK ) {
		fprintf(stderr, "SQLite Error: %s\n", sqlite3_errmsg(Database));
		fprintf(stderr, "query = \"%s\"\n", Query);
		return NULL;
	}
	
	return ret;
}

/*
 * Create a SQLite statement and query it for the first row
 * Returns NULL if the the set is empty
 */
sqlite3_stmt *Bank_int_QuerySingle(sqlite3 *Database, const char *Query)
{
	sqlite3_stmt	*ret;
	 int	rv;
	
	#if DEBUG
	printf("Bank_int_QuerySingle: (Query='%s')\n", Query);
	#endif
	
	// Prepare query
	ret = Bank_int_MakeStatemnt(Database, Query);
	if( !ret ) {
		#if DEBUG >= 2
		printf("Bank_int_QuerySingle: RETURN NULL ret=NULL\n");
		#endif
		return NULL;
	}
	
	// Get row
	rv = sqlite3_step(ret);
	// - Empty result set
	if( rv == SQLITE_DONE ) {
		#if DEBUG >= 2
		printf("Bank_int_QuerySingle: RETURN NULL (rv == SQLITE_DONE)\n");
		#endif
		return NULL;
	}
	// - Other error
	if( rv != SQLITE_ROW ) {
		fprintf(stderr, "SQLite Error: %s\n", sqlite3_errmsg(gBank_Database));
		fprintf(stderr, "query = \"%s\"\n", Query);
		return NULL;
	}
	
	#if DEBUG >= 2
	printf("Bank_int_QuerySingle: RETURN %p\n", ret);
	#endif
	return ret;
}

/**
 * \brief Checks if the passed account name is valid
 */
int Bank_int_IsValidName(const char *Name)
{
	while(*Name)
	{
		if( *Name == '\'' )	return 0;
		Name ++;
	}
	return 1;
}
