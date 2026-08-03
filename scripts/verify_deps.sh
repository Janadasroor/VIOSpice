#!/usr/bin/env bash
# verify_deps.sh — check that every dependency pinned in deps.lock is reachable
# and (optionally) byte-for-byte intact.
#
# Default mode (used by CI): HEAD-request every prebuilt URL, assert HTTP 200,
# and confirm every git ref resolves via ls-remote. Fast, ~10 requests.
#
# --verify-hash: additionally download every prebuilt asset and compare its
#   sha256 against the pin. Slower (downloads ~170 MB) — used for release runs.
#
# Exit code 0 = all pins healthy; non-zero lists every broken pin.

set -u
LOCK="${1:-$(dirname "$0")/../deps.lock}"
VERIFY_HASH=0
[ "${1:-}" = "--verify-hash" ] && VERIFY_HASH=1 && LOCK="$(dirname "$0")/../deps.lock"

if [ ! -f "$LOCK" ]; then
    echo "ERROR: lockfile not found: $LOCK" >&2
    exit 1
fi

FAILED=0
FAILED_MSG=""
in_section=""
line_no=0

check_http() {
    local url="$1" label="$2"
    local code
    code=$(curl -fsSIL -o /dev/null -w '%{http_code}' --max-time 60 "$url" 2>/dev/null)
    if [ "$code" = "200" ]; then
        echo "OK   $label ($url)"
    else
        echo "FAIL $label (HTTP ${code:-unreachable}: $url)"
        FAILED=1
    fi
}

check_hash() {
    local url="$1" expect="$2" asset="$3" label="$4"
    local tmp dir
    tmp="$(mktemp -d)"
    dir="${tmp}/asset"
    if ! curl -fsSL --max-time 600 -o "$dir" "$url" 2>/dev/null; then
        echo "FAIL $label (download failed: $url)"
        FAILED=1
        rm -rf "$tmp"
        return
    fi
    local got
    got=$(sha256sum "$dir" | awk '{print $1}')
    if [ "$got" = "$expect" ]; then
        echo "OK   $label sha256=$got"
    else
        echo "FAIL $label sha256 mismatch (expected $expect, got $got): $url"
        FAILED=1
    fi
    rm -rf "$tmp"
}

check_git() {
    local ref="$1" repo="$2" label="$3" tmp
    tmp="$(mktemp -d)"
    if git -C "$tmp" init -q 2>/dev/null && \
       git -C "$tmp" fetch -q --depth=1 "$repo" "$ref" 2>/dev/null; then
        echo "OK   $label ref=${ref} ($repo)"
    else
        echo "FAIL $label ref=${ref} not reachable ($repo)"
        FAILED=1
    fi
    rm -rf "$tmp"
}

while IFS= read -r line || [ -n "$line" ]; do
    line_no=$((line_no + 1))
    [ -z "$line" ] && continue
    case "$line" in \#*) continue ;; esac

    if [[ "$line" =~ ^\[([a-z]+)\]$ ]]; then
        in_section="${BASH_REMATCH[1]}"
        continue
    fi

    case "$in_section" in
        prebuilt)
            read -r name tag platform asset sha url <<< "$line"
            if [ -z "${name:-}" ] || [ -z "${url:-}" ]; then
                echo "FAIL malformed prebuilt entry at line $line_no" >&2
                FAILED=1
                continue
            fi
            label="${name}@${tag}/${platform}"
            if [ "$VERIFY_HASH" = "1" ]; then
                check_hash "$url" "$sha" "$asset" "$label"
            else
                check_http "$url" "$label"
            fi
            ;;
        git)
            read -r name tag repo <<< "$line"
            if [ -z "${name:-}" ] || [ -z "${repo:-}" ]; then
                echo "FAIL malformed git entry at line $line_no" >&2
                FAILED=1
                continue
            fi
            check_git "$tag" "$repo" "${name}@${tag}"
            ;;
        *)
            echo "FAIL line $line_no before any section" >&2
            FAILED=1
            ;;
    esac
done < "$LOCK"

if [ "$FAILED" = "1" ]; then
    echo "ERROR: one or more pinned dependencies are broken — fix deps.lock or the release." >&2
    exit 1
fi
echo "All pinned dependencies verified OK."
exit 0
