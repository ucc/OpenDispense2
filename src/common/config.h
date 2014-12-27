/*
 * OpenDispense2
 *
 * This code is published under the terms of the Acess licence.
 * See the file COPYING for details.
 *
 * config.h - Config Header
 */
#ifndef _CONFIG_H_
#define _CONFIG_H_

// === HELPER MACROS ===
#define _EXPSTR(x)      #x
#define EXPSTR(x)       _EXPSTR(x)

#define ASSERT(cnd) do{if(!(cnd)){fprintf(stderr, "ASSERT failed at "__FILE__":"EXPSTR(__LINE__)" - "EXPSTR(cnd)"\n");exit(-1);}}while(0)



// --- Config Database ---
extern void	Config_ParseFile(const char *Filename);
extern void	Config_AddValue(const char *Key, const char *Value);
extern int	Config_GetValueCount(const char *KeyName);
extern const char	*Config_GetValue(const char *KeyName, int Index);
extern int	Config_GetValue_Bool(const char *KeyName, int Index);
extern int	Config_GetValue_Int(const char *KeyName, int Index);

#endif
