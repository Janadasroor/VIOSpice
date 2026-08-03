#!/usr/bin/env bash
# Run CTest with retry policy for flaky tests.
#
# Non-flaky tests are fail-fast (no retries) so real regressions surface
# immediately. Tests tagged with the 'flaky' label are allowed up to three
# attempts; a flaky test must stay flaky for only as long as its root cause
# is being worked on and should be un-tagged once fixed.
#
# Usage: ci_ctest.sh <ctest args...>    (e.g. --preset unit -R "schematic")
set -u

if [ "$#" -eq 0 ]; then
    echo "usage: ci_ctest.sh <ctest args...>" >&2
    exit 2
fi

ctest "$@" -LE flaky
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "ci_ctest.sh: a non-flaky test failed (exit $rc); not retrying." >&2
    exit "$rc"
fi

flaky_count=$(ctest "$@" -N -L flaky 2>/dev/null | awk '/Total Tests:/{print $3}' | head -n1)
if [ "${flaky_count:-0}" != "0" ]; then
    echo "ci_ctest.sh: running flaky-tagged tests with up to 3 attempts..."
    ctest "$@" -L flaky --repeat until-pass:3
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "ci_ctest.sh: a flaky-tagged test failed after retries (exit $rc)." >&2
    fi
    exit "$rc"
fi

exit 0
