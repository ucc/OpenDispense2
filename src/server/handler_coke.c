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
#include "../common/config.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <modbus/modbus.h>
#include <errno.h>

#define MIN_DISPENSE_PERIOD	2
#define COKE_RECONNECT_RATELIMIT	2

// === CONSTANTS ===
const int	ciCoke_MinPeriod = 5;
const int	ciCoke_DropBitBase = 1024;
const int	ciCoke_StatusBitBase = 16;

// === IMPORTS ===

// === PROTOTYPES ===
 int	Coke_InitHandler();
 int	Coke_CanDispense(int User, int Item);
 int	Coke_DoDispense(int User, int Item);
 int	Coke_int_ConnectToPLC(void);
 int	Coke_int_GetSlotFromItem(int Item, int bDispensing);
 int	Coke_int_IsSlotEmpty(int Slot);
 int	Coke_int_DropSlot(int Slot);
static int	_ReadBit(int BitNum, uint8_t *Value);
static int	_WriteBit(int BitNum, uint8_t Value);

// === GLOBALS ===
tHandler	gCoke_Handler = {
	"coke",
	Coke_InitHandler,
	Coke_CanDispense,
	Coke_DoDispense
};
// - Config
const char	*gsCoke_ModbusAddress = "130.95.13.73";
 int		giCoke_ModbusPort = 502;
bool	gbCoke_DummyMode = false;
// - State
modbus_t	*gCoke_Modbus;
time_t	gtCoke_LastDispenseTime;
time_t	gtCoke_LastReconnectTime;
 int	giCoke_NextCokeSlot = 0;

// == CODE ===
int Coke_InitHandler()
{
	// Configuable dummy/blank mode (all dispenses succeed)
	// TODO: Find a better way of handling missing/invalid options
	Config_GetValue_Bool("coke_dummy_mode", &gbCoke_DummyMode);

	// Open modbus
	if( !gbCoke_DummyMode )
	{
		Coke_int_ConnectToPLC();
	}

	return 0;
}

int Coke_CanDispense(int UNUSED(User), int Item)
{
	 int	slot;
	
	// Check for 'dummy' mode
	if( gbCoke_DummyMode )
		return 0;

	// Get slot
	slot = Coke_int_GetSlotFromItem(Item, 0);
	if(slot < 0)	return -1;

	return Coke_int_IsSlotEmpty(slot);
}

/**
 * \brief Actually do a dispense from the coke machine
 */
int Coke_DoDispense(int UNUSED(User), int Item)
{
	 int	slot;
	// Check for 'dummy' mode
	if( gbCoke_DummyMode )
		return 0;

	// Get slot
	slot = Coke_int_GetSlotFromItem(Item, 1);
	if(slot < 0)	return -1;
	
	// Make sure there are not two dispenses within n seconds
	if( time(NULL) - gtCoke_LastDispenseTime < ciCoke_MinPeriod )
	{
		 int	delay = ciCoke_MinPeriod - (time(NULL) - gtCoke_LastDispenseTime);
		Debug_Debug("Waiting for %i seconds (rate limit)", delay);
		sleep( delay );
		Debug_Debug("wait done");
	}
	gtCoke_LastDispenseTime = time(NULL);

	return Coke_int_DropSlot(slot);
}

// --- INTERNAL FUNCTIONS ---
int Coke_int_ConnectToPLC(void)
{
	// Ratelimit
	time_t elapsed = time(NULL) - gtCoke_LastReconnectTime;
	if( elapsed < COKE_RECONNECT_RATELIMIT ) {
		Debug_Notice("Not reconnecting, only %llis have pased, %i limit", (long long)elapsed, COKE_RECONNECT_RATELIMIT);
		errno = EAGAIN;
		return -1;
	}

	if( !gCoke_Modbus )
	{
		gCoke_Modbus = modbus_new_tcp(gsCoke_ModbusAddress, giCoke_ModbusPort);
		if( !gCoke_Modbus )
		{
			perror("coke - modbus_new_tcp");
			gtCoke_LastReconnectTime = time(NULL);
			return 1;
		}
	}
	else {
		// Preven resource leaks
		modbus_close(gCoke_Modbus);
	}
	Debug_Notice("Connecting to coke PLC machine on '%s':%i", gsCoke_ModbusAddress, giCoke_ModbusPort);
	
	if( modbus_connect(gCoke_Modbus) )
	{
		gtCoke_LastReconnectTime = time(NULL);
		perror("coke - modbus_connect");
		modbus_free(gCoke_Modbus);
		gCoke_Modbus = NULL;
		return 1;
	}

	return 0;
}

int Coke_int_GetSlotFromItem(int Item, int bDispensing)
{
	if( Item < 0 || Item > 6 )	return -1;

	// Non-coke slots
	if( Item < 6 )
		return Item;
	
	// Iterate though coke slots and find the first one with a drink avaliable
	// `giCoke_NextCokeSlot` ensures that the slots rotate
	for( int i = 0; i < 4; i ++ )
	{
		int slot = 6 + (i + giCoke_NextCokeSlot) % 4;
		if( !Coke_int_IsSlotEmpty(slot) )
		{
			if(bDispensing) {
				giCoke_NextCokeSlot ++;
				if(giCoke_NextCokeSlot == 4)	giCoke_NextCokeSlot = 0;
			}
			return slot;	// Drink avaliable
		}
	}

	// Coke is empty!
	// - Return 6, even if it's empty, the checks elsewhere will avoid problems
	return 6;
}

int Coke_int_IsSlotEmpty(int Slot)
{
	uint8_t status;

	if( Slot < 0 || Slot > 9 )	return -1;

	errno = 0;
	if( _ReadBit(ciCoke_StatusBitBase + Slot, &status) )
	{
		perror("Coke_int_IsSlotEmpty - modbus_read_bits");
		return -2;
	}

	return status == 0;
}

int Coke_int_DropSlot(int Slot)
{
	uint8_t res;

	if(Slot < 0 || Slot > 9)	return -1;

	// Check if a dispense is in progress
	if( _ReadBit(ciCoke_DropBitBase + Slot, &res) )
	{
		perror("Coke_int_DropSlot - modbus_read_bits#1");
		return -2;
	}
	if( res != 0 )
	{
		// Manual dispense in progress
		return -1;
	}

	// Dispense
	if( _WriteBit(ciCoke_DropBitBase + Slot, 1) )
	{
		perror("Coke_int_DropSlot - modbus_write_bit");
		return -2;
	}

	// Check that it started
	usleep(1000);	// 1ms
	if( _ReadBit(ciCoke_DropBitBase + Slot, &res) )
	{
		perror("Coke_int_DropSlot - modbus_read_bits#2");
		return -2;
	}
	if( res == 0 )
	{
		// Oops!, no drink
		Log_Error("Drink dispense failed, bit lowered too quickly");
		Debug_Notice("Drink dispense failed, bit lowered too quickly");
		return 1;
	}
	
	return 0;
}

int _ReadBit(int BitNum, uint8_t *Value)
{
	errno = 0;
	if( !gCoke_Modbus && Coke_int_ConnectToPLC() )
		return -1;
	if( modbus_read_bits( gCoke_Modbus, BitNum, 1, Value) >= 0 )
		return 0;
	if( Coke_int_ConnectToPLC() )
		return -1;
	if( modbus_read_bits( gCoke_Modbus, BitNum, 1, Value) >= 0 )
		return 0;
	return -1;
}

int _WriteBit(int BitNum, uint8_t Value)
{
	errno = 0;
	if( !gCoke_Modbus && Coke_int_ConnectToPLC() )
		return -1;
	if( modbus_write_bit( gCoke_Modbus, BitNum, Value != 0 ) >= 0 )
		return 0;
	// Error case
	if( Coke_int_ConnectToPLC() )
		return -1;
	if( modbus_write_bit( gCoke_Modbus, BitNum, Value != 0 ) >= 0 )
		return 0;
	return -1;
}

