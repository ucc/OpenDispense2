/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * config.c - Configuration file parser
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "doregex.h"
#include <regex.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE_LEN	128

// === TYPES ===
typedef struct sConfigValue	tConfigValue;
typedef struct sConfigKey	tConfigKey;

// === STRUCTURES ===
struct sConfigValue
{
	tConfigValue	*Next;
	char	Data[];
};

struct sConfigKey
{
	tConfigKey	*NextKey;
	tConfigValue	*FirstValue;
	tConfigValue	*LastValue;
	 int	ValueCount;
	char	KeyName[];
};

// === PROTOTYPES ===
//int	Config_ParseFile(const char *Filename);
void	Config_AddValue(const char *Key, const char *Value);
void	Config_int_AddValueToKey(tConfigKey *Key, const char *Value);
tConfigKey	*Config_int_GetKey(const char *KeyName, int bCreate);
 int	Config_GetValueCount(const char *KeyName);
const char	*Config_GetValue(const char *KeyName, int Index);

// === GLOBALS ===
tConfigKey	*gConfig;

// === CODE ===
int Config_ParseFile(const char *Filename)
{
	char	line[MAX_LINE_LEN];
	regex_t	regexp_option;
	regex_t	regexp_empty;

	CompileRegex(&regexp_option, "^\\s*([^ \t]+)\\s+(.+)$", REG_EXTENDED);	//
	CompileRegex(&regexp_empty, "^\\s*$", REG_EXTENDED);	//
	
	FILE *fp = fopen(Filename, "r");
	if(!fp) {
		fprintf(stderr, "Unable to open config file '%s'\n", Filename);
		perror("Config_ParseFile");
		return 1;
	}
	
	while( fgets(line, sizeof(line), fp) )
	{
		regmatch_t	matches[3];

		// Trim and clean up
		{
			 int	i;
			for( i = 0; line[i]; i ++ )
			{
				if( line[i] == '#' || line[i] == ';' ) {
					line[i] = '\0';
					break;
				}
			}
			
			while( i --, isspace(line[i]) )
				line[i] = 0;
		}
		
				
		if( regexec(&regexp_empty, line, 1, matches, 0) == 0 )
			continue ;

		if( RunRegex(&regexp_option, line, 3, matches, "Parsing configuration file") )
		{
			fprintf(stderr, "Syntax error\n- %s", line);
			continue ;
		}
		
		line[matches[1].rm_eo] = 0;
		line[matches[2].rm_eo] = 0;
	
		Config_AddValue(line + matches[1].rm_so, line + matches[2].rm_so);
	}
	
	fclose(fp);
	regfree(&regexp_option);
	regfree(&regexp_empty);

	return 0;
}

void Config_AddValue(const char *Key, const char *Value)
{
	tConfigKey	*key;
	
	// Find key (creating if needed)
	key = Config_int_GetKey(Key, 1);

	Config_int_AddValueToKey(key, Value);	
}

void Config_int_AddValueToKey(tConfigKey *Key, const char *Value)
{
	tConfigValue	*newVal;
	// Create value
	newVal = malloc(sizeof(tConfigValue) + strlen(Value) + 1);
	newVal->Next = NULL;
	strcpy(newVal->Data, Value);
	
	#if 1
	// Add to the end of the key's list
	if(Key->LastValue)
		Key->LastValue->Next = newVal;
	else
		Key->FirstValue = newVal;
	Key->LastValue = newVal;
	#else
	// Add to the start of the key's list
	if(Key->LastValue == NULL)
		Key->LastValue = newVal;
	newVal->Next = Key->FirstValue;
	Key->FirstValue = newVal;
	#endif
	Key->ValueCount ++;
}

/**
 * \brief 
 */
tConfigKey *Config_int_GetKey(const char *KeyName, int bCreate)
{
	tConfigKey	*key, *prev = NULL;
	
	// Search the sorted list of keys
	for( key = gConfig; key; prev = key, key = key->NextKey )
	{
		int cmp = strcmp(key->KeyName, KeyName);
		if(cmp == 0)	return key;	// Equal, return
		if(cmp > 0)	break;	// Greater, fast exit
	}
	
	if( bCreate )
	{
		// Create new key
		key = malloc(sizeof(tConfigKey) + strlen(KeyName) + 1);
		key->FirstValue = NULL;
		key->LastValue = NULL;
		key->ValueCount = 0;
		strcpy(key->KeyName, KeyName);
		
		// Append
		if(prev) {
			key->NextKey = prev->NextKey;
			prev->NextKey = key;
		}
		else {
			key->NextKey = gConfig;
			gConfig = key;
		}
	}
	else
	{
		key = NULL;
	}
	
	return key;
}

int Config_GetValueCount(const char *KeyName)
{
	tConfigKey	*key = Config_int_GetKey(KeyName, 0);
	if(!key)	return 0;
	
	return key->ValueCount;
}

const tConfigValue *Config_int_GetValue(const char *KeyName, int Index)
{
	tConfigValue	*val;	

	tConfigKey *key = Config_int_GetKey(KeyName, 0);
	if(!key) {
		fprintf(stderr, "Unknown key '%s'\n", KeyName);
		exit(1);
		return NULL;
	}
	
	if(Index < 0 || Index >= key->ValueCount)	return NULL;
	
	for( val = key->FirstValue; Index && val; val = val->Next, Index -- );

	ASSERT(val != NULL);
	return val;
}

const char *Config_GetValue_Idx(const char *KeyName, int Index)
{
	const tConfigValue* val = Config_int_GetValue(KeyName, Index);
	if( !val )	return NULL;
	return val->Data;
}

bool Config_GetValue_Str(const char *KeyName, const char** Value)
{
	const tConfigValue* val = Config_int_GetValue(KeyName, 0);
	if(!val)	return false;
	*Value = val->Data;
	return true;
}
bool str_to_bool(const char *val)
{
	if( atoi(val) == 1 )	return 1;
	if( val[0] == '0' && val[1] == '\0' )	return 0;
	
	if( strcasecmp(val, "true") == 0 )	return 1;
	if( strcasecmp(val, "false") == 0 )	return 0;
	
	if( strcasecmp(val, "yes") == 0 )	return 1;
	if( strcasecmp(val, "no") == 0 )	return 0;

	// INVALID, TODO: Error message
	return 0;
}
bool Config_GetValue_Bool(const char *KeyName, bool* Value)
{
	const tConfigValue* val = Config_int_GetValue(KeyName, 0);
	if(!val)	return false;
	
	*Value = str_to_bool(val->Data);
	
	return true;
}

bool Config_GetValue_Int(const char *KeyName, int* Value)
{
	const tConfigValue* val = Config_int_GetValue(KeyName, 0);
	if(!val)	return false;
	
	char* end;
	 int	value = strtol(val->Data, &end, 0);
	if( *end != '\0' ) {
		return false;
	}
	*Value = value;
	
	return true;
}

