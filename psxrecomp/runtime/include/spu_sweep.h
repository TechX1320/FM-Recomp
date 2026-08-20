#ifndef PSXRECOMP_SPU_SWEEP_H
#define PSXRECOMP_SPU_SWEEP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PlayStation SPU volume-envelope state. The control register is the raw
 * VxVOL/MainVol value. Current is the signed 16-bit internal volume exposed at
 * 0x1F801E00+voice*4 (or 0x1F801DB8 for main volume). Divider implements the
 * sub-sample rates used by slow sweeps. */
typedef struct SpuSweep {
    uint16_t control;
    uint16_t current;
    uint16_t divider;
} SpuSweep;

void    spu_sweep_reset(SpuSweep *sweep);
void    spu_sweep_write_control(SpuSweep *sweep, uint16_t value);
void    spu_sweep_write_current(SpuSweep *sweep, int16_t value);
int16_t spu_sweep_read_current(const SpuSweep *sweep);
void    spu_sweep_clock(SpuSweep *sweep);

#ifdef __cplusplus
}
#endif

#endif /* PSXRECOMP_SPU_SWEEP_H */
