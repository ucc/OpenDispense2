/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * cokebank.c - Coke-Bank management
 *
 * This file is licenced under the 3-clause BSD Licence. See the file COPYING
 * for full details.
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include "common.h"
#if USE_LDAP
# include <ldap.h>
#endif

// === HACKS ===
#define HACK_TPG_NOAUTH	1
#define HACK_ROOT_NOAUTH	1

// === PROTOTYPES ===
void	Init_Cokebank(const char *Argument);
 int	Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason);
 int	GetBalance(int User);
char	*GetUserName(int User);
 int	GetUserID(const char *Username);
 int	GetMaxID(void);
 int	GetUserAuth(const char *Salt, const char *Username, const char *PasswordString);
#if USE_LDAP
char	*ReadLDAPValue(const char *Filter, char *Value);
#endif
void	HexBin(uint8_t *Dest, int BufSize, const char *Src);

// === GLOBALS ===
FILE	*gBank_LogFile;
#if USE_LDAP
char	*gsLDAPPath = "ldapi:///";
LDAP	*gpLDAP;
#endif

// === CODE ===
/**
 * \brief Load the cokebank database
 */
void Init_Cokebank(const char *Argument)
{
	#if USE_LDAP
	 int	rv;
	#endif
	
	// Open Cokebank
	gBank_File = fopen(Argument, "rb+");
	if( !gBank_File ) {
		gBank_File = fopen(Argument, "wb+");
	}
	if( !gBank_File ) {
		perror("Opening coke bank");
	}

	// Open log file
	// TODO: Do I need this?
	gBank_LogFile = fopen("cokebank.log", "a");
	if( !gBank_LogFile )	gBank_LogFile = stdout;

	// Read in cokebank
	fseek(gBank_File, 0, SEEK_END);
	giBank_NumUsers = ftell(gBank_File) / sizeof(gaBank_Users[0]);
	fseek(gBank_File, 0, SEEK_SET);
	gaBank_Users = malloc( giBank_NumUsers * sizeof(gaBank_Users[0]) );
	fread(gaBank_Users, sizeof(gaBank_Users[0]), giBank_NumUsers, gBank_File);
	
	#if USE_LDAP
	// Connect to LDAP
	rv = ldap_create(&gpLDAP);
	if(rv) {
		fprintf(stderr, "ldap_create: %s\n", ldap_err2string(rv));
		exit(1);
	}
	rv = ldap_initialize(&gpLDAP, gsLDAPPath);
	if(rv) {
		fprintf(stderr, "ldap_initialize: %s\n", ldap_err2string(rv));
		exit(1);
	}
	{ int ver = LDAP_VERSION3; ldap_set_option(gpLDAP, LDAP_OPT_PROTOCOL_VERSION, &ver); }
	# if 0
	rv = ldap_start_tls_s(gpLDAP, NULL, NULL);
	if(rv) {
		fprintf(stderr, "ldap_start_tls_s: %s\n", ldap_err2string(rv));
		exit(1);
	}
	# endif
	{
		struct berval	cred;
		struct berval	*servcred;
		cred.bv_val = "secret";
		cred.bv_len = 6;
		rv = ldap_sasl_bind_s(gpLDAP, "cn=root,dc=ucc,dc=gu,dc=uwa,dc=edu,dc=au",
			"", &cred, NULL, NULL, &servcred);
		if(rv) {
			fprintf(stderr, "ldap_start_tls_s: %s\n", ldap_err2string(rv));
			exit(1);
		}
	}
	#endif
}

/**
 * \brief Transfers money from one user to another
 * \param SourceUser	Source user
 * \param DestUser	Destination user
 * \param Ammount	Ammount of cents to move from \a SourceUser to \a DestUser
 * \param Reason	Reason for the transfer (essentially a comment)
 * \return Boolean failure
 */
int Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason)
{
	 int	srcBal = Bank_GetUserBalance(SourceUser);
	 int	dstBal = Bank_GetUserBalance(DestUser);
	
	if( srcBal - Ammount < Bank_GetMinAllowedBalance(SourceUser) )
		return 1;
	if( dstBal + Ammount < Bank_GetMinAllowedBalance(DestUser) )
		return 1;
	Bank_AlterUserBalance(DestUser, Ammount);
	Bank_AlterUserBalance(SourceUser, -Ammount);
	fprintf(gBank_LogFile, "ACCT #%i{%i} -= %ic [to #%i] (%s)\n", SourceUser, srcBal, Ammount, DestUser, Reason);
	fprintf(gBank_LogFile, "ACCT #%i{%i} += %ic [from #%i] (%s)\n", DestUser, dstBal, Ammount, SourceUser, Reason);
	return 0;
}

int GetFlags(int User)
{
	return Bank_GetUserFlags(User);
}

int SetFlags(int User, int Mask, int Flags)
{
	return Bank_SetUserFlags(User, Mask, Flags);
}

/**
 * \brief Get the balance of the passed user
 */
int GetBalance(int User)
{
	return Bank_GetUserBalance(User);;
}

/**
 * \brief Return the name the passed user
 */
char *GetUserName(int User)
{
	return Bank_GetUserName(User);
}

/**
 * \brief Get the User ID of the named user
 */
int GetUserID(const char *Username)
{
	return Bank_GetUserByName(Username);
}

int CreateUser(const char *Username)
{
	 int	ret;
	
	ret = Bank_GetUserByName(Username);
	if( ret != -1 )	return -1;
	
	return Bank_AddUser(Username);
}

int GetMaxID(void)
{
	return giBank_NumUsers;
}

/**
 * \brief Authenticate a user
 * \return User ID, or -1 if authentication failed
 */
int GetUserAuth(const char *Salt, const char *Username, const char *PasswordString)
{
	#if USE_LDAP
	uint8_t	hash[20];
	uint8_t	h[20];
	 int	ofs = strlen(Username) + strlen(Salt);
	char	input[ ofs + 40 + 1];
	char	tmp[4 + strlen(Username) + 1];	// uid=%s
	char	*passhash;
	#endif
	
	#if HACK_TPG_NOAUTH
	if( strcmp(Username, "tpg") == 0 )
		return GetUserID("tpg");
	#endif
	#if HACK_ROOT_NOAUTH
	if( strcmp(Username, "root") == 0 ) {
		int ret = GetUserID("root");
		if( ret == -1 )
			return CreateUser("root");
		return ret;
	}
	#endif
	
	#if USE_LDAP
	HexBin(hash, 20, PasswordString);
	
	// Build string to hash
	strcpy(input, Username);
	strcpy(input, Salt);
	
	// TODO: Get user's SHA-1 hash
	sprintf(tmp, "uid=%s", Username);
	printf("tmp = '%s'\n", tmp);
	passhash = ReadLDAPValue(tmp, "userPassword");
	if( !passhash ) {
		return -1;
	}
	printf("LDAP hash '%s'\n", passhash);
	
	sprintf(input+ofs, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
		h[ 0], h[ 1], h[ 2], h[ 3], h[ 4], h[ 5], h[ 6], h[ 7], h[ 8], h[ 9],
		h[10], h[11], h[12], h[13], h[14], h[15], h[16], h[17], h[18], h[19]
		);
	// Then create the hash from the provided salt
	// Compare that with the provided hash

	# if 1
	{
		 int	i;
		printf("Password hash ");
		for(i=0;i<20;i++)
			printf("%02x", hash[i]&0xFF);
		printf("\n");
	}
	# endif
	
	#endif
	
	return -1;
}

#if USE_LDAP
char *ReadLDAPValue(const char *Filter, char *Value)
{
	LDAPMessage	*res, *res2;
	struct berval **attrValues;
	char	*attrNames[] = {Value,NULL};
	char	*ret;
	struct timeval	timeout;
	 int	rv;
	
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	
	rv = ldap_search_ext_s(gpLDAP, "", LDAP_SCOPE_BASE, Filter,
		attrNames, 0, NULL, NULL, &timeout, 1, &res
		);
	printf("ReadLDAPValue: rv = %i\n", rv);
	if(rv) {
		fprintf(stderr, "LDAP Error reading '%s' with filter '%s'\n%s\n",
			Value, Filter,
			ldap_err2string(rv)
			);
		return NULL;
	}
	
	res2 = ldap_first_entry(gpLDAP, res);
	attrValues = ldap_get_values_len(gpLDAP, res2, Value);
	
	ret = strndup(attrValues[0]->bv_val, attrValues[0]->bv_len);
	
	ldap_value_free_len(attrValues);
	
	
	return ret;
}
#endif

// TODO: Move to another file
void HexBin(uint8_t *Dest, int BufSize, const char *Src)
{
	 int	i;
	for( i = 0; i < BufSize; i ++ )
	{
		uint8_t	val = 0;
		
		if('0' <= *Src && *Src <= '9')
			val |= (*Src-'0') << 4;
		else if('A' <= *Src && *Src <= 'F')
			val |= (*Src-'A'+10) << 4;
		else if('a' <= *Src && *Src <= 'f')
			val |= (*Src-'a'+10) << 4;
		else
			break;
		Src ++;
		
		if('0' <= *Src && *Src <= '9')
			val |= (*Src-'0');
		else if('A' <= *Src && *Src <= 'F')
			val |= (*Src-'A'+10);
		else if('a' <= *Src && *Src <= 'f')
			val |= (*Src-'a'+10);
		else
			break;
		Src ++;
		
		Dest[i] = val;
	}
	for( ; i < BufSize; i++ )
		Dest[i] = 0;
}

