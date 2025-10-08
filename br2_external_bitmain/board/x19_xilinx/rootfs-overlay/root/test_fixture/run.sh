#!/bin/sh
#
# Run single_board_test with test fixture shim
# No LCD or SD Card needed!
#

# Run directly from current directory
LD_PRELOAD=/root/test_fixture/test_fixture_shim.so /root/test_fixture/single_board_test
