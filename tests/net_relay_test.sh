#!/bin/bash
# Starts the relay on a free port, runs net_relay_test against it, stops it.
set -e
. "$(dirname "$0")/net_lib.sh"
relay="$1"; test="$2"
port=$(( 20000 + $$ % 20000 ))
node "$relay" --port "$port" > /dev/null &
pid=$!
trap 'kill $pid 2>/dev/null' EXIT
wait_port "$port"
"$test" "ws://127.0.0.1:$port/"
