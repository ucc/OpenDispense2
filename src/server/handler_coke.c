/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * handler_coke.c - Coke controller code
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 *
 * NOTES:
 * - Remember, the coke machine echoes your text back to you!
 */
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <modbus/modbus.h>

#define MIN_DISPENSE_PERIOD	5

// === CONSTANTS ===
const int	ciCoke_MinPeriod = 5;
const int	ciCoke_DropBitBase = 0;
const int	ciCoke_StatusBitBase = 0;

// === IMPORTS ===

// === PROTOTYPES ===
 int	Coke_InitHandler();
 int	Coke_CanDispense(int User, int Item);
 int	Coke_DoDispense(int User, int Item);

// === GLOBALS ===
tHandler	gCoke_Handler = {
	"coke",
	Coke_InitHandler,
	Coke_CanDispense,
	Coke_DoDispense
};
const char	*gsCoke_ModbusAddress = "130.95.13.73";
modbus_t	*gCoke_Modbus;
time_t	gtCoke_LastDispenseTime;
 int	gbCoke_DummyMode = 1;

// == CODE ===
int Coke_InitHandler()
{
	// Configuable dummy/blank mode (all dispenses succeed)
	// TODO: Find a better way of handling missing/invalid options
	if( Config_GetValueCount("coke_dummy_mode") > 0 )
	{
		gbCoke_DummyMode = Config_GetValue_Bool("coke_dummy_mode", 0);
		if(gbCoke_DummyMode == -1)	gbCoke_DummyMode = 0;
	}

	// Open modbus
	if( !gbCoke_DummyMode )
	{
		printf("Connecting to coke machine on '%s'\n", gsCoke_ModbusAddress);
		
		modbus_new_tcp(gsCoke_ModbusAddress, 502);
		if( !gCoke_Modbus )
		{
			perror("coke - modbus_new_tcp");
		}
		
		if( gCoke_Modbus && modbus_connect(gCoke_Modbus) )
		{
			perror("coke - modbus_connect");
			modbus_free(gCoke_Modbus);
			gCoke_Modbus = NULL;
		}
	}

	return 0;
}

int Coke_CanDispense(int UNUSED(User), int Item)
{
	uint8_t	status;
	// Sanity please
	if( Item < 0 || Item > 6 )	return -1;

	// Check for 'dummy' mode
	if( gbCoke_DummyMode )
		return 0;
	
	// Can't dispense if the machine is not connected
	if( !gCoke_Modbus )
		return -2;

	if( modbus_read_bits(gCoke_Modbus, ciCoke_StatusBitBase + Item, 1, &status) )
	{
		// TODO: Check for a connection issue
	}

	return status == 0;
}

/**
 * \brief Actually do a dispense from the coke machine
 */
int Coke_DoDispense(int UNUSED(User), int Item)
{
	// Sanity please
	if( Item < 0 || Item > 6 )	return -1;

	// Check for 'dummy' mode
	if( gbCoke_DummyMode )
		return 0;
	
	// Can't dispense if the machine is not connected
	if( !gCoke_Modbus )
		return -2;

	// Make sure there are not two dispenses within n seconds
	if( time(NULL) - gtCoke_LastDispenseTime < ciCoke_MinPeriod )
	{
		 int	delay = ciCoke_MinPeriod - (time(NULL) - gtCoke_LastDispenseTime);
		printf("Wait %i seconds?\n", delay);
		sleep( delay );
		printf("wait done\n");
	}
	
	// Dispense
	if( modbus_write_bit(gCoke_Modbus, ciCoke_DropBitBase + Item, 1) )
	{
		// TODO: Handle connection issues
	}
	
	return 0;
}

