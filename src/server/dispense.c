/**
 */
#include "common.h"
#include <stdlib.h>

// === CODE ===
/**
 * \brief Dispense an item for a user
 * 
 * The core of the dispense system, I kinda like it :)
 */
int DispenseItem(int ActualUser, int User, tItem *Item)
{
	 int	ret;
	tHandler	*handler;
	char	*username, *actualUsername;
	char	*reason;
	
	handler = Item->Handler;
	
	// Check if the dispense is possible
	if( handler->CanDispense ) {
		ret = handler->CanDispense( User, Item->ID );
		if(ret)	return 1;	// 1: Unable to dispense
	}

	// Subtract the balance
	reason = mkstr("Dispense - %s:%i %s", handler->Name, Item->ID, Item->Name);
	if( !reason )	reason = Item->Name;	// TODO: Should I instead return an error?
	ret = Bank_Transfer( User, Bank_GetAcctByName(COKEBANK_SALES_ACCT), Item->Price, reason);
	free(reason);
	if(ret)	return 2;	// 2: No balance
	
	// Get username for debugging
	username = Bank_GetAcctName(User);
	
	// Actually do the dispense
	if( handler->DoDispense ) {
		ret = handler->DoDispense( User, Item->ID );
		if(ret) {
			Log_Error("Dispense failed after deducting cost (%s dispensing %s - %ic)",
				username, Item->Name, Item->Price);
			Bank_Transfer( Bank_GetAcctByName(COKEBANK_SALES_ACCT), User, Item->Price, "rollback" );
			free( username );
			return -1;	// 1: Unkown Error again
		}
	}
	
	actualUsername = Bank_GetAcctName(ActualUser);
	
	// And log that it happened
	Log_Info("dispense '%s' (%s:%i) for %s by %s [cost %i, balance %i cents]",
		Item->Name, handler->Name, Item->ID,
		username, actualUsername, Item->Price, Bank_GetBalance(User)
		);
	
	free( username );
	free( actualUsername );
	return 0;	// 0: EOK
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
	
	ret = Bank_Transfer( SrcUser, DestUser, Ammount, ReasonGiven );
	if(ret)	return 2;	// No Balance
	
	
	srcName = Bank_GetAcctName(SrcUser);
	dstName = Bank_GetAcctName(DestUser);
	actualUsername = Bank_GetAcctName(ActualUser);
	
	Log_Info("give %i to %s from %s by %s (%s) [balances %i, %i]",
		Ammount, dstName, srcName, actualUsername, ReasonGiven,
		Bank_GetBalance(SrcUser), Bank_GetBalance(DestUser)
		);
	
	free(srcName);
	free(dstName);
	free(actualUsername);
	
	return 0;
}

/**
 * \brief Add money to an account
 */
int DispenseAdd(int User, int ByUser, int Ammount, const char *ReasonGiven)
{
	 int	ret;
	char	*dstName, *byName;
	
	ret = Bank_Transfer( Bank_GetAcctByName(COKEBANK_DEBT_ACCT), User, Ammount, ReasonGiven );
	if(ret)	return 2;
	
	byName = Bank_GetAcctName(ByUser);
	dstName = Bank_GetAcctName(User);
	
	Log_Info("add %i to %s by %s (%s) [balance %i]",
		Ammount, dstName, byName, ReasonGiven, Bank_GetBalance(User)
		);
	
	free(byName);
	free(dstName);
	
	return 0;
}
