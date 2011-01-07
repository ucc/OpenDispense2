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
	USER_FLAG_COKE  	= 0x01,
	USER_FLAG_WHEEL 	= 0x02,
	USER_FLAG_DOORGROUP	= 0x04,
	USER_FLAG_INTERNAL	= 0x40,
	USER_FLAG_DISABLED	= 0x80
};

// --- Cokebank Functions ---
/**
 * \brief Transfer money from one account to another
 * \param SourceUser	UID (from \a GetUserID) to take the money from
 * \param DestUser	UID (from \a GetUserID) give money to
 * \param Ammount	Amount of money (in cents) to transfer
 * \param Reason	Reason for the transfer
 */
extern int	Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason);
/**
 * \brief Get flags on an account
 * \param User	UID to get flags from
 * \see eCokebank_Flags
 */
extern int	GetFlags(int User);
/**
 * \brief Set an account's flags
 * \param User	UID to set flags on
 * \param Mask	Mask of flags changed
 * \param Value	Final value of changed flags
 */
extern int	SetFlags(int User, int Mask, int Value);
/**
 * \brief Get an account's balance
 * \param User	UID to query
 */
extern int	GetBalance(int User);
/**
 * \brief Get the name associated with an account
 * \return Heap string
 */
extern char	*GetUserName(int User);
/**
 * \brief Get a UID from a passed name
 */
extern int	GetUserID(const char *Username);
/**
 * \brief Create a new account
 */
extern int	CreateUser(const char *Username);
/**
 * \brief Get the maximum UID
 * \note Used for iterating accounts
 */
extern int	GetMaxID(void);
/**
 * \brief Validates a user's authentication
 * \param Salt	Salt given to the client for hashing the password
 * \param Username	Username used
 * \param Password	Password sent by the client
 */
extern int	GetUserAuth(const char *Salt, const char *Username, const char *Password);

#endif
