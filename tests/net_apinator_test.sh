#!/bin/bash
# Starts a fake Apinator cluster (tests/fake_apinator.mjs) and the channel
# signer (tools/netserver/apinator-auth.mjs) on free ports, then runs
# net_relay_test twice with no relay: once signing in the game (a built-in
# secret), once through the signer (an auth URL).
set -e
fake="$1"; auth="$2"; test="$3"
port=$(( 20000 + $$ % 20000 )); aport=$(( port + 1 ))
node "$fake" --port "$port" --key app_test --secret s3cret > /dev/null &
fpid=$!
APINATOR_KEY=app_test APINATOR_SECRET=s3cret node "$auth" --port "$aport" > /dev/null &
apid=$!
trap 'kill $fpid $apid 2>/dev/null' EXIT
for p in $port $aport; do
  for i in 1 2 3 4 5 6 7 8 9 10; do
    (exec 3<>/dev/tcp/127.0.0.1/$p) 2>/dev/null && break
    sleep 0.2
  done
done
"$test" "apinator:ws://127.0.0.1:$port|app_test|s3cret"
"$test" "apinator:ws://127.0.0.1:$port|app_test|http://127.0.0.1:$aport/"
