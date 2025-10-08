Read these documents:

/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/docs/BM1398_PROTOCOL.md

/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/docs/PATTERN_TEST.md

Here is the bmminer binary, with Ghidra, and IDA Pro decompilations and assembly files.

/home/danielsokil/Downloads/Bitmain_Peek/S19_Pro/Antminer-S19-Pro-merge-release-20221226124238/Antminer S19 Pro/zynq7007_NBP1901/update/minerfs.no_header.image_extract/\_ghidra/bins/bmminer-2f464d0989b763718a6fbbdee35424ae

/home/danielsokil/Downloads/Bitmain_Peek/S19_Pro/Antminer-S19-Pro-merge-release-20221226124238/Antminer S19 Pro/zynq7007_NBP1901/update/minerfs.no_header.image_extract/usr/bin

Here is also single_board_test, the Bitmain test fixture tool used to test a hashboard, and perform pattern testing
When single_board_test is ran, it assumes the connected hashboard is already receiving power, however in our case we need to power on the PSU as bmminer does, and then power off once done testing.

/home/danielsokil/Downloads/Bitmain_Test_Fixtures/S19_Pro

Here is the single_board_test output saved in a log:

/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/docs/single_board_test_pt1.log

We also have the binary_ninja_mcp MCP server available with single_board_test and bmminer binaries loaded and ready for analysis.

Here are also logs from bmminer that may be useful to understand how it works:

/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/docs/bmminer_s19pro_68_7C_2E_2F_A4_D9.log

And bmminer debug logs:

/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/docs/bmminer_s19pro_68_7C_2E_2F_A4_D9_debug.log

We are reimplementing bmminer and single_board_test, here is our current source code:
/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/hashsource_x19/include/bm1398_asic.h
/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/hashsource_x19/src/bm1398_asic.c
/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/hashsource_x19/src/chain_test.c
/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/hashsource_x19/src/pattern_test.c
/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/hashsource_x19/src/work_test.c

The goal is to get at least one hashboard hashing, chain 0, use the docs we have and the Bitmain binaries to achieve that goal.

I need you to analyze our correct source code, and figure out what details we are missing, compared to the Bitmain binaries.
You also need to verify every detail, FPGA registers, initialization code, work generation, pattern testing code compared to BM1398_PROTOCOL doc, PATTERN_TEST doc, and bmminer, single_board_test binaries.

These programs have been verified to be working, use them as reference to understand fan and PSU controls.
Verify in our pattern test program and work submission that the PSU is enabled and powered on properly, otherwise the hashboard and ASICs won't respond.

/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/hashsource_x19/src/fan_test.c
/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/hashsource_x19/src/psu_test.c

We have the Bitmain Test Fixture deployed to the HashSource test machine:
Here are the files that are deployed:
/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/br2_external_bitmain/board/x19_xilinx/rootfs-overlay/root/test_fixture/Config.ini

/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/br2_external_bitmain/board/x19_xilinx/rootfs-overlay/root/test_fixture/run_debug.sh

/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/br2_external_bitmain/board/x19_xilinx/rootfs-overlay/root/test_fixture/run.sh

/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/br2_external_bitmain/board/x19_xilinx/rootfs-overlay/root/test_fixture/single_board_test

When you run the run_debug.sh script, it runs single_board_test with the fpga_logger, that will log all the fpga registers to a file in /tmp on the machine, this is very useful to understand how Bitmain's tools are communicating with the chips. single_board_test runs a PT1 test, however we could configure it to run a PT2 test in the Config.ini file. Also, when running single_board_test, it does not automatically exit, so you need to wait 1 minute, and then kill the process.

Don't create new documents with your analysis, fix our code directly.
When writing code, the code should be concise, pragmatic, maintainable, idiomatic, modern, type-safe, secure and performant.

Once you make your corrections, rebuild the code, and deploy to the HashSource machine and test it.
/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/docs/TEST_MACHINES.md

We are using buildroot to build the firmware, use the ARM32 compiler to compile the code, check the Makefile to understand how to rebuild the code:
/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/Makefile
/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/hashsource_x19/Makefile
