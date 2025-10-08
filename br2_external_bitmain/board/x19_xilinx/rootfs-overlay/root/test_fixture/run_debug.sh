#!/bin/sh
#
# Run single_board_test with FPGA register logging (RECOMMENDED)
#
# Uses fpga_logger to capture ALL FPGA register changes
#

echo "Starting FPGA register logger..."

# Start fpga_logger in background
fpga_logger --no-restart /tmp/fpga_registers.log > /dev/null &
LOGGER_PID=$!

# Give logger time to initialize
sleep 2

echo "Logger PID: $LOGGER_PID"
echo "Logs: /tmp/fpga_registers.log"
echo ""
echo "Starting test..."
echo ""

# Run single_board_test with LD_PRELOAD shim (LCD/GPIO emulation)
LD_PRELOAD=/root/test_fixture/test_fixture_shim.so /root/test_fixture/single_board_test

echo ""
echo "Stopping logger..."

# Stop the logger
kill -INT "$LOGGER_PID" 2>/dev/null
wait "$LOGGER_PID" 2>/dev/null

echo ""
echo "Logs saved to: /tmp/fpga_registers.log"
echo "View with: cat /tmp/fpga_registers.log"
