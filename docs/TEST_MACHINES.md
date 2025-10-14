# Test Machines

## 1

-   HashSource
-   192.168.1.36
-   SSH User/Pass: root/root

## 2

-   Bitmain Stock Firmware
-   192.168.1.35
-   SSH User/Pass: miner/miner
-   If you need root/sudo access, check this document for the unlock: `/home/danielsokil/Lab/HashSource/hashsource_antminer_x19/scripts/bitmain_ramdisk_install.sh`

Use sshpass when connecting with password, it's installed
And these SSH/SCP flags: `-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null`

When using SCP, use the legacy flag:
`sshpass -p 'root' scp -O -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null`
