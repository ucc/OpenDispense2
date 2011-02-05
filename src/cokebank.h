/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * cokebank.h - Coke-Bank common definitions
 *
 * This file is licenced under the 3-clause BSD Licence. See the file COPYING
 * for full details.
 */
/**
 * \file cokebank.h
 * \brief Coke Bank API Documentation
 */
#ifndef _COKEBANK_H_
#define _COKEBANK_H_

#include <stdlib.h>

#define COKEBANK_SALES_ACCT	">sales"	//!< Sales made into
#define COKEBANK_DEBT_ACCT	">liability"	//!< Credit taken out of
#define COKEBANK_FREE_ACCT	">freeitems"	//!< ODay drink costs taken out of

/**
 * \brief Account iterator opaque structure
 *
 * Opaque structure for account iterators returned by Bank_Iterator
 * and used by Bank_IteratorNext and Bank_DelIterator
 */
typedef struct sAcctIterator	tAcctIterator;

/**
 * \brief Iterator for a collection of items
 */
typedef struct sItemIterator	tItemIterator;

/**
 * \brief Flag values for the \a Flags parameter to Bank_Iterator
 */
enum eBank_ItFlags
{
	BANK_ITFLAG_MINBALANCE	= 0x01,	//!< Balance value is Minium Balance
	BANK_ITFLAG_MAXBALANCE	= 0x02,	//!< Balance value is Maximum Balance (higher priority)
	BANK_ITFLAG_SEENAFTER	= 0x04,	//!< Last seen value is lower bound
	BANK_ITFLAG_SEENBEFORE	= 0x08,	//!< Last seen value is upper bound (higher priority)
	
	BANK_ITFLAG_SORT_NONE	= 0x000,	//!< No sorting (up to the implementation)
	BANK_ITFLAG_SORT_NAME	= 0x100,	//!< Sort alphabetically ascending by name
	BANK_ITFLAG_SORT_BAL	= 0x200,	//!< Sort by balance, ascending
	BANK_ITFLAG_SORT_UNIXID	= 0x300,	//!< Sort by UnixUID (TODO: Needed?)
	BANK_ITFLAG_SORT_LASTSEEN = 0x400,	//!< Sort by last seen time (ascending)
	BANK_ITFLAG_SORTMASK	= 0x700,	//!< Sort type mask
	BANK_ITFLAG_REVSORT 	= 0x800	//!< Sort descending instead
};

/**
 * \brief User flag values
 *
 * User flag values used by Bank_GetFlags and Bank_SetFlags
 */
enum eCokebank_Flags {	
	USER_FLAG_COKE  	= 0x01,	//!< User is a coke member (can do coke accounting)
	USER_FLAG_ADMIN 	= 0x02,	//!< User is a administrator (can create, delete and lock accounts)
	USER_FLAG_DOORGROUP	= 0x04,	//!< User is in the door group (can open the clubroom door)
	USER_FLAG_INTERNAL	= 0x40,	//!< Account is internal (cannot be authenticated, no lower balance limit)
	USER_FLAG_DISABLED	= 0x80	//!< Account is disabled (no transactions allowed)
};

// --- Cokebank Functions ---
/**
 * \brief Initialise the cokebank
 * \param Argument	Cokebank argument specified in the dispense config file (typically the database path)
 * \return Boolean Failure
 */
extern int	Bank_Initialise(const char *Argument);

/**
 * \brief Transfer money from one account to another
 * \param SourceAcct	UID (from \a Bank_GetUserID) to take the money from
 * \param DestAcct	UID (from \a Bank_GetUserID) give money to
 * \param Ammount	Amount of money (in cents) to transfer
 * \param Reason	Reason for the transfer
 */
extern int	Bank_Transfer(int SourceAcct, int DestAcct, int Ammount, const char *Reason);
/**
 * \brief Get flags on an account
 * \param AcctID	UID to get flags from
 * \return Flag set as defined in eCokebank_Flags
 */
extern int	Bank_GetFlags(int AcctID);
/**
 * \brief Set an account's flags
 * \param AcctID	UID to set flags on
 * \param Mask	Mask of flags changed
 * \param Value	Final value of changed flags
 */
extern int	Bank_SetFlags(int AcctID, int Mask, int Value);
/**
 * \brief Get an account's balance
 * \param AcctID	Account to query
 */
extern int	Bank_GetBalance(int AcctID);
/**
 * \brief Get the name associated with an account
 * \return Heap string
 */
extern char	*Bank_GetAcctName(int AcctID);
/**
 * \brief Get an account ID from a passed name
 * \param Name	Name to search for
 * \return ID of the account, or -1 if not found
 */
extern int	Bank_GetAcctByName(const char *Name);
/**
 * \brief Create a new account
 * \param Name	Name for the new account (if NULL, an anoymous account is created)
 * \return ID of the new account
 */
extern int	Bank_CreateAcct(const char *Name);

/**
 * \brief Create an account iterator
 * \param FlagMask	Mask of account flags to check
 * \param FlagValues	Wanted values for checked flags
 * \param Flags	Specifies the operation of \a MinMaxBalance and \a Timestamp  (\see eBank_ItFlags)
 * \param MinMaxBalance	Mininum/Maximum balance
 * \param LastSeen	Latest/Earliest last seen time
 * \return Pointer to an iterator across the selected data set
 */
extern tAcctIterator	*Bank_Iterator(int FlagMask, int FlagValues,
	int Flags, int MinMaxBalance, time_t LastSeen);

/**
 * \brief Get the current entry in the iterator and move to the next
 * \param It	Iterator returned by Bank_Iterator
 * \return Accoun ID, or -1 for end of list
 */
extern int	Bank_IteratorNext(tAcctIterator *It);

/**
 * \brief Free an allocated iterator
 * \param It	Iterator returned by Bank_Iterator
 */
extern void	Bank_DelIterator(tAcctIterator *It);

/**
 * \brief Validates a user's authentication
 * \param Salt	Salt given to the client for hashing the password
 * \param Username	Username used
 * \param Password	Password sent by the client
 * \return User ID
 */
extern int	Bank_GetUserAuth(const char *Salt, const char *Username, const char *Password);

/**
 * \brief Get an account ID from a MIFARE card ID
 * \param CardID	MIFARE card ID
 * \return Account ID
 */
extern int	Bank_GetAcctByCard(const char *CardID);

/**
 * \brief Add a card to an account
 * \param AcctID	Account ID
 * \param CardID	MIFARE card ID
 * \return Boolean failure
 * \retval 0	Success
 * \retval 1	Bad account ID
 * \retval 2	Card in use
 */
extern int	Bank_AddAcctCard(int AcctID, const char *CardID);

// ---
// Server provided helper functions
// ---
/**
 * \brief Create a formatted string on the heap
 * \param Format	Format string
 * \return Heap string
 */
extern char	*mkstr(const char *Format, ...);

/**
 * \brief Dispense log access
 * \note Try not to over-use, only stuff that matters should go in here
 */
extern void	Log_Info(const char *Format, ...);

#endif
