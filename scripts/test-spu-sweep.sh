#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/harness.c" <<'C'
#include "spu_sweep.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void expect(int condition, const char *message)
{
    if (!condition)
        fail(message);
}

static void clock_n(SpuSweep *sweep, int count)
{
    for (int i = 0; i < count; i++)
        spu_sweep_clock(sweep);
}

static int16_t legacy_fixed_volume(uint16_t raw)
{
    int32_t value = raw & 0x7FFFu;
    if (value & 0x4000)
        value -= 0x8000;
    return (int16_t)value;
}

static void test_fixed_mix_equivalence(void)
{
    static const int16_t samples[] = {
        INT16_MIN, -12345, -1, 0, 1, 12345, INT16_MAX
    };
    SpuSweep sweep;

    /* v0.1.11 changes the internal representation from signed volume/2 with
     * a >>14 mix to the SPU's full signed current volume with a >>15 mix.
     * Every fixed-volume control value must retain the old audible gain. */
    for (uint32_t raw = 0; raw < 0x8000u; raw++) {
        spu_sweep_reset(&sweep);
        spu_sweep_write_control(&sweep, (uint16_t)raw);
        spu_sweep_clock(&sweep);
        const int32_t current = spu_sweep_read_current(&sweep);
        const int32_t legacy = legacy_fixed_volume((uint16_t)raw);

        for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
            const int32_t old_mix = ((int32_t)samples[i] * legacy) >> 14;
            const int32_t new_mix = ((int32_t)samples[i] * current) >> 15;
            if (old_mix != new_mix)
                fail("fixed-volume mixer equivalence");
        }
    }
}

int main(void)
{
    SpuSweep sweep;

    spu_sweep_reset(&sweep);
    expect(spu_sweep_read_current(&sweep) == 0, "power-on current volume");

    /* Fixed mode is applied on the next 44.1 kHz SPU cycle. */
    spu_sweep_write_control(&sweep, 0x3FFFu);
    expect(spu_sweep_read_current(&sweep) == 0,
           "fixed control must retain current until clock");
    spu_sweep_clock(&sweep);
    expect(spu_sweep_read_current(&sweep) == 32766,
           "positive fixed volume");

    spu_sweep_write_control(&sweep, 0x4000u);
    spu_sweep_clock(&sweep);
    expect(spu_sweep_read_current(&sweep) == INT16_MIN,
           "negative fixed volume phase");

    test_fixed_mix_equivalence();

    /* Switching to sweep mode must start from the live current volume rather
     * than misreading the low rate bits as a tiny direct gain. */
    spu_sweep_write_control(&sweep, 0x2000u);
    spu_sweep_clock(&sweep);
    expect(spu_sweep_read_current(&sweep) == 0x4000,
           "sweep starting level setup");
    spu_sweep_write_control(&sweep, 0x8000u);
    expect(spu_sweep_read_current(&sweep) == 0x4000,
           "sweep control must preserve live volume");
    spu_sweep_clock(&sweep);
    expect(spu_sweep_read_current(&sweep) > 0x4000,
           "linear positive increase");

    /* Linear decrease trends to and clamps at zero. */
    spu_sweep_write_control(&sweep, 0x3000u);
    spu_sweep_clock(&sweep);
    expect(spu_sweep_read_current(&sweep) == 0x6000,
           "decrease starting level setup");
    spu_sweep_write_control(&sweep, 0xA000u);
    {
        const int16_t before = spu_sweep_read_current(&sweep);
        spu_sweep_clock(&sweep);
        expect(spu_sweep_read_current(&sweep) < before,
               "linear decrease direction");
    }
    clock_n(&sweep, 16);
    expect(spu_sweep_read_current(&sweep) == 0,
           "linear decrease zero clamp");

    /* Negative phase is signed inversion, not a small positive volume. */
    spu_sweep_write_current(&sweep, 0);
    spu_sweep_write_control(&sweep, 0x9000u);
    spu_sweep_clock(&sweep);
    expect(spu_sweep_read_current(&sweep) < 0,
           "negative-phase increase");

    /* Slow rates accumulate divider state instead of stepping every sample. */
    spu_sweep_write_current(&sweep, 0x1000);
    spu_sweep_write_control(&sweep, 0x807Eu);
    {
        const int16_t before = spu_sweep_read_current(&sweep);
        spu_sweep_clock(&sweep);
        expect(spu_sweep_read_current(&sweep) == before,
               "slow-rate first cycle must not step");
        expect(sweep.divider != 0, "slow-rate divider accumulation");
    }

    puts("PASS: SPU fixed gain, sweep preservation, direction, phase, and rate");
    return 0;
}
C

cc -std=c99 -Wall -Wextra -Werror \
  -I"$ROOT/psxrecomp/runtime/include" \
  "$TMP/harness.c" \
  "$ROOT/psxrecomp/runtime/src/spu_sweep.c" \
  -o "$TMP/harness"

"$TMP/harness"
