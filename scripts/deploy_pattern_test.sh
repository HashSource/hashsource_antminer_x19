#!/usr/bin/bash
#
# deploy BM1398 pattern tests
#

set -e

# Config
HOST="192.168.1.27"
USER="root"
PASS="root"
PATTERN_SRC="/home/danielsokil/Downloads/Bitmain_Test_Fixtures/S19_Pro/BM1398-pattern"

SSH="sshpass -p ${PASS} ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ${USER}@${HOST}"
SCP="sshpass -p ${PASS} scp -O -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"

echo "Deploying pattern files..."
${SSH} 'mkdir -p /root/test_fixture/BM1398-pattern'

# Count pattern files
PATTERN_COUNT=$(ls -1 ${PATTERN_SRC}/*.bin 2>/dev/null | wc -l)
echo "Found ${PATTERN_COUNT} pattern files in ${PATTERN_SRC}"

# Deploy only 2 pattern files to save space
if [ ${PATTERN_COUNT} -gt 0 ]; then
    echo "Copying only 2 pattern files (to save space)..."
    ${SCP} ${PATTERN_SRC}/btc-asic-000.bin ${USER}@${HOST}:/root/test_fixture/BM1398-pattern/
    ${SCP} ${PATTERN_SRC}/btc-asic-001.bin ${USER}@${HOST}:/root/test_fixture/BM1398-pattern/
    echo "  btc-asic-000.bin and btc-asic-001.bin copied"

    # Verify deployment
    echo "Verifying deployment..."
    ${SSH} 'ls -lh /root/test_fixture/BM1398-pattern/*.bin | wc -l' | xargs -I {} echo "  {} files deployed successfully"
else
    echo "ERROR: No pattern files found in ${PATTERN_SRC}"
    exit 1
fi

echo ""
echo "Deployment complete!"
