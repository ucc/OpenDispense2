#!/bin/bash
set -eu
TESTNAME=basic

. _common.sh

TRY_COMMAND "$DISPENSE acct ${USER}"
TRY_COMMAND "$DISPENSE pseudo:0"

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
