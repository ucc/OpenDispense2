/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * cokebank.c - Coke-Bank management
 *
 * This file is licenced under the 3-clause BSD Licence. See the file COPYING
 * for full details.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include "common.h"

// === PROTOTYPES ===
void	Init_Cokebank(const char *Argument);
 int	Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason);
 int	GetBalance(int User);
char	*GetUserName(int User);
 int	GetUserID(const char *Username); 
 int	GetUserAuth(const char *Username, const char *Password);

// === GLOBALS ===
FILE	*gBank_LogFile;

// === CODE ===
/**
 * \brief Load the cokebank database
 */
void Init_Cokebank(const char *Argument)
{
	gBank_File = fopen(Argument, "rb+");
	if( !gBank_File ) {
		gBank_File = fopen(Argument, "wb+");
	}
	if( !gBank_File ) {
		perror("Opening coke bank");
	}

	gBank_LogFile = fopen("cokebank.log", "a");
	if( !gBank_LogFile )	gBank_LogFile = stdout;

	fseek(gBank_File, 0, SEEK_END);
	giBank_NumUsers = ftell(gBank_File) / sizeof(gaBank_Users[0]);
	fseek(gBank_File, 0, SEEK_SET);
	gaBank_Users = malloc( giBank_NumUsers * sizeof(gaBank_Users[0]) );
	fread(gaBank_Users, sizeof(gaBank_Users[0]), giBank_NumUsers, gBank_File);
}

/**
 * \brief Transfers money from one user to another
 * \param SourceUser	Source user
 * \param DestUser	Destination user
 * \param Ammount	Ammount of cents to move from \a SourceUser to \a DestUser
 * \param Reason	Reason for the transfer (essentially a comment)
 * \return Boolean failure
 */
int Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason)
{
	 int	srcBal = Bank_GetUserBalance(SourceUser);
	 int	dstBal = Bank_GetUserBalance(DestUser);
	
	if( srcBal - Ammount < Bank_GetMinAllowedBalance(SourceUser) )
		return 1;
	if( dstBal + Ammount < Bank_GetMinAllowedBalance(DestUser) )
		return 1;
	Bank_AlterUserBalance(DestUser, Ammount);
	Bank_AlterUserBalance(SourceUser, -Ammount);
	fprintf(gBank_LogFile, "ACCT #%i{%i} -= %ic [to #%i] (%s)\n", SourceUser, srcBal, Ammount, DestUser, Reason);
	fprintf(gBank_LogFile, "ACCT #%i{%i} += %ic [from #%i] (%s)\n", DestUser, dstBal, Ammount, SourceUser, Reason);
	return 0;
}

int GetFlags(int User)
{
	return Bank_GetUserFlags(User);
}

/**
 * \brief Get the balance of the passed user
 */
int GetBalance(int User)
{
	return Bank_GetUserBalance(User);;
}

/**
 * \brief Return the name the passed user
 */
char *GetUserName(int User)
{
	return Bank_GetUserName(User);
}

/**
 * \brief Get the User ID of the named user
 */
int GetUserID(const char *Username)
{
	 int	ret;

	// Get internal ID (or create new user)
	ret = Bank_GetUserByName(Username);
	if( ret == -1 ) {
		ret = Bank_AddUser(Username);
	}

	return ret;
}

