/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * cokebank.c - Coke-Bank management
 *
 * This file is licenced under the 3-clause BSD Licence. See the file COPYING
 * for full details.
 * 
 * TODO: Make this a Dynamic Library and load it at runtime
 */
#include <stdlib.h>
#include <stdio.h>

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
int Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason);
{
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
	return -1;
}

/**
 * \brief Authenticate a user
 * \return User ID, or -1 if authentication failed
 */
int GetUserAuth(const char *Username, const char *Password)
{
	return -1;
}

