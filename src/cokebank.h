/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * cokebank.h - Coke-Bank common definitions
 *
 * This file is licenced under the 3-clause BSD Licence. See the file COPYING
 * for full details.
 */
#ifndef _COKEBANK_H_
#define _COKEBANK_H_

#define COKEBANK_SALES_ACCT	">sales"	//!< Sales made into
#define COKEBANK_DEBT_ACCT	">liability"	//!< Credit taken out of

enum eCokebank_Flags {
	USER_FLAG_TYPEMASK = 0x03,
	USER_TYPE_NORMAL = 0x00,
	USER_TYPE_COKE   = 0x01,
	USER_TYPE_WHEEL  = 0x02,
	USER_TYPE_GOD    = 0x03,
	
	USER_FLAG_DOORGROUP = 0x40,
	USER_FLAG_DISABLED  = 0x80
};

// --- Cokebank Functions ---
extern int	Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason);
extern int	GetFlags(int User);
extern int	GetBalance(int User);
extern char	*GetUserName(int User);
extern int	GetUserID(const char *Username);
extern int	GetMaxID(void);

#endif
