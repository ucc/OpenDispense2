/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * itemdb.c - Dispense Item Databse
 *
 * This file is licenced under the 3-clause BSD Licence. See the file COPYING
 * for full details.
 */
#include <stdlib.h>
#include <stdio.h>
#include "common.h"

// === GLOBALS ===
 int	giNumItems = 0;
tItem	*gaItems = NULL;
tHandler	*gaHandlers = NULL;

// === CODE ===
