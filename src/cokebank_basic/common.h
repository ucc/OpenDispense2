/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * cokebank_basic/common.h - Coke-Bank management
 *
 * This file is licenced under the 3-clause BSD Licence. See the file COPYING
 * for full details.
 */
#ifndef _COKEBANK_COMMON_H_
#define _COKEBANK_COMMON_H_

#include "../cokebank.h"

typedef struct sUser {
	 int	UnixID;
	 int	Balance;
	 int	Flags;
}	tUser;

// === IMPORTS ===
extern int	Bank_GetMinAllowedBalance(int ID);
extern int	Bank_GetUserBalance(int ID);
extern int	Bank_AlterUserBalance(int ID, int Delta);
extern char	*Bank_GetUserName(int ID);
extern int	Bank_GetUserFlags(int ID);
extern int	Bank_GetUserByName(const char *Username);
extern int	Bank_AddUser(const char *Username);
extern FILE	*gBank_File;
extern tUser	*gaBank_Users;
extern int	giBank_NumUsers;

#endif
