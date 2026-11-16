#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -pthread src/main.cpp src/http_server.cpp src/bank_service.cpp src/fraud_engine.cpp -o aurora_bank
exec ./aurora_bank "$PWD"
