#!/bin/bash
# Starts a fake Apinator cluster (tests/fake_apinator.mjs) and the channel
# signer (tools/netserver/apinator-auth.mjs) on free ports, then runs
# net_relay_test twice with no relay: once signing in the game (a built-in
# secret), once through the signer (an auth URL). When a run fails, what the
# two servers printed is shown with it.
. "$(dirname "$0")/net_lib.sh"
fake="$1"; auth="$2"; test="$3"
port=$(( 20000 + $$ % 20000 )); aport=$(( port + 1 ))
logs=$(mktemp -d)
node "$fake" --port "$port" --key app_test --secret s3cret > "$logs/fake.log" 2>&1 &
fpid=$!
APINATOR_KEY=app_test APINATOR_SECRET=s3cret node "$auth" --port "$aport" > "$logs/auth.log" 2>&1 &
apid=$!
trap 'kill $fpid $apid 2>/dev/null; rm -rf "$logs"' EXIT
wait_port "$port"
wait_port "$aport"
run() {
  local t0=$SECONDS
  "$test" "$2" && return 0
  echo "net_apinator_test: the run $1 failed after $(( SECONDS - t0 )) s (servers up after $t0 s)"
  for pid in $fpid $apid; do kill -0 $pid 2>/dev/null || echo "net_apinator_test: server process $pid is not running"; done
  echo "--- fake apinator (port $port)"; tail -n 40 "$logs/fake.log"
  echo "--- signer (port $aport)"; tail -n 40 "$logs/auth.log"
  exit 1
}
run "signing in the game" "apinator:ws://127.0.0.1:$port|app_test|s3cret"
run "signing through the signer" "apinator:ws://127.0.0.1:$port|app_test|http://127.0.0.1:$aport/"
