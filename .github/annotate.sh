#!/bin/bash
# annotate.sh build|test LOG: after a failed step, prints the log's error
# lines (for tests also the last 60 lines of output) as annotations, which
# can be read without signing in.
kind="$1"; log="$2"
if [ "$kind" = build ]; then
  grep -E "error|Error" "$log" | grep -v "^ *[0-9]* *Error" | sort -u | head -20 |
    while IFS= read -r l; do echo "::error title=build::$l"; done
else
  grep -E "\*\*\*|Failed|FAILED" "$log" | head -10 | while IFS= read -r l; do echo "::error title=test::$l"; done
  echo "::error title=test output::$(tail -60 "$log" | sed 's/%/%25/g' | awk '{printf "%s%%0A", $0}')"
fi
