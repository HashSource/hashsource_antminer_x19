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

echo "Deploying pattern parser and pattern files..."
${SSH} 'mkdir -p /root/test_fixture/BM1398-pattern'

# Deploy pattern_parser utility
echo "Copying pattern_parser utility..."
${SCP} ~/Lab/HashSource/hashsource_antminer_x19/hashsource_x19/bin/pattern_parser ${USER}@${HOST}:/root/test_fixture/
${SSH} 'chmod +x /root/test_fixture/pattern_parser'
echo "  pattern_parser deployed to /root/test_fixture/"

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

    # Test pattern_parser
    echo ""
    echo "Testing pattern_parser utility..."
    echo "Running: /root/test_fixture/pattern_parser /root/test_fixture/BM1398-pattern/btc-asic-000.bin -s"
    echo ""
    ${SSH} '/root/test_fixture/pattern_parser /root/test_fixture/BM1398-pattern/btc-asic-000.bin -s'
else
    echo "ERROR: No pattern files found in ${PATTERN_SRC}"
    exit 1
fi

echo ""
echo "Deployment complete!"
echo ""
echo "Deployed files:"
echo "  - /root/test_fixture/pattern_parser (utility)"
echo "  - /root/test_fixture/BM1398-pattern/btc-asic-000.bin (565 KB)"
echo "  - /root/test_fixture/BM1398-pattern/btc-asic-001.bin (565 KB)"
echo ""
