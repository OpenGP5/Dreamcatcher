#!/bin/bash
# Checks on the computer, no pedal: the sound engine at every setting's extremes and at random (finite, bounded
# output, exact bypass), and the screen code with sanitizers, rendering mock-ups to build/screens.png.
set -euo pipefail
cd "$(dirname "$0")"
P=opengp5/platform
mkdir -p build
cc -O2 -std=c99 -Wall -Wextra -fno-builtin -fsanitize=address,undefined -I $P/dsp dsp/test/dream_host.c $P/dsp/gp5_math.c \
   -o build/dream_host
build/dream_host
cc -std=c99 -O1 -Wall -Wextra -fsanitize=address,undefined -I $P/nds32 -I nds32 nds32/test/ui_mock.c nds32/controls.c \
   nds32/ui.c nds32/firmware.c -lz -o build/ui_mock
build/ui_mock build/screens.png
echo "screens: build/screens.png"
