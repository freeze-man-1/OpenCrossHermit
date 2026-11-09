#!/bin/bash
# Starts a fake Apinator cluster (tests/fake_apinator.mjs) and the channel
# signer (tools/netserver/apinator-auth.mjs) on free ports, then runs
# net_relay_test twice with no relay: once signing in the game (a built-in
# secret), once through the signer (an auth URL).
set -e
. "$(dirname "$0")/net_lib.sh"
fake="$1"; auth="$2"; test="$3"
port=$(( 20000 + $$ % 20000 )); aport=$(( port + 1 ))
node "$fake" --port "$port" --key app_test --secret s3cret > /dev/null &
fpid=$!
APINATOR_KEY=app_test APINATOR_SECRET=s3cret node "$auth" --port "$aport" > /dev/null &
apid=$!
trap 'kill $fpid $apid 2>/dev/null' EXIT
wait_port "$port"
wait_port "$aport"
"$test" "apinator:ws://127.0.0.1:$port|app_test|s3cret"
"$test" "apinator:ws://127.0.0.1:$port|app_test|http://127.0.0.1:$aport/"
