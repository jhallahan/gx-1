#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SEQ-4 host tests: the maths, the platform pieces kept from Felucca / X0X / AMB-1 / IND-1 / LCD-4 / CRT-4, the
# flash (the settings' objects and the patterns' log), and the whole firmware app in the simulator
# (tests/scenarios/*.s4: the steps, the six modes, recording, patterns, songs, MIDI in, the menus), with
# screenshots and MIDI logs in build/scenarios/.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
FAIL=0
OUT=build/host
mkdir -p "$OUT" build/scenarios
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run fastmath sh -c "$CC -O2 -ffp-contract=off -Wall -Wextra -Werror -o $OUT/fastmath_test tests/host/fastmath_test.c -lm && $OUT/fastmath_test"
run encoder sh -c "$CC -O2 -w -Ifirmware/hal -o $OUT/encoder_test tests/host/encoder_test.c && $OUT/encoder_test"
run trs sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/trs_test tests/host/trs_test.c && $OUT/trs_test"
run storage sh -c "$CC -O2 -o $OUT/storage_test tests/storage_test.c && $OUT/storage_test"
run logstore sh -c "$CC -O2 -Wall -Wextra -o $OUT/logstore_test tests/logstore_test.c && $OUT/logstore_test"
# the update path, against the firmware package (Felucca's tests; needs ./build.sh)
if [ -f build/seq4.fwsc ]; then
    run ota-entry sh -c "$CC -o $OUT/ota_test tests/ota_test.c && $OUT/ota_test build/seq4.fwsc"
    if [ -z "${AC79_SDK:-}" ]; then
        echo "  skip update-loader (needs AC79_SDK, as the build)"
    else
    run update-loader sh -c "head -c 100000 build/seq4.bin > $OUT/old_app.bin && \
        python3 tools/fm1pkg_make.py $OUT/old_app.bin build/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -o $OUT/ldr_test tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc build/seq4.fwsc"
    fi
    run installer python3 tests/install_test.py
else
    echo "  skip update-path tests (no build/seq4.fwsc: run ./build.sh)"
fi
run host-build sh host/build_host.sh
if command -v emcc >/dev/null 2>&1; then
    run emu sh -c "sh web/emu/build.sh >/dev/null && node tests/host/emu_test.mjs build/emu/seq4.wasm"
fi
# GX-1: what the firmware plays against what the genre engine rendered (several genres, seeds, parts, maps)
run gx-parity sh tests/gx_parity.sh
for s in tests/scenarios/*.s4; do
    n=$(basename "$s" .s4)
    mkdir -p "build/scenarios/$n"
    run "scenario-$n" build/host/seq4_host "$s" "build/scenarios/$n"
done
# the same scenarios under the address and undefined-behaviour sanitizers: a word access that is not word
# aligned works on this machine but faults on the FM-1 (0.1's crash changing patterns on sequencers 2 and 4)
if $CC -fsanitize=address,undefined -x c -o "$OUT/san_probe" - >/dev/null 2>&1 <<'C'
int main(void) { return 0; }
C
then
    run sanitized-build sh -c "$CC -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
        -ffp-contract=off -std=c99 -w -DEN_HOST -DGX_HIST=16 -Ifirmware/src -Ifirmware/src/seq -Ifirmware/src/gx -Ibuild/gen \
        -o $OUT/seq4_host_san host/seq4_host.c firmware/src/seq/seq4.c firmware/src/gx/gx_rng.c firmware/src/gx/gx_engine.c \
        firmware/src/gx/gx_drums.c firmware/src/gx/gx_pitched.c firmware/src/gx/gx_mutate.c firmware/src/gx/gx_profiles_gen.c -lm"
    for s in tests/scenarios/*.s4; do
        n=$(basename "$s" .s4)
        mkdir -p "build/scenarios/san-$n"
        run "sanitized-$n" "$OUT/seq4_host_san" "$s" "build/scenarios/san-$n"
    done
else
    echo "  skip sanitized scenarios (this compiler has no -fsanitize=address,undefined)"
fi
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
