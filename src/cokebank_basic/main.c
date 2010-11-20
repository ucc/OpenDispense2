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
#include <pwd.h>
#include <string.h>

// === IMPORTS ===
 int	Bank_GetMinAllowedBalance(int ID);
 int	Bank_GetUserBalance(int ID);
 int	Bank_AlterUserBalance(int ID, int Delta);
 int	Bank_GetUserByUnixID(int UnixID);
 int	Bank_GetUserByName(const char *Name);
 int	Bank_AddUser(int UnixID);

// === PROTOTYPES ===
void	Init_Cokebank(void);
 int	Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason);
 int	GetBalance(int User);
char	*GetUserName(int User);
 int	GetUserID(const char *Username); 
 int	GetUserAuth(const char *Username, const char *Password);

// === CODE ===
/**
 * \brief Load the cokebank database
 */
void Init_Cokebank(void)
{
	
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
	if( Bank_GetUserBalance(SourceUser) - Ammount < Bank_GetMinAllowedBalance(SourceUser) )
		return 1;
	if( Bank_GetUserBalance(DestUser) + Ammount < Bank_GetMinAllowedBalance(DestUser) )
		return 1;
	Bank_AlterUserBalance(DestUser, Ammount);
	Bank_AlterUserBalance(SourceUser, -Ammount);
	return 0;
}

/**
 * \brief Get the balance of the passed user
 */
int GetBalance(int User)
{
	return 0;
}

/**
 * \brief Return the name the passed user
 */
char *GetUserName(int User)
{
	return NULL;
}

/**
 * \brief Get the User ID of the named user
 */
int GetUserID(const char *Username)
{
	struct passwd	*pwd;
	 int	ret;

	// Get user ID
	pwd = getpwnam(Username);
	if( !pwd ) {
		return -1;
	}

	// Get internal ID (or create new user)
	ret = Bank_GetUserByUnixID(pwd->pw_uid);
	if( ret == -1 ) {
		ret = Bank_AddUser(pwd->pw_uid);
	}

	return ret;
}

/**
 * \brief Authenticate a user
 * \return User ID, or -1 if authentication failed
 */
int GetUserAuth(const char *Username, const char *Password)
{
	if( strcmp(Username, "test") == 0 )
		return Bank_GetUserByName("test");
	return -1;
}

