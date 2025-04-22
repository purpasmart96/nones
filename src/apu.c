#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "apu.h"
#include "cpu.h"
#include "nones.h"
#include "ppu.h"
#include "arena.h"
#include "loader.h"
#include "arena.h"
#include "bus.h"

#include "utils.h"

typedef struct {
    uint32_t cycles;
    // Envelopes & triangle's linear counter
    bool quarter_frame_clock;
    // Length counters & sweep units
    bool half_frame_clock;
    // Frame interrupt flag 
    bool frame_interrupt;
} SequenceStep;

static const SequenceStep sequence_mode_0_table[] =
{
    {3727,  true, false, false },
    {7455,  true, true,  false },
    {11184, true, false, false },
    {14913, true, true,  true  }
};

static const SequenceStep sequence_mode_1_table[] =
{
    { 3727,  true,  false, false },
    { 7455,  true,  true,  false },
    { 11184, true,  false, false },
    { 14913, false, false, false },
    { 18639, true,  true,  false }
};

static const SequenceStep sequence_mode_0_table_cpu[] =
{
    {7456,  true,  false,  false  },
    {14912, true,  true,   false  },
    {22370, true,  false,  false  },
//    {29828, false, false,  true   },
    {29828, true,  true,   true   }
};

static const SequenceStep sequence_mode_1_table_cpu[] =
{
    { 7456,  true,  false, false },
    { 14912, true,  true,  false },
    { 22370, true,  false, false },
    { 29828, false, false, false },
    { 37280, true,  true,  false }
};

static const uint8_t length_counter_table[] =
{
    10,
    254,
    20,
    2,
    40,
    4,
    80,
    6,
    160,
    8,
    60,
    10,
    14,
    12,
    26,
    14,

    12,
    16,
    24,
    18,
    48,
    20,
    96,
    22,
    192,
    24,
    72,
    26,
    16,
    28,
    32,
    30
};

static const uint8_t duty_cycle_table[4][8] =
{
    { 0, 0, 0, 0, 0, 0, 0, 1 },
    { 0, 0, 0, 0, 0, 0, 1, 1 },
    { 0, 0, 0, 0, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 0, 0 }
};

static const uint8_t duty_cycle_table1[4][8] =
{
    { 0, 1, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 1, 1, 0, 0, 0 },
    { 1, 0, 0, 1, 1, 1, 1, 1 }
};

static const uint8_t triangle_table[32] =
{
    15, 14, 13, 12, 11, 10,  9,  8, 
    7,  6,  5,  4,  3,  2,  1,  0,
    0,  1,  2,  3,  4,  5,  6,  7, 
    8,  9, 10, 11, 12, 13, 14, 15
};

static const uint16_t noise_table[16] =
{
    4,   8,   16,  32,  64,  96,   128,  160,
    202, 254, 380, 508, 762, 1016, 2034, 4068
};

static inline float CreateSquareSample(int input, float volume)
{
    return (input ? 1.0f : -1.0f) * volume;
}

static inline float CreateTriangleSample(int input)
{
    return 0.00851 * (input / 15.f);
}

static inline float CreateNoiseSample(int input)
{
    return 0.00494 * (input / 15.f);
}

#define FCPU 1789773.0

static void ApuWritePulse1Duty(Apu *apu, const uint8_t data)
{
    apu->pulse1.reg.raw = data;

    //printf("Set pulse 1 duty: %d\n", apu->pulse1.reg.duty);
    //printf("Set pulse 1 volume/envelope: %d\n", apu->pulse1.reg.volume_env);
    //printf("Set pulse 1 constant volume/envelope: %d\n", apu->pulse1.reg.constant_volume);
    //printf("Set pulse 1 counter halt/envelope loop: %d\n", apu->pulse1.reg.counter_halt);

    apu->pulse1.freq = roundf(FCPU / (16 * (apu->pulse1.timer_period.raw + 1)));

    //printf("Pulse 1 freq: %d\n",  (uint16_t)(FCPU / (16 * (apu->pulse1.timer.raw + 1))));
    //printf("Pulse 1 freq: %d\n", apu->pulse1.freq);
}

static void ApuWritePulse2Duty(Apu *apu, const uint8_t data)
{
    apu->pulse2.reg.raw = data;

    //printf("Set pulse 2 duty: %d\n", apu->pulse2.reg.duty);
    //printf("Set pulse 2 volume/envelope: %d\n", apu->pulse2.reg.volume_env);
    //printf("Set pulse 2 constant volume/envelope: %d\n", apu->pulse2.reg.constant_volume);
    //printf("Set pulse 2 counter halt/envelope loop: %d\n", apu->pulse2.reg.counter_halt);
    apu->pulse2.freq = roundf(FCPU / (16 * (apu->pulse2.timer_period.raw + 1)));
}

static void ApuWritePulse1LengthCounter(Apu *apu, const uint8_t data)
{
    apu->pulse1.length_counter_load = data;

    if (apu->status.pulse1)
    {
        apu->pulse1.length_counter = length_counter_table[apu->pulse1.length_counter_load];
    }
}

static void ApuWritePulse2LengthCounter(Apu *apu, const uint8_t data)
{
    apu->pulse2.length_counter_load = data;

    if (apu->status.pulse2)
    {
        apu->pulse2.length_counter = length_counter_table[apu->pulse2.length_counter_load];
    }
}

static void ApuWriteTriangleLengthCounter(Apu *apu, const uint8_t data)
{
    apu->triangle.length_counter_load = data;

    if (apu->status.triangle)
    {
        apu->triangle.length_counter = length_counter_table[apu->triangle.length_counter_load];
    }
}

static void ApuWriteNoiseLengthCounter(Apu *apu, const uint8_t data)
{
    apu->noise.length_counter_load = data;

    if (apu->status.noise)
    {
        apu->noise.length_counter = length_counter_table[apu->noise.length_counter_load];
    }
}

// Writing a zero to any of the channel enable bits (NT21) will silence that channel and halt its length counter.
// If the DMC bit is clear, the DMC bytes remaining will be set to 0 and the DMC will silence when it empties.
// If the DMC bit is set, the DMC sample will be restarted only if its bytes remaining is 0.
// If there are bits remaining in the 1-byte sample buffer, these will finish playing before the next sample is fetched.
// Writing to this register clears the DMC interrupt flag.
// Power-up and reset have the effect of writing $00, silencing all channels.
static void ApuWriteStatus(Apu *apu, const uint8_t data)
{
    apu->status.raw = data;
    if (!apu->status.pulse1)
    {
        apu->pulse1.length_counter = 0;
    }

    if (!apu->status.pulse2)
    {
        apu->pulse2.length_counter = 0;
    }

    if (!apu->status.triangle)
    {
        apu->triangle.length_counter = 0;
    }

    if (!apu->status.noise)
    {
        apu->noise.length_counter = 0;
    }

    apu->status.dmc_interrupt = 0;
}

static void ApuClockFrameCounter(Apu *apu)
{
    if (apu->frame_counter.counter > 0)
        apu->frame_counter.counter--;
    else
    {
        apu->frame_counter.counter = apu->frame_counter.control.sequencer_mode ? 4 : 3;
    }
    printf("Frame counter counter: %d\n", apu->frame_counter.counter);
}

static void ApuClockLengthCounters(Apu *apu)
{
    if (apu->pulse1.length_counter && !apu->pulse1.reg.counter_halt)
    {
        apu->pulse1.length_counter--;
    }

    if (apu->pulse2.length_counter && !apu->pulse2.reg.counter_halt)
    {
        apu->pulse2.length_counter--;
    }

    if (apu->triangle.length_counter && !apu->triangle.reg.control_halt)
    {
        apu->triangle.length_counter--;
    }

    if (apu->noise.length_counter && !apu->noise.reg.counter_halt)
    {
        apu->noise.length_counter--;
    }

    //printf("Pulse 1 length counter: %d\n", apu->pulse1.length_counter);
    //printf("Pulse 2 length counter: %d\n", apu->pulse2.length_counter);
    //printf("Triangle length counter: %d\n", apu->triangle.length_counter);
    //printf("Noise length counter: %d\n", apu->noise.length_counter);
}

static void ApuClockLinearCounters(Apu *apu)
{
    if (apu->triangle.reload)
    {
        apu->triangle.linear_counter = apu->triangle.reg.reload_value;
    }
    else if (apu->triangle.linear_counter > 0)
    {
        apu->triangle.linear_counter--;
    }

    if (!apu->triangle.reg.control_halt)
    {
        apu->triangle.reload = false;
    }

    //printf("Triangle linear counter: %d\n", apu->triangle.linear_counter);
}

static void UpdateTargetPeriod1(Apu *apu)
{
    int delta = apu->pulse1.timer_period.raw >> apu->pulse1.sweep_reg.shift_count;
    int target = apu->pulse1.timer_period.raw;
    if (apu->pulse1.sweep_reg.negate)
    {
        target -= (delta + 1); 
    }
    else
    {
        target += delta;
    }

    if (target > 0x7FF || apu->pulse1.timer_period.raw < 8)
    {
        apu->pulse1.muting = true;
    }
    else
    {
        apu->pulse1.muting = false;
    }

    //if (apu->pulse1.sweep_reg.shift_count)
        apu->pulse1.target_period = MAX(0, target);
}

static void UpdateTargetPeriod2(Apu *apu)
{
    int delta = apu->pulse2.timer_period.raw >> apu->pulse2.sweep_reg.shift_count;
    int target = apu->pulse2.timer_period.raw;

    if (apu->pulse2.sweep_reg.negate)
    {
        target -= delta;
    }
    else
    {
        target += delta;
    }

    if (target > 0x7FF || apu->pulse2.timer_period.raw < 8)
    {
        apu->pulse2.muting = true;
    }
    else {
        apu->pulse2.muting = false;
    }

    //if (apu->pulse2.sweep_reg.shift_count)
        apu->pulse2.target_period = MAX(0, target);
}

static void ApuClockSweeps(Apu *apu)
{
    if (!apu->pulse1.sweep_counter && apu->pulse1.sweep_reg.enabled && apu->pulse1.sweep_reg.shift_count && !apu->pulse1.muting)
    {
        apu->pulse1.timer_period.raw = apu->pulse1.target_period;
        UpdateTargetPeriod1(apu);
    }
    if (!apu->pulse1.sweep_counter || apu->pulse1.reload)
    {
        apu->pulse1.sweep_counter = apu->pulse1.sweep_reg.devider_period;
        apu->pulse1.reload = false;
        UpdateTargetPeriod1(apu);
    }
    else
    {
        apu->pulse1.sweep_counter--;
    }

    //if (apu->pulse1.sweep_reg.enabled)
    //{
    //    printf("After Pulse 1 timer period: %d\n", apu->pulse1.target_period);
    //    printf("After Pulse 1 timer raw: %d\n", apu->pulse1.timer.raw);
    //}

    //UpdateTargetPeriod2(apu);
    if (!apu->pulse2.sweep_counter && apu->pulse2.sweep_reg.enabled && apu->pulse2.sweep_reg.shift_count && !apu->pulse2.muting)
    {
        apu->pulse2.timer_period.raw = apu->pulse2.target_period;
        UpdateTargetPeriod2(apu);
    }
    if (!apu->pulse2.sweep_counter || apu->pulse2.reload)
    {
        apu->pulse2.sweep_counter = apu->pulse2.sweep_reg.devider_period;
        apu->pulse2.reload = false;
        UpdateTargetPeriod2(apu);
    }
    else
    {
        apu->pulse2.sweep_counter--;
    }
}

static void ApuClockEnvelopes(Apu *apu)
{
    if (!apu->pulse1.envelope.start)
    {
        if (apu->pulse1.envelope.counter > 0)
            apu->pulse1.envelope.counter--;
        else
        {
            apu->pulse1.envelope.counter = apu->pulse1.reg.volume_env;
            if (apu->pulse1.envelope.decay_counter > 0)
                apu->pulse1.envelope.decay_counter--;
            else if (apu->pulse1.reg.counter_halt)
                apu->pulse1.envelope.decay_counter = 15;
        }
    }
    else
    {
        apu->pulse1.envelope.start = false;
        apu->pulse1.envelope.decay_counter = 15;
        apu->pulse1.envelope.counter = apu->pulse1.reg.volume_env;
    }

    //printf("Pulse 1 envelope decay: %d\n", apu->pulse1.envelope.decay_counter);
    //printf("Pulse 1 envelope counter: %d\n", apu->pulse1.envelope.counter);
    if (!apu->pulse2.envelope.start)
    {
        if (apu->pulse2.envelope.counter > 0)
            apu->pulse2.envelope.counter--;
        else
        {
            apu->pulse2.envelope.counter = apu->pulse2.reg.volume_env;
            if (apu->pulse2.envelope.decay_counter > 0)
                apu->pulse2.envelope.decay_counter--;
            else if (apu->pulse2.reg.counter_halt)
                apu->pulse2.envelope.decay_counter = 15;
        }
    }
    else
    {
        apu->pulse2.envelope.start = false;
        apu->pulse2.envelope.decay_counter = 15;
        apu->pulse2.envelope.counter = apu->pulse2.reg.volume_env;
    }

    if (!apu->noise.envelope.start)
    {
        if (apu->noise.envelope.counter > 0)
            apu->noise.envelope.counter--;
        else
        {
            apu->noise.envelope.counter = apu->noise.reg.volume_env;
            if (apu->noise.envelope.decay_counter > 0)
                apu->noise.envelope.decay_counter--;
            else if (apu->noise.reg.counter_halt)
                apu->noise.envelope.decay_counter = 15;
        }
    }
    else
    {
        apu->noise.envelope.start = false;
        apu->noise.envelope.decay_counter = 15;
        apu->noise.envelope.counter = apu->noise.reg.volume_env;
    }

    DEBUG_LOG("Pulse 1 envelope counter: %d\n", apu->pulse1.envelope.counter);
    DEBUG_LOG("Pulse 1 envelope decay counter: %d\n", apu->pulse1.envelope.decay_counter);
    DEBUG_LOG("Pulse 2 envelope counter: %d\n", apu->pulse2.envelope.counter);
    DEBUG_LOG("Pulse 2 envelope decay counter: %d\n", apu->pulse2.envelope.decay_counter);
    DEBUG_LOG("Noise envelope counter: %d\n", apu->noise.envelope.counter);
    DEBUG_LOG("Noise envelope decay counter: %d\n", apu->noise.envelope.decay_counter);

    if (apu->pulse1.reg.constant_volume)
        apu->pulse1.volume = apu->pulse1.reg.volume_env;
    else
        apu->pulse1.volume = apu->pulse1.envelope.decay_counter;

    if (apu->pulse2.reg.constant_volume)
        apu->pulse2.volume = apu->pulse2.reg.volume_env;
    else
        apu->pulse2.volume = apu->pulse2.envelope.decay_counter;

    if (apu->noise.reg.constant_volume)
        apu->noise.volume = apu->noise.reg.volume_env;
    else
        apu->noise.volume = apu->noise.envelope.decay_counter;
}

static void ApuWriteFrameCounter(Apu *apu, const uint8_t data)
{
    apu->frame_counter.control.raw = data;
    apu->sequence_step = 0;
    apu->cycle_counter = 0;
    //ApuClockFrameCounter(apu);
    if (apu->frame_counter.control.sequencer_mode)
    {
        apu->frame_counter.clock_all = true;
        //printf("Framecounter called on cycle: %d\n", apu->cycle_counter);
    }
}

static void ApuWritePulse1Sweep(Apu *apu, const uint8_t data)
{
    apu->pulse1.sweep_reg.raw = data;
    apu->pulse1.reload = 1;
    UpdateTargetPeriod1(apu);

    //printf("Set pulse 1 sweep enabled: %d\n", apu->pulse1.sweep_reg.enabled);
    //printf("Set pulse 1 sweep shift count: %d\n", apu->pulse1.sweep_reg.shift_count);
    //printf("Set pulse 1 sweep devider period: %d\n", apu->pulse1.sweep_reg.devider_period);
    //printf("Set pulse 1 sweep negate: %d\n", apu->pulse1.sweep_reg.negate);
}

static void ApuWritePulse2Sweep(Apu *apu, const uint8_t data)
{
    apu->pulse2.sweep_reg.raw = data;
    apu->pulse2.reload = 1;
    UpdateTargetPeriod2(apu);

    //printf("Set pulse 2 sweep enabled: %d\n", apu->pulse2.sweep.enabled);
    //printf("Set pulse 2 sweep shift count: %d\n", apu->pulse2.sweep.shift_count);
    //printf("Set pulse 2 sweep devider period: %d\n", apu->pulse2.sweep.devider_period);
    //printf("Set pulse 2 sweep negate: %d\n", apu->pulse2.sweep.negate);
}

void WriteAPURegister(Apu *apu, const uint16_t addr, const uint8_t data)
{
    switch (addr)
    {
        case APU_PULSE_1_DUTY:
            ApuWritePulse1Duty(apu, data);
            break;
        case APU_PULSE_1_SWEEP:
            ApuWritePulse1Sweep(apu, data);
            break;
        case APU_PULSE_1_TIMER_LOW:
            apu->pulse1.timer_period.low = data;
            UpdateTargetPeriod1(apu);
            break;
        case APU_PULSE_1_TIMER_HIGH:
            apu->pulse1.timer_period.high = data & 0x7;
            ApuWritePulse1LengthCounter(apu, data >> 3);
            UpdateTargetPeriod1(apu);
            apu->pulse1.envelope.start = true;
            apu->pulse1.duty_step = 0;
            break;
        case APU_PULSE_2_DUTY:
            ApuWritePulse2Duty(apu, data);
            break;
        case APU_PULSE_2_SWEEP:
            ApuWritePulse2Sweep(apu, data);
            break;
        case APU_PULSE_2_TIMER_LOW:
            apu->pulse2.timer_period.low = data;
            UpdateTargetPeriod2(apu);
            break;
        case APU_PULSE_2_TIMER_HIGH:
            apu->pulse2.timer_period.high = data & 0x7;
            ApuWritePulse2LengthCounter(apu, data >> 3);
            UpdateTargetPeriod2(apu);
            apu->pulse2.envelope.start = true;
            apu->pulse2.duty_step = 0;
            break;
        case APU_TRIANGLE_LINEAR_COUNTER:
            apu->triangle.reg.raw = data;
            break;
        case APU_TRIANGLE_TIMER_LOW:
            apu->triangle.timer_period.low = data;
            break;
        case APU_TRIANGLE_TIMER_HIGH:
            apu->triangle.timer_period.high = data & 0x7;
            ApuWriteTriangleLengthCounter(apu, data >> 3);
            apu->triangle.reload = true;
            break;
        case APU_NOISE:
            apu->noise.reg.raw = data;
            break;
        case APU_NOISE_PERIOD:
            apu->noise.period_reg.raw = data;
            apu->noise.timer_period.raw = noise_table[apu->noise.period_reg.period];
            break;
        case APU_NOISE_LENGTH_COUNTER_LOAD:
            ApuWriteNoiseLengthCounter(apu, data >> 3);
            apu->noise.envelope.start = true;
            break;
        case APU_DMC_DIRECT_LOAD:
            apu->dmc.level = data;
            //printf("Dmc level: %d\n", apu->dmc.level);
            break;
        case APU_STATUS:
            ApuWriteStatus(apu, data);
            break;
        case APU_FRAME_COUNTER:
            ApuWriteFrameCounter(apu, data);
            break;
        default:
            DEBUG_LOG("Writing to unfinished Apu reg at addr: 0x%04X\n", addr);
            break;
    }
}

static uint8_t ApuReadStatus(Apu *apu)
{
    apu->status.frame_interrupt = 0;
    ApuStatus status;
    status.raw = apu->status.raw;

    status.pulse1 = apu->pulse1.length_counter != 0;
    status.pulse2 = apu->pulse2.length_counter != 0;
    status.triangle = apu->triangle.length_counter != 0;
    status.noise = apu->noise.length_counter != 0;

    return status.raw;
}

uint8_t ReadAPURegister(Apu *apu, const uint16_t addr)
{
    switch (addr)
    {
        case APU_STATUS:
            return ApuReadStatus(apu);
        default:
            printf("Reading from open bus at addr: 0x%04X\n", addr);
            return 0;
    }
}

static void ApuFrameMode0SetSeqStep(Apu *apu)
{
    switch (apu->cycle_counter) {
        case 0:
            apu->sequence_step = 0;
            break;
        case 7457:
            apu->sequence_step = 1;
            break;
        case 14914:
            apu->sequence_step = 2;
            break;
        case 22371:
            apu->sequence_step = 3;
            break;
    }
}

static void ApuFrameMode1SetSeqStep(Apu *apu)
{
    switch (apu->cycle_counter) {
        case 0:
            apu->sequence_step = 0;
            break;
        case 7457:
            apu->sequence_step = 1;
            break;
        case 14913:
            apu->sequence_step = 2;
            break;
        case 22371:
            apu->sequence_step = 3;
            break;
        case 29829:
            apu->sequence_step = 4;
            break;
    }
}

static void ApuClockTimers(Apu *apu)
{
    if (apu->pulse1.timer.raw > 0)
        apu->pulse1.timer.raw--;
    else
    {
        apu->pulse1.timer.raw = apu->pulse1.timer_period.raw;
        apu->pulse1.duty_step = (apu->pulse1.duty_step + 1) % 8;
    }

    if (apu->pulse1.length_counter == 0 || apu->pulse1.muting)
    {
        apu->pulse1.output = 0;
    }
    else
    {
        apu->pulse1.output = duty_cycle_table[apu->pulse1.reg.duty][apu->pulse1.duty_step];
    }

    if (apu->pulse2.timer.raw > 0)
        apu->pulse2.timer.raw--;
    else
    {
        apu->pulse2.timer.raw = apu->pulse2.timer_period.raw;
        apu->pulse2.duty_step = (apu->pulse2.duty_step + 1) % 8;
    }

    if (apu->pulse2.length_counter == 0 || apu->pulse2.muting)
    {
        apu->pulse2.output = 0;
    }
    else
    {
        apu->pulse2.output = duty_cycle_table[apu->pulse2.reg.duty][apu->pulse2.duty_step];
    }

    if (apu->noise.timer.raw > 0)
        apu->noise.timer.raw--;
    else
    {
        apu->noise.timer.raw = apu->noise.timer_period.raw;
        // Clock shift reg here
        uint16_t feedback;
        if (apu->noise.period_reg.mode)
        {
            feedback = (apu->noise.shift_reg.bit0 ^ apu->noise.shift_reg.bit6);
        }
        else
        {
            feedback = (apu->noise.shift_reg.bit0 ^ apu->noise.shift_reg.bit1);
        }
        apu->noise.shift_reg.raw >>= 1;
        apu->noise.shift_reg.bit14 = feedback;
    }

    if (apu->noise.length_counter == 0 || apu->noise.shift_reg.bit0)
    {
        apu->noise.output = 0;
    }
    else
    {
        apu->noise.output = apu->noise.volume;
    }

}

static void ApuMixSample(Apu *apu)
{
    float square1 = CreateSquareSample(apu->pulse1.output, apu->pulse1.volume / 15.f);
    float square2 = CreateSquareSample(apu->pulse2.output, apu->pulse2.volume / 15.f);
    float triangle = CreateTriangleSample(apu->triangle.output);
    float noise = CreateNoiseSample(apu->noise.output);

    float tnd_out =  triangle + noise; // + 0.00335 * apu->dmc.level;
    apu->mixed_sample = ((0.00752 * (square1 + square2)) + tnd_out) * 10;
}

void APU_Init(Apu *apu)
{
    memset(apu, 0, sizeof(*apu));
    apu->noise.shift_reg.raw = 1;
}

static bool irq_triggered = false;

void APU_Update(Apu *apu, uint64_t cpu_cycles)
{
    // Get the delta of cpu cycles since the last cpu instruction
    uint64_t cpu_cycles_delta = cpu_cycles - apu->prev_cpu_cycles;
    // Update prev cpu cycles to current amount for next update
    apu->prev_cpu_cycles = cpu_cycles;
    // Calculate how many apu ticks we need to run
    uint64_t apu_cycles_to_run = cpu_cycles_delta;

    while (apu_cycles_to_run != 0)
    {
        SequenceStep step;
        if (apu->frame_counter.control.sequencer_mode == 0)
        {
            apu->cycle_counter = apu->cycle_counter % 29829;
            ApuFrameMode0SetSeqStep(apu);
            step = sequence_mode_0_table_cpu[apu->sequence_step];
        }
        else
        {
            apu->cycle_counter = apu->cycle_counter % 37281;
            ApuFrameMode1SetSeqStep(apu);
            step = sequence_mode_1_table_cpu[apu->sequence_step];
        }

        // 0x1000's apu test 1 & 2 will fail with this even though this is needed for proper sound
        if (apu->cycle_counter == 0 && apu->frame_counter.clock_all)
        {
            ApuClockEnvelopes(apu);
            ApuClockLinearCounters(apu);
            ApuClockLengthCounters(apu);
            ApuClockSweeps(apu);
            apu->frame_counter.clock_all = false;
        }

        if (apu->cycle_counter == step.cycles)
        {
            if (step.quarter_frame_clock)
            {
                ApuClockEnvelopes(apu);
                ApuClockLinearCounters(apu);
            }

            if (step.half_frame_clock)
            {
                ApuClockLengthCounters(apu);
                ApuClockSweeps(apu);
            }

            if (step.frame_interrupt && !apu->frame_counter.control.interrupt_inhibit)
            {
                irq_triggered = true;
                apu->frame_counter.interrupt = true;
            }
        }

        if (apu->triangle.timer.raw > 0)
            apu->triangle.timer.raw--;
        else
        {
            apu->triangle.timer.raw = apu->triangle.timer_period.raw;
            if (apu->triangle.length_counter && apu->triangle.linear_counter)
                apu->triangle.seq_pos = (apu->triangle.seq_pos + 1) % 32;
        }

        if (apu->triangle.timer_period.raw < 2)
        {
            apu->triangle.output = 0;
        }
        else if (apu->triangle.length_counter && apu->triangle.linear_counter)
        {
            apu->triangle.output = triangle_table[apu->triangle.seq_pos];
        }

        if ((cpu_cycles - apu_cycles_to_run) & 1)
        {
            //ApuClockFrameCounter(apu);
            ApuClockTimers(apu);
            ApuMixSample(apu);
        }

        //UpdateTargetPeriod1(apu);
        //UpdateTargetPeriod2(apu);

        if (apu->current_sample >= 29780)
        {
            NonesPutSoundData(apu);
            apu->current_sample = 0;
        }
        apu->buffer[apu->current_sample++] = apu->mixed_sample;
        apu->cycle_counter++;
        apu_cycles_to_run--;
    }
}

void APU_Reset(void)
{

}

bool APU_IrqTriggered(void)
{
    if (irq_triggered)
    {
        irq_triggered = false;
        return true;
    }
    return false;
}
