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

#if 0
typedef struct sUser
{
	char	*Username;
	 int	UID;
	 int	Pin;
	 int	Balance;
	 int	Flags;
	time_t	LastUsed;
}	tUser;
typedef struct sAltLogin
{
	tUser	*User;
	char	CardID[];
}	tAltLogin;
#endif

#endif
