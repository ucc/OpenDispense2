/**
 */
#include "common.h"
#include <stdlib.h>

// === CODE ===
int DispenseItem(int User, int Item)
{
	 int	ret;
	tItem	*item;
	tHandler	*handler;
	char	*username;
	
	if(Item < 0 || Item >= giNumItems)
		return -1;
	
	item = &gaItems[Item];
	handler = &gaHandlers[ item->Type ];
	
	username = GetUserName(User);
	
	ret = handler->CanDispense( User, item->ID );
	if(!ret)	return ret;
	
	ret = AlterBalance( User, -item->Price );
	// What value should I use for this error?
	// AlterBalance should return the final user balance
	if(ret == 0)	return 1;
	
	ret = handler->DoDispense( User, item->ID );
	if(ret) {
		Log_Error("Dispense failed after deducting cost (%s dispensing %s - %ic)",
			username, item->Name, item->Price);
		AlterBalance( User, item->Price );
		free( username );
		return 1;
	}
	
	Log_Info("Dispensed %s (%i:%i) for %s [cost %i, balance %i cents]",
		item->Name, item->Type, item->ID,
		username, item->Price, GetBalance(User)
		);
	
	free( username );
	return 0;
}
