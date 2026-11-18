#!/bin/bash
# Checks on the computer, no pedal: the sound engine at every setting's extremes and at random (finite, bounded
# output, exact bypass), and the screen code with sanitizers, rendering mock-ups to build/screens.png.
set -euo pipefail
export ASAN_OPTIONS=abort_on_error=0      # a memory error is reported and fails the test, without a crash report
cd "$(dirname "$0")"
P=${OPENGP5:-opengp5}/platform                 # OPENGP5=<an OpenGP5 checkout>: test against another platform
mkdir -p build
cc -O2 -std=c99 -Wall -Wextra -fno-builtin -fsanitize=address,undefined -I "$P"/dsp dsp/test/dream_host.c "$P"/dsp/gp5_math.c \
   -o build/dream_host
cc -O2 -std=c99 -Wall -Wextra -fno-builtin -fsanitize=address,undefined -I "$P"/dsp dsp/test/hall_test.c "$P"/dsp/gp5_math.c -o build/hall_test
build/hall_test
build/dream_host
cc -std=c99 -O1 -Wall -Wextra -fsanitize=address,undefined -DGP5_API1 -DGP5_HAS_PARAMS -DGP5_CTL_SIDE -I "$P"/nds32 -I "$P"/dsp -I . -I nds32 \
   nds32/test/ui_mock.c nds32/controls.c "$P"/nds32/text.c nds32/ui.c nds32/firmware.c -lz -o build/ui_mock
build/ui_mock build/screens.png
cc -std=c99 -O1 -Wall -Wextra -fsanitize=address,undefined -I nds32 nds32/test/presets_test.c nds32/controls.c -o build/presets_test
build/presets_test
cc -std=c99 -O1 -Wall -Wextra -fsanitize=address,undefined -I "$P"/nds32 -I "$P"/dsp -I nds32 nds32/test/engine_test.c -o build/engine_test
build/engine_test
echo "screens: build/screens.png"
