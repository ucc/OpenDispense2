#!/bin/bash
set -eu
TESTNAME=basic

. _common.sh

# (CI runs as root, and root is auto-added by databse creation)
if [[ "x$USER" != "xroot" ]]; then
	# Ensure that the databse doesn't already contain the current user
	# - 1. Protects against running the test against the production database
	# - 2. Allows adding the current user as an admin
	if $DISPENSE acct "$USER"; then
		FAIL "Database contains '$USER'"
	fi
	# Add the current user as an admin
	sqlite3 "${BASEDIR}cokebank.db" "INSERT INTO accounts (acct_name,acct_is_admin,acct_uid) VALUES ('${USER}',1,1);"
else
	if $DISPENSE acct accmurph; then
		FAIL "Database contains 'accmurph' (running as root, might be using prodution)"
	fi
fi

# Try to add a new user
TRY_COMMAND "$DISPENSE user add unittest_user0"

# Ensure that the add worked
LOG "Checking for test user"
TRY_COMMAND $DISPENSE acct unittest_user0 | grep ': $    0.00'

# Manipulate user's balance
TRY_COMMAND $DISPENSE acct unittest_user0 +100 Unit_test
TRY_COMMAND $DISPENSE acct unittest_user0 | grep ': $    1.00'
TRY_COMMAND $DISPENSE acct unittest_user0 -100 Unit_test
TRY_COMMAND $DISPENSE acct unittest_user0 | grep ': $    0.00'
LOG "Success"
