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

// === CONSTANTS ===
#define	DEFAULT_CONFIG_FILE	"/etc/opendispense/main.cfg"

// === HELPER MACROS ===
#define _EXPSTR(x)	#x
#define EXPSTR(x)	_EXPSTR(x)

// === STRUCTURES ===
typedef struct sItem	tItem;
struct sItem
{
	char	*Name;	//!< Display Name
	 int	Price;	//!< Price
	
	short	Type;	//!< References an action
	short	ID;	//!< Item ID
};

typedef struct sUser	tUser;
struct sUser
{
	 int	ID;		//!< User ID (LDAP ID)
	 int	Balance;	//!< Balance in cents
	 int	Bytes;	//!< Traffic Usage
	char	Name[];	//!< Username
};

typedef struct sHandler	tHandler;
struct sHandler
{
	char	*Name;
	 int	(*CanDispense)(int User, int ID);
	 int	(*DoDispense)(int User, int ID);
};

// === GLOBALS ===
extern tItem	*gaItems;
extern int	giNumItems;
extern tHandler	*gaHandlers;
extern int	giDebugLevel;

// === FUNCTIONS ===
// --- Logging ---
extern void	Log_Error(const char *Format, ...);
extern void	Log_Info(const char *Format, ...);

// --- Cokebank Functions ---
extern int	AlterBalance(int User, int Ammount);
extern int	GetBalance(int User);
extern char	*GetUserName(int User);
extern int	GetUserID(const char *Username);

#endif
