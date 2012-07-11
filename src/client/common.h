/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 * - Dispense Client
 *
 * common.h
 * - Shared definitions
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
#ifndef _CLIENT__COMMON_H_
#define _CLIENT__COMMON_H_

#include <regex.h>

typedef struct sItem {
	char	*Type;
	 int	ID;
	 int	Status;	// 0: Availiable, 1: Sold out, -1: Error
	char	*Desc;
	 int	Price;
}	tItem;

enum eUI_Modes
{
	UI_MODE_BASIC,	// Non-NCurses
	UI_MODE_STANDARD,
	UI_MODE_DRINKSONLY,
	UI_MODE_ALL,
	NUM_UI_MODES
};

enum eReturnValues
{
	RV_SUCCESS,
	RV_BAD_ITEM,
	RV_INVALID_USER,
	RV_PERMISSIONS,
	RV_ARGUMENTS,
	RV_BALANCE,
	RV_SERVER_ERROR,	// Generic for 5xx codes
	RV_UNKNOWN_ERROR = -1,
	RV_SOCKET_ERROR = -2,
	RV_UNKNOWN_RESPONSE = -3,
};

extern regex_t	gArrayRegex;
extern regex_t	gItemRegex;
extern regex_t	gSaltRegex;
extern regex_t	gUserInfoRegex;
extern regex_t	gUserItemIdentRegex;

extern int	gbDryRun;
extern int	gbDisallowSelectWithoutBalance;
extern int	giMinimumBalance;
extern int	giMaximumBalance;
extern enum eUI_Modes	giUIMode;

extern int	gbIsAuthenticated;
extern char	*gsEffectiveUser;
extern char	*gsUserName;
extern int	giUserBalance;
extern char	*gsUserFlags;

extern int	giNumItems;
extern tItem	*gaItems;

extern int	RunRegex(regex_t *regex, const char *str, int nMatches, regmatch_t *matches, const char *errmsg);

extern int	ShowNCursesUI(void);

extern int	OpenConnection(const char *Host, int Port);
extern int	Authenticate(int Socket);
extern int	GetUserBalance(int Socket);
extern void	PopulateItemList(int Socket);
extern int	Dispense_ItemInfo(int Socket, const char *Type, int ID);
extern int	DispenseItem(int Socket, const char *Type, int ID);
extern int	Dispense_AlterBalance(int Socket, const char *Username, int Ammount, const char *Reason);
extern int	Dispense_SetBalance(int Socket, const char *Username, int Balance, const char *Reason);
extern int	Dispense_Give(int Socket, const char *Username, int Ammount, const char *Reason);
extern int	Dispense_Refund(int Socket, const char *Username, const char *Item, int PriceOverride);
extern int	Dispense_Donate(int Socket, int Ammount, const char *Reason);
extern int	Dispense_EnumUsers(int Socket);
extern int	Dispense_ShowUser(int Socket, const char *Username);
extern void	_PrintUserLine(const char *Line);
extern int	Dispense_AddUser(int Socket, const char *Username);
extern int	Dispense_SetUserType(int Socket, const char *Username, const char *TypeString, const char *Reason);
extern int	Dispense_SetItem(int Socket, const char *Type, int ID, int NewPrice, const char *NewName);

#endif

