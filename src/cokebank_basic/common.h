/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * cokebank.c - Coke-Bank management
 *
 * This file is licenced under the 3-clause BSD Licence. See the file COPYING
 * for full details.
 */
#ifndef _COKEBANK_COMMON_H_
#define _COKEBANK_COMMON_H_

typedef struct sUser {
	 int	UnixID;
	 int	Balance;
	 int	Flags;
}	tUser;

#endif
