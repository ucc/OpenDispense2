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
int DispenseItem(int User, int Item)
{
	 int	ret;
	tItem	*item;
	tHandler	*handler;
	char	*username;
	
	// Sanity check please?
	if(Item < 0 || Item >= giNumItems)
		return -1;
	
	// Get item pointers
	item = &gaItems[Item];
	handler = item->Handler;
	
	// Check if the dispense is possible
	ret = handler->CanDispense( User, item->ID );
	if(!ret)	return ret;
	
	// Subtract the balance
	ret = Transfer( User, GetUserID(">sales"), item->Price, "" );
	// What value should I use for this error?
	// AlterBalance should return the final user balance
	if(ret == 0)	return 1;
	
	// Get username for debugging
	username = GetUserName(User);
	
	// Actually do the dispense
	ret = handler->DoDispense( User, item->ID );
	if(ret) {
		Log_Error("Dispense failed after deducting cost (%s dispensing %s - %ic)",
			username, item->Name, item->Price);
		Transfer( GetUserID(">sales"), User, item->Price, "rollback" );
		free( username );
		return 1;
	}
	
	// And log that it happened
	Log_Info("Dispensed %s (%i:%i) for %s [cost %i, balance %i cents]",
		item->Name, handler->Name, item->ID,
		username, item->Price, GetBalance(User)
		);
	
	free( username );
	return 0;
}
