# Shared by the net tests' scripts (sourced).
# wait_port PORT: waits up to 10 s for a listener on 127.0.0.1:PORT. It
# never fails: a server that did not start shows as the test's failure.
wait_port() {
  for i in $(seq 1 50); do
    (exec 3<>/dev/tcp/127.0.0.1/"$1") 2>/dev/null && return 0
    sleep 0.2
  done
  return 0
}
