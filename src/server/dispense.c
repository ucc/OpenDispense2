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
int DispenseItem(int User, tItem *Item)
{
	 int	ret;
	tHandler	*handler;
	char	*username;
	
	handler = Item->Handler;
	
	// Check if the dispense is possible
	if( handler->CanDispense ) {
		ret = handler->CanDispense( User, Item->ID );
		if(ret)	return 1;	// 1: Unable to dispense
	}
	
	// Subtract the balance
	ret = Transfer( User, GetUserID(">sales"), Item->Price, "" );
	// What value should I use for this error?
	// AlterBalance should return the final user balance
	if(ret)	return 2;	// 2: No balance
	
	// Get username for debugging
	username = GetUserName(User);
	
	// Actually do the dispense
	if( handler->DoDispense ) {
		ret = handler->DoDispense( User, Item->ID );
		if(ret) {
			Log_Error("Dispense failed after deducting cost (%s dispensing %s - %ic)",
				username, Item->Name, Item->Price);
			Transfer( GetUserID(">sales"), User, Item->Price, "rollback" );
			free( username );
			return -1;	// 1: Unkown Error again
		}
	}
	
	// And log that it happened
	Log_Info("Dispensed %s (%i:%i) for %s [cost %i, balance %i cents]",
		Item->Name, handler->Name, Item->ID,
		username, Item->Price, GetBalance(User)
		);
	
	free( username );
	return 0;	// 0: EOK
}
