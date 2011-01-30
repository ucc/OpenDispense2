/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * SQLite Coke Bank (Accounts Database)
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
#include "../cokebank.h"
#include <sqlite3.h>

const char * const csBank_CreateAccountQry = "CREATE TABLE IF NOT EXISTS accounts ("
"	acct_id INTEGER PRIMARY KEY NOT NULL,"
"	acct_balance INTEGER NOT NULL,"
"	acct_name STRING UNIQUE,"
"	acct_uid INTEGER UNIQUE,"
"	acct_pin INTEGER CHECK (acct_pin > 0 AND acct_pin < 10000),"
"	acct_is_disabled BOOLEAN NOT NULL DEFAULT false,"
"	acct_is_coke BOOLEAN NOT NULL DEFAULT false,"
"	acct_is_wheel BOOLEAN NOT NULL DEFAULT false,"
"	acct_is_door BOOLEAN NOT NULL DEFAULT false,"
"	acct_is_internal BOOLEAN NOT NULL DEFAULT false"
")";
const char * const csBank_CreateCardsQry = "CREATE TABLE IF NOT EXISTS cards ("
"	acct_id INTEGER NOT NULL,"
"	card_name STRING NOT NULL UNIQUE,"
"	FOREIGN KEY (acct_id) REFERENCES accounts (acct_id) ON DELETE CASCADE"
//	                 Deletion of the account frees the card  ^ ^ ^
")";

// === PROTOYPES ===
 int	Bank_Initialise(const char *Argument);
 int	Bank_Transfer(int SourceAcct, int DestAcct, int Ammount, const char *Reason);
 int	Bank_GetUserFlags(int AcctID);
 int	Bank_SetUserFlags(int AcctID, int Mask, int Value);
 int	Bank_GetBalance(int AcctID);
char	*Bank_GetAcctName(int AcctID);
sqlite3_stmt	*Bank_int_MakeStatemnt(sqlite3 *Database, const char *Query);
sqlite3_stmt	*Bank_int_QuerySingle(sqlite3 *Database, const char *Query);

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
	else if( rv == SQLITE_NOTFOUND )
	{
		sqlite3_free(errmsg);
		// Create tables
		// - Accounts
		rv = sqlite3_exec(gBank_Database, csBank_CreateAccountQry, NULL, NULL, &errmsg);
		if( rv != SQLITE_OK ) {
			fprintf(stderr, "SQLite Error: %s\n", errmsg);
			sqlite3_free(errmsg);
			return 1;
		}
		// - Mifare relation
		rv = sqlite3_exec(gBank_Database, csBank_CreateCardsQry, NULL, NULL, &errmsg);
		if( rv != SQLITE_OK ) {
			fprintf(stderr, "SQLite Error: %s\n", errmsg);
			sqlite3_free(errmsg);
			return 1;
		}
	}
	else
	{
		// Unknown error
		fprintf(stderr, "SQLite Error: %s\n", errmsg);
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
	char	*query
	 int	rv;
	char	*errmsg;
	
	// Begin SQL Transaction
	sqlite3_exec(gBank_Database, "BEGIN TRANSACTION", NULL, NULL, NULL);

	// Take from the source
	query = mkstr("UPDATE accounts SET acct_balance=acct_balance-%i WHERE acct_id=%i", Ammount, SourceUser);
	rv = sqlite3_exec(gBank_Database, query, NULL, NULL, &errmsg);
	free(query);
	if( rv != SQLITE_OK )
	{
		fprintf(stderr, "SQLite Error: %s\n", errmsg);
		sqlite3_free(errMsg);
		sqlite3_query(gBank_Database, "ROLLBACK", NULL, NULL, NULL);
		return 1;
	}

	// Give to the destination
	query = mkstr("UPDATE accounts SET acct_balance=acct_balance+%i WHERE acct_id=%i", Ammount, DestUser);
	rv = sqlite3_exec(gBank_Database, query, NULL, NULL, &errmsg);
	free(query);
	if( rv != SQLITE_OK )
	{
		fprintf(stderr, "SQLite Error: %s\n", errmsg);
		sqlite3_free(errmsg);
		sqlite3_query(gBank_Database, "ROLLBACK", NULL, NULL, NULL);
		return 1;
	}

	// Commit transaction
	sqlite3_query(gBank_Database, "COMMIT", NULL, NULL, NULL);

	return 0;
}

/*
 * Get user flags
 */
int Bank_GetUserFlags(int UserID)
{
	sqlite3_stmt	*statement;
	char	*query;
	 int	rv;
	 int	ret;

	// Build Query
	query = mkstr(
		"SELECT acct_is_disabled,acct_is_coke,acct_is_wheel,acct_is_door,acct_is_internal"
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
	if( sqlite3_column_int(statement, 2) )	ret |= USER_FLAG_WHEEL;
	// - Door
	if( sqlite3_column_int(statement, 3) )	ret |= USER_FLAG_DOOR;
	// - Internal
	if( sqlite3_column_int(statement, 3) )	ret |= USER_FLAG_INTERNAL;
	
	// Destroy and return
	sqlite3_finalise(statement);
	
	return ret;
}

/*
 * Set user flags
 */
int Bank_SetUserFlags(int UserID, int Mask, int Value)
{
	char	*query;
	 int	rv;
	char	*errmsg;

	#define MAP_FLAG(name, flag)	(Mask&(flag)?(Value&(flag)?","name"=1":","name"=0"))
	query = mkstr(
		"UDPATE accounts WHERE acct_id=%i SET acct_id=acct_id%s%s%s%s%s",
		MAP_FLAG("acct_is_coke", USER_FLAG_COKE),
		MAP_FLAG("acct_is_wheel", USER_FLAG_WHEEL),
		MAP_FLAG("acct_is_door", USER_FLAG_DOORGROUP),
		MAP_FLAG("acct_is_internal", USER_FLAG_INTERNAL),
		MAP_FLAG("acct_is_disabled", USER_FLAG_DISABLED)
		);
	#undef MAP_FLAG

	// Execute Query
	rv = sqlite3_query(gBank_Database, query, NULL, NULL, &errmsg);
	free(query);
	if( rv != SQLITE_OK )
	{
		fprintf(stderr, "SQLite Error: %s\n", errmsg);
		sqlite3_free(errmsg);
		return -1;
	}
	
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
	sqlite3_finalise(statement);
	return ret;
}

/*
 * Get the name of an account
 */
char *Bank_GetUserName(int AcctID)
{
	sqlite3_stmt	*statement;
	char	*query;
	char	*ret;
	
	query = mkstr("SELECT acct_name FROM accounts WHERE acct_id=%i LIMIT 1", AcctID);
	statement = Bank_int_QuerySingle(gBank_Database, query);
	free(query);
	if( !statement )	return NULL;
	
	// Read return value
	ret = strdup( sqlite3_column_text(statement, 0) );
	
	// Clean up and return
	sqlite3_finalise(statement);
	return ret;
}

/*
 * Create a SQLite Statement
 */
sqlite3_stmt *Bank_int_MakeStatemnt(sqlite3 *Database, const char *Query)
{
	 int	rv;
	sqlite3_stmt	*ret;
	rv = sqlite3_prepare_v2(Database, Query, strlen(Query)+1, &ret, NULL);
	free(query);
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
	
	// Prepare query
	ret = Bank_int_MakeStatemnt(Database, Query);
	if( !statement )	return NULL;
	
	// Get row
	rv = sqlite3_step(statement);
	// - Empty result set
	if( rv == SQLITE_DONE )	return NULL;
	// - Other error
	if( rv != SQLITE_ROW ) {
		fprintf(stderr, "SQLite Error: %s\n", sqlite3_errmsg(gBank_Database));
		fprintf(stderr, "query = \"%s\"\n", Query);
		return NULL;
	}
	
	return ret;
}
