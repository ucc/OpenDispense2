/*
 * OpenDispense2
 *
 * This code is published under the terms of the Acess licence.
 * See the file COPYING for details.
 *
 * common.h - Core Header
 */
#ifndef _COMMON_H_
#define _COMMON_H_

#include <regex.h>
#include "../cokebank.h"

// === CONSTANTS ===
#define	DEFAULT_CONFIG_FILE	"/etc/opendispense/main.cfg"
#define	DEFAULT_ITEM_FILE	"/etc/opendispense/items.cfg"

// === HELPER MACROS ===

#define UNUSED(var)    unused__##var __attribute__((__unused__))

// === STRUCTURES ===
typedef struct sItem	tItem;
typedef struct sUser	tUser;
typedef struct sConfigItem	tConfigItem;
typedef struct sHandler	tHandler;

struct sItem
{
	char	*Name;	//!< Display Name
	 int	Price;	//!< Price
	 int	bHidden;	//!< Hidden item?
	 int	bDisabledi;	//!< Disabled Item
	
	tHandler	*Handler;	//!< Handler for the item
	short	ID;	//!< Item ID
};

struct sUser
{
	 int	ID;		//!< User ID (LDAP ID)
	 int	Balance;	//!< Balance in cents
	 int	Bytes;	//!< Traffic Usage
	char	Name[];	//!< Username
};

struct sConfigItem
{
	char	*Name;
	char	*Value;
};

struct sHandler
{
	char	*Name;
	 int	(*Init)(int NConfig, tConfigItem *Config);
	/**
	 * \brief Check if an item can be dispensed
	 * \return Boolean Failure
	 */
	 int	(*CanDispense)(int User, int ID);
	 int	(*DoDispense)(int User, int ID);
};

// === GLOBALS ===
extern tItem	*gaItems;
extern int	giNumItems;
extern tHandler	*gaHandlers[];
extern int	giNumHandlers;
extern int	giDebugLevel;
extern int	gbNoCostMode;

// === FUNCTIONS ===
extern void	Items_UpdateFile(void);

// --- Helpers --
extern void	StartPeriodicThread(void);
extern void	AddPeriodicFunction(void (*Fcn)(void));
extern void	CompileRegex(regex_t *Regex, const char *Pattern, int Flags);
extern int	RunRegex(regex_t *regex, const char *string, int nMatches, regmatch_t *matches, const char *errorMessage);
extern int	InitSerial(const char *Path, int BaudRate);
extern char	*mkstr(const char *Format, ...);

// --- Dispense ---
extern int	DispenseItem(int ActualUser, int User, tItem *Item);
extern int	DispenseRefund(int ActualUser, int DestUser, tItem *Item, int OverridePrice);
extern int	DispenseGive(int ActualUser, int SrcUser, int DestUser, int Ammount, const char *ReasonGiven);
extern int	DispenseAdd(int ActualUser, int User, int Ammount, const char *ReasonGiven);
extern int	DispenseSet(int ActualUser, int User, int Balance, const char *ReasonGiven, int *OrigBalance);
extern int	DispenseDonate(int ActualUser, int User, int Ammount, const char *ReasonGiven);
extern int	DispenseUpdateItem(int User, tItem *Item, const char *NewName, int NewPrice);

// --- Logging ---
// to syslog
extern void	Log_Error(const char *Format, ...);
extern void	Log_Info(const char *Format, ...);
// To stdout
#define Debug_Notice(msg, v...)	fprintf(stderr, "%08llun: "msg"\n", (unsigned long long)time(NULL) ,##v)
#define Debug_Debug(msg, v...)	fprintf(stderr, "%08llud: "msg"\n", (unsigned long long)time(NULL) ,##v)

#endif
