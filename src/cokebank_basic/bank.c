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

enum {
	FLAG_TYPEMASK    = 0x03,
	USER_FLAG_NORMAL = 0x00,
	USER_FLAG_COKE   = 0x01,
	USER_FLAG_WHEEL  = 0x02,
	USER_FLAG_GOD    = 0x03
};

// === CODE ===
int Bank_GetUserByUnixID(int UnixUID)
{
	// Expensive search :(
	for( i = 0; i < giBank_NumUsers; i ++ )
	{
		if( gaBank_Users[i].UnixID == UnixID )
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

int Bank_AlterUserBalance(int ID, int Delta)
{
	// Sanity
	if( ID < 0 || ID >= giBank_NumUsers )
		return -1;

	// Update
	gaBank_Users[ID].Balance += Delta;

	// Commit
	fseek(gBank_File, ID*sizeof(gaBank_Users[0]), SEEK_SET);
	fwrite(&gaBank_Users[ID], sizeof(gaBank_Users[0]), 1, gBank_File);
	
	return 0;
}

int Bank_SetUserBalance(int ID, int Value)
{
	// Sanity
	if( ID < 0 || ID >= giBank_NumUsers )
		return -1;

	// Update
	gaBank_Users[ID].Balance = Value;
	
	// Commit
	fseek(gBank_File, ID*sizeof(gaBank_Users[0]), SEEK_SET);
	fwrite(&gaBank_Users[ID], sizeof(gaBank_Users[0]), 1, gBank_File);
	
	return 0;
}

int Bank_GetMinAllowedBalance(int ID)
{
	if( ID < 0 || ID >= giBank_NumUsers )
		return -1;

	switch( gaBank_Users[ID].Flags & FLAG_TYPEMASK )
	{
	case USER_TYPE_NORMAL:	return     0;
	case USER_TYPE_COKE:	return  -2000;
	case USER_TYPE_WHEEL:	return -10000;
	case USER_TYPE_GOD:	return INT_MIN;
	default:	return 0;
	}
}

/**
 * \brief Create a new user in our database
 */
int Bank_AddUser(int UnixID)
{
	void	*tmp;

	// Can has moar space plz?
	tmp = realloc(gaBank_Users, (giBank_NumUsers+1)*sizeof(gaBank_Users[0]));
	if( !tmp )	return -1;
	gaBank_Users = tmp;

	// Crete new user
	gaBank_Users[giBank_NumUsers].UnixID = UnixID;
	gaBank_Users[giBank_NumUsers].Balance = 0;
	gaBank_Users[giBank_NumUsers].Flags = 0;
	
	// Commit to file
	fseek(gBank_File, giBank_NumUsers*sizeof(gaBank_Users[0]), SEEK_SET);
	fwrite(gaBank_Users[giBank_NumUsers], sizeof(gaBank_Users[0]), 1, gBank_File);

	// Increment count
	giBank_NumUsers ++;

	return 0;
}
