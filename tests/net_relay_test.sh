#!/bin/bash
# Starts the relay on a free port, runs net_relay_test against it, stops it.
set -e
relay="$1"; test="$2"
port=$(( 20000 + $$ % 20000 ))
node "$relay" --port "$port" > /dev/null &
pid=$!
trap 'kill $pid 2>/dev/null' EXIT
for i in 1 2 3 4 5 6 7 8 9 10; do
  (exec 3<>/dev/tcp/127.0.0.1/$port) 2>/dev/null && break
  sleep 0.2
done
"$test" "ws://127.0.0.1:$port/"
