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
#include "common.h"

// === PROTOTYPES ===
void	Init_Cokebank(void);
 int	AlterBalance(int User, int Delta);
 int	GetBalance(int User);
char	*GetUserName(int User);
 int	GetUserID(const char *Username); 

// === CODE ===
/**
 * \brief Load the cokebank database
 */
void Init_Cokebank(void)
{
	
}

/**
 * \brief Alters a user's balance by \a Delta
 */
int AlterBalance(int User, int Delta)
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
	return 0;
}

