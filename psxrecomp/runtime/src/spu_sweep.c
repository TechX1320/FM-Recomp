/*
 * spu_sweep.c - PlayStation SPU fixed/sweep volume envelope.
 *
 * Independent implementation of the public SPU envelope behavior documented
 * by psx-spx. The unit is intentionally standalone so fixed-gain compatibility
 * and sweep edge cases can be tested without booting a game.
 */

#include "spu_sweep.h"

typedef struct SweepDelta {
    int32_t step;
    uint16_t counter_add;
} SweepDelta;

static SweepDelta sweep_delta(uint16_t control, int16_t phase_level)
{
    const uint8_t rate = (uint8_t)(control & 0x7Fu);
    const int exponential = (control & 0x4000u) != 0;
    const int decreasing = (control & 0x2000u) != 0;
    const int negative_phase = (control & 0x1000u) != 0;
    const int reverse_step = (decreasing != negative_phase) ||
                             (decreasing && exponential);
    int32_t step = 7 - (int32_t)(rate & 3u);
    uint32_t counter_add = 0x8000u;
    SweepDelta result;

    if (reverse_step)
        step = ~step;

    /* The low seven rate bits encode a five-bit shift and a two-bit step.
     * Expressing the hardware thresholds in the packed-rate domain preserves
     * the documented high-shift divider aliases. */
    if (rate < 0x2Cu)
        step = (int32_t)((uint32_t)step << ((0x2Fu - rate) >> 2));
    if (rate >= 0x30u)
        counter_add >>= (rate - 0x2Cu) >> 2;

    if (exponential) {
        if (decreasing) {
            step = ((int32_t)phase_level * step) >> 15;
        } else if (((uint16_t)phase_level & 0x7FFFu) >= 0x6000u) {
            if (rate < 0x28u) {
                step >>= 2;
            } else if (rate >= 0x2Cu) {
                counter_add >>= 2;
            } else {
                step >>= 1;
                counter_add >>= 1;
            }
        }
    }

    /* All rate bits set is the special never-step setting. Other rates that
     * underflow the divider still advance by the minimum one-count tick. */
    if (counter_add == 0 && rate != 0x7Fu)
        counter_add = 1;

    result.step = step;
    result.counter_add = (uint16_t)counter_add;
    return result;
}

static int decrease_has_reached_zero(uint16_t control, uint16_t current)
{
    const int decreasing = (control & 0x2000u) != 0;
    const int exponential = (control & 0x4000u) != 0;
    const int negative_phase = (control & 0x1000u) != 0;
    const uint16_t stop_sign = negative_phase ? 0x0000u : 0x8000u;

    if (!decreasing)
        return 0;

    /* Exponential decrease ignores negative phase and follows its own signed
     * level multiplication, so it does not use this early zero-crossing gate. */
    if (negative_phase && exponential)
        return 0;

    return current == 0 || (current & 0x8000u) == stop_sign;
}

static uint16_t add_step_with_saturation(uint16_t control, uint16_t current,
                                         int32_t step)
{
    const int decreasing = (control & 0x2000u) != 0;
    const uint16_t phase_mask = (control & 0x1000u) ? 0xFFFFu : 0x0000u;

    if (!decreasing && (uint16_t)(current ^ phase_mask) == 0x7FFFu)
        return current;

    {
        const uint16_t previous = current;
        uint16_t next = (uint16_t)(current + step);

        /* Increasing sweeps saturate at the signed endpoint selected by the
         * phase bit instead of wrapping through the opposite phase. */
        if (!decreasing && ((next ^ previous) & 0x8000u) &&
            ((next ^ phase_mask) & 0x8000u)) {
            next = (uint16_t)(0x7FFFu ^ phase_mask);
        }
        return next;
    }
}

void spu_sweep_reset(SpuSweep *sweep)
{
    if (!sweep)
        return;
    sweep->control = 0;
    sweep->current = 0;
    sweep->divider = 0;
}

void spu_sweep_write_control(SpuSweep *sweep, uint16_t value)
{
    if (!sweep)
        return;

    /* A control write does not immediately rewrite the live current-volume
     * register. Fixed mode applies on the next 44.1 kHz SPU cycle; sweep mode
     * begins from whichever current value is live at that time. */
    sweep->control = value;
}

void spu_sweep_write_current(SpuSweep *sweep, int16_t value)
{
    if (!sweep)
        return;
    sweep->current = (uint16_t)value;
}

int16_t spu_sweep_read_current(const SpuSweep *sweep)
{
    return sweep ? (int16_t)sweep->current : 0;
}

void spu_sweep_clock(SpuSweep *sweep)
{
    uint16_t phase_xor;
    SweepDelta delta;

    if (!sweep)
        return;

    if (!(sweep->control & 0x8000u)) {
        /* Fixed volume is signed volume/2. The internal current-volume
         * register is full signed 16-bit. */
        sweep->current = (uint16_t)((sweep->control & 0x7FFFu) << 1);
        return;
    }

    if (decrease_has_reached_zero(sweep->control, sweep->current)) {
        sweep->current = 0;
        return;
    }

    /* Negative phase normally evaluates the envelope against an inverted
     * current level. Exponential decrease is the hardware exception. */
    phase_xor = ((sweep->control & 0x1000u) &&
                 !((sweep->control & 0x2000u) &&
                   (sweep->control & 0x4000u))) ? 0xFFFFu : 0x0000u;
    delta = sweep_delta(sweep->control,
                        (int16_t)(sweep->current ^ phase_xor));

    sweep->divider = (uint16_t)(sweep->divider + delta.counter_add);
    if (!(sweep->divider & 0x8000u))
        return;

    sweep->divider = 0;
    sweep->current = add_step_with_saturation(sweep->control,
                                              sweep->current,
                                              delta.step);
}
