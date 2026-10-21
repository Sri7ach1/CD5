#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf -- "$TEST_DIR"' EXIT
for test in recovery device snapshot discid manifest unjail; do
    python3 "tests/test_${test}.py"
done
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Isrc tests/test_toc.c src/toc.c -o "$TEST_DIR/toc"
"$TEST_DIR/toc"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Isrc -Ithird_party/jsmn tests/test_metadata.c src/metadata.c -o "$TEST_DIR/metadata"
"$TEST_DIR/metadata"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Isrc -Ithird_party/stb tests/test_cover.c -lm -o "$TEST_DIR/cover"
"$TEST_DIR/cover"
bash -n scripts/*.sh tools/setup-packaging-dependencies.sh
if [[ ${1:-} == --sdk ]]; then
    # Compile Linux host tests with only the SDK curl headers, not its FreeBSD
    # system headers. All HTTP operations are mocked; no network requests.
    docker run --rm -v "$ROOT":/work:ro -w /work "${CD5_SDK_IMAGE:-ps5-payload-sdk}" sh -eu -c '
        mkdir /tmp/test-include
        cp -r /opt/ps5-payload-sdk/target/include/curl /tmp/test-include/
        clang-18 -std=c11 -Wall -Wextra -Werror -I/tmp/test-include tests/test_net.c -o /tmp/test-net
        /tmp/test-net
    '
fi
