#!/bin/bash
set -eux
TESTNAME=basic

. _common.sh

if $DISPENSE acct tpg; then
	FAIL "Database contains '$USER'"
fi

sqlite3 "${BASEDIR}cokebank.db" "INSERT INTO accounts (acct_name,acct_is_admin,acct_uid) VALUES ('${USER}',1,1);"

TRY_COMMAND "$DISPENSE user add unittest_user0"

LOG "Checking for test user"
TRY_COMMAND $DISPENSE acct unittest_user0 | grep ': $    0.00'

TRY_COMMAND $DISPENSE acct unittest_user0 +100 Unit_test
TRY_COMMAND $DISPENSE acct unittest_user0 | grep ': $    1.00'
TRY_COMMAND $DISPENSE acct unittest_user0 -100 Unit_test
TRY_COMMAND $DISPENSE acct unittest_user0 | grep ': $    0.00'
LOG "Success"
