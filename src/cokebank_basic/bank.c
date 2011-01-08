/*
 * OpenDispense 2
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 * - Cokebank (Basic Version)
 *
 * bank.c - Actual bank database
 *
 * This file is licenced under the 3-clause BSD Licence. See the file COPYING
 * for full details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include "common.h"

#define USE_UNIX_GROUPS	1

// === PROTOTYPES ===
static int	GetUnixID(const char *Username);

// === GLOBALS ===
tUser	*gaBank_Users;
 int	giBank_NumUsers;
FILE	*gBank_File;

// === CODE ===
static int Bank_int_WriteEntry(int ID)
{
	if( ID < 0 || ID >= giBank_NumUsers ) {
		return -1;
	}
	
	// Commit to file
	fseek(gBank_File, ID*sizeof(gaBank_Users[0]), SEEK_SET);
	fwrite(&gaBank_Users[ID], sizeof(gaBank_Users[0]), 1, gBank_File);
	
	return 0;
}

int Bank_GetUserByName(const char *Username)
{
	 int	i, uid;
	
	uid = GetUnixID(Username);
	
	// Expensive search :(
	for( i = 0; i < giBank_NumUsers; i ++ )
	{
		if( gaBank_Users[i].UnixID == uid )
			return i;
	}

	return -1;
}

int Bank_GetUserBalance(int ID)
{
	if( ID < 0 || ID >= giBank_NumUsers )
		return INT_MIN;

	return gaBank_Users[ID].Balance;
}

int Bank_GetUserFlags(int ID)
{
	if( ID < 0 || ID >= giBank_NumUsers )
		return -1;

	// root
	if( gaBank_Users[ID].UnixID == 0 ) {
		gaBank_Users[ID].Flags |= USER_FLAG_WHEEL|USER_FLAG_COKE;
	}

	#if USE_UNIX_GROUPS
	// TODO: Implement checking the PAM groups and status instead, then
	// fall back on the database. (and update if there is a difference)
	if( gaBank_Users[ID].UnixID > 0 )
	{
		struct passwd	*pwd;
		struct group	*grp;
		 int	i;
		
		// Get username
		pwd = getpwuid( gaBank_Users[ID].UnixID );
		
		// Check for additions to the "coke" group
		grp = getgrnam("coke");
		if( grp ) {
			for( i = 0; grp->gr_mem[i]; i ++ )
			{
				if( strcmp(grp->gr_mem[i], pwd->pw_name) == 0 ) {
					gaBank_Users[ID].Flags |= USER_FLAG_COKE;
					break ;
				}
			}
		}
		
		// Check for additions to the "wheel" group
		grp = getgrnam("wheel");
		if( grp ) {
			for( i = 0; grp->gr_mem[i]; i ++ )
			{
				if( strcmp(grp->gr_mem[i], pwd->pw_name) == 0 ) {
					gaBank_Users[ID].Flags |= USER_FLAG_WHEEL;
					break ;
				}
			}
		}
	}
	#endif

	return gaBank_Users[ID].Flags;
}

int Bank_SetUserFlags(int ID, int Mask, int Value)
{
	// Sanity
	if( ID < 0 || ID >= giBank_NumUsers )
		return -1;
	
	// Silently ignore changes to root and meta accounts
	if( gaBank_Users[ID].UnixID <= 0 )	return 0;
	
	gaBank_Users[ID].Flags &= ~Mask;
	gaBank_Users[ID].Flags |= Value;

	Bank_int_WriteEntry(ID);
	
	return 0;
}

int Bank_AlterUserBalance(int ID, int Delta)
{
	// Sanity
	if( ID < 0 || ID >= giBank_NumUsers )
		return -1;

	// Update
	gaBank_Users[ID].Balance += Delta;

	Bank_int_WriteEntry(ID);
	
	return 0;
}

int Bank_GetMinAllowedBalance(int ID)
{
	 int	flags;
	if( ID < 0 || ID >= giBank_NumUsers )
		return 0;

	flags = Bank_GetUserFlags(ID);

	// Internal accounts have no limit
	if( (flags & USER_FLAG_INTERNAL) )
		return INT_MIN;

	// Wheel is allowed to go to -$100
	if( (flags & USER_FLAG_WHEEL) )
		return -10000;
	
	// Coke is allowed to go to -$20
	if( (flags & USER_FLAG_COKE) )
		return -2000;

	// For everyone else, no negative
	return 0;
}

/**
 * \brief Create a new user in our database
 */
int Bank_AddUser(const char *Username)
{
	void	*tmp;
	 int	uid = GetUnixID(Username);

	// Can has moar space plz?
	tmp = realloc(gaBank_Users, (giBank_NumUsers+1)*sizeof(gaBank_Users[0]));
	if( !tmp )	return -1;
	gaBank_Users = tmp;

	// Crete new user
	gaBank_Users[giBank_NumUsers].UnixID = uid;
	gaBank_Users[giBank_NumUsers].Balance = 0;
	gaBank_Users[giBank_NumUsers].Flags = 0;
	
	if( strcmp(Username, COKEBANK_DEBT_ACCT) == 0 ) {
		gaBank_Users[giBank_NumUsers].Flags = USER_FLAG_INTERNAL;
	}
	else if( strcmp(Username, COKEBANK_SALES_ACCT) == 0 ) {
		gaBank_Users[giBank_NumUsers].Flags = USER_FLAG_INTERNAL;
	}
	else if( strcmp(Username, "root") == 0 ) {
		gaBank_Users[giBank_NumUsers].Flags = USER_FLAG_WHEEL|USER_FLAG_COKE;
	}

	// Increment count
	giBank_NumUsers ++;
	
	Bank_int_WriteEntry(giBank_NumUsers - 1);

	return 0;
}

// ---
// Unix user dependent code
// TODO: Modify to keep its own list of usernames
// ---
char *Bank_GetUserName(int ID)
{
	struct passwd	*pwd;
	
	if( ID < 0 || ID >= giBank_NumUsers )
		return NULL;
	
	if( gaBank_Users[ID].UnixID == -1 )
		return strdup(COKEBANK_SALES_ACCT);

	if( gaBank_Users[ID].UnixID == -2 )
		return strdup(COKEBANK_DEBT_ACCT);

	pwd = getpwuid(gaBank_Users[ID].UnixID);
	if( !pwd )	return NULL;

	return strdup(pwd->pw_name);
}

static int GetUnixID(const char *Username)
{
	 int	uid;

	if( strcmp(Username, COKEBANK_SALES_ACCT) == 0 ) {	// Pseudo account that sales are made into
		uid = -1;
	}
	else if( strcmp(Username, COKEBANK_DEBT_ACCT) == 0 ) {	// Pseudo acount that money is added from
		uid = -2;
	}
	else {
		struct passwd	*pwd;
		// Get user ID
		pwd = getpwnam(Username);
		if( !pwd )	return -1;
		uid = pwd->pw_uid;
	}
	return uid;
}
