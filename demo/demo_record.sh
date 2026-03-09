#!/bin/bash
# Automated demo script — run inside asciinema rec.
# Usage: asciinema rec demo/demo.cast -c "bash demo/demo_record.sh"

set -e
cd "$(dirname "$0")/.."

clear
echo "┌─────────────────────────────────────────────────────────────┐"
echo "│         Order Matching Engine  —  FIX 4.2 Live Demo         │"
echo "│   C++17  •  epoll TCP server  •  256 tests  •  ~2M ops/s    │"
echo "└─────────────────────────────────────────────────────────────┘"
echo ""
sleep 1.5

echo "── Step 1: start the FIX 4.2 gateway ──────────────────────────"
./build/ome_main --port 9000 &
GW_PID=$!
sleep 1
echo ""

echo "── Step 2: connect two clients (SELLER and BUYER) ─────────────"
echo "   SELLER logs on and places 3 limit sells: \$150, \$151, \$152"
echo "   BUYER  logs on and sends an aggressive buy sweeping 2 levels"
echo "   Watch the ExecutionReports stream back in real time:"
echo ""
sleep 1.5

python3 demo/fix_client.py --port 9000
echo ""
sleep 1

echo "── Step 3: shut down ───────────────────────────────────────────"
kill $GW_PID 2>/dev/null || true
wait $GW_PID 2>/dev/null || true
echo "Gateway stopped cleanly."
sleep 1
