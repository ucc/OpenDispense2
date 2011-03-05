/**
 */
#include "common.h"
#include <stdlib.h>
#include <limits.h>
#include <string.h>

 int	_GetMinBalance(int Account);
 int	_CanTransfer(int Source, int Destination, int Ammount);
 int	_Transfer(int Source, int Destination, int Ammount, const char *Reason);

// === CODE ===
/**
 * \brief Dispense an item for a user
 * 
 * The core of the dispense system, I kinda like it :)
 */
int DispenseItem(int ActualUser, int User, tItem *Item)
{
	 int	ret, salesAcct;
	tHandler	*handler;
	char	*username, *actualUsername;
	
	salesAcct = Bank_GetAcctByName(COKEBANK_SALES_ACCT);

	// Check if the user can afford it
	if( Item->Price && !_CanTransfer(User, salesAcct, Item->Price) )
	{
		return 2;	// 2: No balance
	}
	
	handler = Item->Handler;
	
	// KNOWN HACK: Naming a slot "dead" disables it
	if( strcmp(Item->Name, "dead") == 0 )
		return 1;
	
	// Check if the dispense is possible
	if( handler->CanDispense ) {
		ret = handler->CanDispense( User, Item->ID );
		if(ret)	return 1;	// 1: Unable to dispense
	}
	
	// Get username for debugging
	username = Bank_GetAcctName(User);
	
	// Actually do the dispense
	if( handler->DoDispense ) {
		ret = handler->DoDispense( User, Item->ID );
		if(ret) {
			Log_Error("Dispense failed (%s dispensing %s:%i '%s')",
				username, Item->Name, Item->Handler->Name, Item->ID);
			free( username );
			return -1;	// 1: Unknown Error again
		}
	}
	
	// Take away money
	if( Item->Price )
	{
		char	*reason;
		reason = mkstr("Dispense - %s:%i %s", handler->Name, Item->ID, Item->Name);
		_Transfer( User, salesAcct, Item->Price, reason );
		free(reason);
	}
	
	actualUsername = Bank_GetAcctName(ActualUser);
	
	// And log that it happened
	Log_Info("dispense '%s' (%s:%i) for %s by %s [cost %i, balance %i]",
		Item->Name, handler->Name, Item->ID,
		username, actualUsername, Item->Price, Bank_GetBalance(User)
		);
	
	free( username );
	free( actualUsername );
	return 0;	// 0: EOK
}

/**
 * \brief Refund a dispense
 */
int DispenseRefund(int ActualUser, int DestUser, tItem *Item, int OverridePrice)
{
	 int	ret;
	 int	src_acct, price;
	char	*username, *actualUsername;

	src_acct = Bank_GetAcctByName(COKEBANK_SALES_ACCT);

	if( OverridePrice > 0 )
		price = OverridePrice;
	else
		price = Item->Price;

	ret = _Transfer( src_acct, DestUser, price, "Refund");
	if(ret)	return ret;

	username = Bank_GetAcctName(DestUser);
	actualUsername = Bank_GetAcctName(ActualUser);
	
	Log_Info("refund '%s' (%s:%i) to %s by %s [cost %i, balance %i]",
		Item->Name, Item->Handler->Name, Item->ID,
		username, actualUsername, price, Bank_GetBalance(DestUser)
		);

	free(username);
	free(actualUsername);

	return 0;
}

/**
 * \brief Give money from one user to another
 */
int DispenseGive(int ActualUser, int SrcUser, int DestUser, int Ammount, const char *ReasonGiven)
{
	 int	ret;
	char	*actualUsername;
	char	*srcName, *dstName;
	
	if( Ammount < 0 )	return 1;	// Um... negative give? Not on my watch!
	
	ret = _Transfer( SrcUser, DestUser, Ammount, ReasonGiven );
	if(ret)	return 2;	// No Balance
	
	
	actualUsername = Bank_GetAcctName(ActualUser);
	srcName = Bank_GetAcctName(SrcUser);
	dstName = Bank_GetAcctName(DestUser);
	
	Log_Info("give %i from %s to %s by %s [balances %i, %i] - %s",
		Ammount, srcName, dstName, actualUsername,
		Bank_GetBalance(SrcUser), Bank_GetBalance(DestUser),
		ReasonGiven
		);
	
	free(srcName);
	free(dstName);
	free(actualUsername);
	
	return 0;
}

#if 0 // Dead Code
/**
 * \brief Move money from one user to another (Admin Only)
 */
int DispenseTransfer(int ActualUser, int SrcUser, int DestUser, int Ammount, const char *ReasonGiven)
{
	 int	ret;
	char	*actualUsername;
	char	*srcName, *dstName;

	// Make sure the user is an admin
	if( !(Bank_GetFlags(ActualUser) & USER_FLAG_ADMIN) )
		return 1;
	
	ret = _Transfer( SrcUser, DestUser, Ammount, ReasonGiven );
	if(ret)	return 2;	// No Balance
	
	
	actualUsername = Bank_GetAcctName(ActualUser);
	srcName = Bank_GetAcctName(SrcUser);
	dstName = Bank_GetAcctName(DestUser);
	
	Log_Info("move %i from %s to %s by %s [balances %i, %i] - %s",
		Ammount, srcName, dstName, actualUsername,
		Bank_GetBalance(SrcUser), Bank_GetBalance(DestUser),
		ReasonGiven
		);
	
	free(srcName);
	free(dstName);
	free(actualUsername);
	
	return 0;
}
#endif

/**
 * \brief Add money to an account
 */
int DispenseAdd(int ActualUser, int User, int Ammount, const char *ReasonGiven)
{
	 int	ret;
	char	*dstName, *byName;
	
#if DISPENSE_ADD_BELOW_MIN
	ret = _Transfer( Bank_GetAcctByName(COKEBANK_DEBT_ACCT), User, Ammount, ReasonGiven );
#else
	ret = Bank_Transfer( Bank_GetAcctByName(COKEBANK_DEBT_ACCT), User, Ammount, ReasonGiven );
#endif
	if(ret)	return 2;
	
	byName = Bank_GetAcctName(ActualUser);
	dstName = Bank_GetAcctName(User);
	
	Log_Info("add %i to %s by %s [balance %i] - %s",
		Ammount, dstName, byName, Bank_GetBalance(User), ReasonGiven
		);
	
	free(byName);
	free(dstName);
	
	return 0;
}

int DispenseSet(int ActualUser, int User, int Balance, const char *ReasonGiven)
{
	 int	curBal = Bank_GetBalance(User);
	char	*byName, *dstName;
	
	_Transfer( Bank_GetAcctByName(COKEBANK_DEBT_ACCT), User, Balance-curBal, ReasonGiven );
	
	byName = Bank_GetAcctName(ActualUser);
	dstName = Bank_GetAcctName(User);
	
	Log_Info("set balance of %s to %i by %s [balance %i] - %s",
		dstName, Balance, byName, Bank_GetBalance(User), ReasonGiven
		);
	
	free(byName);
	free(dstName);
	
	return 0;
}

/**
 * \brief Donate money to the club
 */
int DispenseDonate(int ActualUser, int User, int Ammount, const char *ReasonGiven)
{
	 int	ret;
	char	*srcName, *byName;
	
	if( Ammount < 0 )	return 2;
	
	ret = _Transfer( User, Bank_GetAcctByName(COKEBANK_DEBT_ACCT), Ammount, ReasonGiven );
	if(ret)	return 2;
	
	byName = Bank_GetAcctName(ActualUser);
	srcName = Bank_GetAcctName(User);
	
	Log_Info("donate %i from %s by %s [balance %i] - %s",
		Ammount, srcName, byName, Bank_GetBalance(User), ReasonGiven
		);
	
	free(byName);
	free(srcName);
	
	return 0;
}

int DispenseUpdateItem(int User, tItem *Item, const char *NewName, int NewPrice)
{
	char	*username;
	
	// Sanity checks
	if( NewPrice < 0 )	return 2;
	if( !Item )	return 2;
	if( strlen(NewName) < 1 )	return 2;
	
	// Update the item
	free(Item->Name);
	Item->Name = strdup(NewName);
	Item->Price = NewPrice;
	
	username = Bank_GetAcctName(User);
	
	Log_Info("item %s:%i updated to '%s' %i by %s",
		Item->Handler->Name, Item->ID,
		NewName, NewPrice, username
		);
	
	free(username);
	
	// Update item file
	Items_UpdateFile();
	
	return 0;
}

// --- Internal Functions ---
int _GetMinBalance(int Account)
{
	 int	flags = Bank_GetFlags(Account);
	
	// Evil little piece of HACK:
	// root's balance cannot be changed by any of the above functions
	// - Stops dispenses as root by returning insufficent balance.
	{
		char	*username = Bank_GetAcctName(Account);
		if( strcmp(username, "root") == 0 )
		{
			free(username);
			return INT_MAX;
		}
		free(username);
	}
	
	// - Internal accounts have no lower bound
	if( flags & USER_FLAG_INTERNAL )	return INT_MIN;
	
	// Admin to -$50
//	if( flags & USER_FLAG_ADMIN )	return -5000;
	
	// Coke to -$20
//	if( flags & USER_FLAG_COKE )	return -2000;
	
	// Anyone else, non-negative
	return 0;
}

/**
 * \brief Check if a transfer is possible
 */
int _CanTransfer(int Source, int Destination, int Ammount)
{
	if( Ammount > 0 )
	{
		if( Bank_GetBalance(Source) - Ammount < _GetMinBalance(Source) )
			return 0;
	}
	else
	{
		if( Bank_GetBalance(Destination) + Ammount < _GetMinBalance(Destination) )
			return 0;
	}
	return 1;
}

int _Transfer(int Source, int Destination, int Ammount, const char *Reason)
{
	if( !_CanTransfer(Source, Destination, Ammount) )
		return 1;
	return Bank_Transfer(Source, Destination, Ammount, Reason);
}
