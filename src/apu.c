#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include <SDL3/SDL.h>
#include <soxr.h>

#include "arena.h"
#include "apu.h"
#include "ppu.h"
#include "system.h"
#include "nones.h"

#include "utils.h"

static soxr_t soxr;
static soxr_error_t error;

//#define APU_FAST_MIXER

static const SequenceStep sequence_table[2][6] =
{
    // Mode 0: 4-Step Sequence
    {
        { 7457,  SEQ_CLOCK_QUARTER_FRAME, false },
        { 14913, SEQ_CLOCK_HALF_FRAME,    false },
        { 22371, SEQ_CLOCK_QUARTER_FRAME, false },
        { 29828, SEQ_CLOCK_NONE,          true  },
        { 29829, SEQ_CLOCK_HALF_FRAME,    true  },
        { 29830, SEQ_CLOCK_NONE,          true  }
    },

    // Mode 1: 5-Step Sequence
    {
        { 7457,  SEQ_CLOCK_QUARTER_FRAME, false },
        { 14913, SEQ_CLOCK_HALF_FRAME,    false },
        { 22371, SEQ_CLOCK_QUARTER_FRAME, false },
        { 29829, SEQ_CLOCK_NONE,          false },
        { 37281, SEQ_CLOCK_HALF_FRAME,    false },
        { 37282, SEQ_CLOCK_NONE,          false }
    }
};

static const uint8_t length_counter_table[] =
{
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

static const uint8_t duty_cycle_table[2][4][8] =
{
    // Normal Duty cycle table
    // Duty  Output
    // 0 	 0 1 0 0 0 0 0 0 (12.5%)
    // 1 	 0 1 1 0 0 0 0 0 (25%)
    // 2 	 0 1 1 1 1 0 0 0 (50%)
    // 3 	 1 0 0 1 1 1 1 1 (25% negated) 
    {
        { 0, 1, 0, 0, 0, 0, 0, 0 },
        { 0, 1, 1, 0, 0, 0, 0, 0 },
        { 0, 1, 1, 1, 1, 0, 0, 0 },
        { 1, 0, 0, 1, 1, 1, 1, 1 }
    },

    // Swapped Duty cycle table for older famiclones
    // Duty  Output
    // 0 	 0 1 0 0 0 0 0 0 (12.5%)
    // 1 	 0 1 1 1 1 0 0 0 (50%)
    // 2 	 0 1 1 0 0 0 0 0 (25%)
    // 3 	 1 0 0 1 1 1 1 1 (25% negated) 
    {
        { 0, 1, 0, 0, 0, 0, 0, 0 },
        { 0, 1, 1, 1, 1, 0, 0, 0 },
        { 0, 1, 1, 0, 0, 0, 0, 0 },
        { 1, 0, 0, 1, 1, 1, 1, 1 }
    }
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
    2,   4,   8,  32,  32,  48,   64,  80,
    101, 127, 190, 254, 381, 508, 1017, 2034
};

// 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84,  72,  54
static const uint8_t dmc_table[16] =
{
    214, 190, 170, 160, 143, 127, 113, 107,
    95,  80,  71,  64,  53,  42,  36,  27
};

bool PollApuIrqs(Apu *apu)
{
    return apu->status.dmc_irq | (apu->status.frame_irq & ~apu->frame_counter.control.irq_inhibit);
}

#define FCPU 1789773.0

static void ApuWritePulse1Duty(Apu *apu, const uint8_t data)
{
    apu->pulse1.reg.raw = data;

    //printf("Set pulse 1 duty: %d\n", apu->pulse1.reg.duty);
    //printf("Set pulse 1 volume/envelope: %d\n", apu->pulse1.reg.volume_env);
    //printf("Set pulse 1 constant volume/envelope: %d\n", apu->pulse1.reg.constant_volume);
    //printf("Set pulse 1 counter halt/envelope loop: %d\n", apu->pulse1.reg.counter_halt);
    //printf("Pulse 1 freq: %f\n",  roundf(FCPU / (16 * (apu->pulse1.timer_period.raw + 1))));
}

static void ApuWritePulse2Duty(Apu *apu, const uint8_t data)
{
    apu->pulse2.reg.raw = data;

    //printf("Set pulse 2 duty: %d\n", apu->pulse2.reg.duty);
    //printf("Set pulse 2 volume/envelope: %d\n", apu->pulse2.reg.volume_env);
    //printf("Set pulse 2 constant volume/envelope: %d\n", apu->pulse2.reg.constant_volume);
    //printf("Set pulse 2 counter halt/envelope loop: %d\n", apu->pulse2.reg.counter_halt);
    //printf("Pulse 2 freq: %f\n",  roundf(FCPU / (16 * (apu->pulse2.timer_period.raw + 1))));
}

static void ApuWritePulse1LengthCounter(Apu *apu, const uint8_t data)
{
    apu->pulse1.length_counter_load = data;

    apu->pulse1.length_counter = length_counter_table[apu->pulse1.length_counter_load] * apu->status.pulse1;
}

static void ApuWritePulse2LengthCounter(Apu *apu, const uint8_t data)
{
    apu->pulse2.length_counter_load = data;

    apu->pulse2.length_counter = length_counter_table[apu->pulse2.length_counter_load] * apu->status.pulse2;
}

static void ApuWriteTriangleLengthCounter(Apu *apu, const uint8_t data)
{
    apu->triangle.length_counter_load = data;

    apu->triangle.length_counter = length_counter_table[apu->triangle.length_counter_load] * apu->status.triangle;
}

static void ApuWriteNoiseLengthCounter(Apu *apu, const uint8_t data)
{
    apu->noise.length_counter_load = data;

    apu->noise.length_counter = length_counter_table[apu->noise.length_counter_load] * apu->status.noise;
}

static void ApuResetSample(Apu *apu)
{
    apu->dmc.bytes_remaining = apu->dmc.sample_length;
    apu->dmc.addr_counter = apu->dmc.sample_addr;
    apu->dmc.restart = false;
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

    apu->pulse1.length_counter *= apu->status.pulse1;
    apu->pulse2.length_counter *= apu->status.pulse2;
    apu->triangle.length_counter *= apu->status.triangle;
    apu->noise.length_counter *= apu->status.noise;

    if (!apu->status.dmc)
    {
        apu->dmc.bytes_remaining = 0;
    }
    else if (!apu->dmc.bytes_remaining)
    {
        apu->dmc.restart = true;
    }

    apu->status.dmc_irq = 0;
}

static void ApuClockLengthCounters(Apu *apu)
{
    if (apu->pulse1.length_counter && !apu->pulse1.reg.counter_halt)
    {
        --apu->pulse1.length_counter;
    }

    if (apu->pulse2.length_counter && !apu->pulse2.reg.counter_halt)
    {
        --apu->pulse2.length_counter;
    }

    if (apu->triangle.length_counter && !apu->triangle.reg.control_halt)
    {
        --apu->triangle.length_counter;
    }

    if (apu->noise.length_counter && !apu->noise.reg.counter_halt)
    {
        --apu->noise.length_counter;
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
        --apu->triangle.linear_counter;
    }

    apu->triangle.reload &= apu->triangle.reg.control_halt; 

    //printf("Triangle linear counter: %d\n", apu->triangle.linear_counter);
}

static void ApuUpdatePulseTargetPeriod(ApuPulse *pulse, const bool pulse1)
{
    const int delta = pulse->timer_period.raw >> pulse->sweep_reg.shift_count;
    int target = pulse->timer_period.raw;

    if (pulse->sweep_reg.negate)
    {
        target -= (delta + pulse1); 
    }
    else
    {
        target += delta;
    }

    if (target > 0x7FF || pulse->timer_period.raw < 8)
    {
        pulse->muting = true;
    }
    else
    {
        pulse->muting = false;
    }

    pulse->target_period = MAX(0, target);
}

static void ApuWriteDmcControl(Apu *apu, uint8_t data)
{
    apu->dmc.control.raw = data;

    apu->status.dmc_irq &= apu->dmc.control.irq;

    apu->dmc.timer_period = dmc_table[apu->dmc.control.freq_rate] - 1;

    //printf("Dmc timer period set: %d\n", apu->dmc.timer_period);
}

static void ApuClockSweeps(Apu *apu)
{
    if (!apu->pulse1.sweep_counter && apu->pulse1.sweep_reg.enabled && apu->pulse1.sweep_reg.shift_count && !apu->pulse1.muting)
    {
        apu->pulse1.timer_period.raw = apu->pulse1.target_period;
        ApuUpdatePulseTargetPeriod(&apu->pulse1, true);
    }
    if (!apu->pulse1.sweep_counter || apu->pulse1.reload)
    {
        apu->pulse1.sweep_counter = apu->pulse1.sweep_reg.devider_period;
        apu->pulse1.reload = false;
        ApuUpdatePulseTargetPeriod(&apu->pulse1, true);
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

    if (!apu->pulse2.sweep_counter && apu->pulse2.sweep_reg.enabled && apu->pulse2.sweep_reg.shift_count && !apu->pulse2.muting)
    {
        apu->pulse2.timer_period.raw = apu->pulse2.target_period;
        ApuUpdatePulseTargetPeriod(&apu->pulse2, false);
    }
    if (!apu->pulse2.sweep_counter || apu->pulse2.reload)
    {
        apu->pulse2.sweep_counter = apu->pulse2.sweep_reg.devider_period;
        apu->pulse2.reload = false;
        ApuUpdatePulseTargetPeriod(&apu->pulse2, false);
    }
    else
    {
        --apu->pulse2.sweep_counter;
    }
}

static void ApuClockEnvelope(ApuEnvelope *envelope, const uint8_t volume, const bool counter_halt)
{
    if (!envelope->start)
    {
        if (envelope->counter > 0)
            --envelope->counter;
        else
        {
            envelope->counter = volume;
            if (envelope->decay_counter > 0)
                --envelope->decay_counter;
            else if (counter_halt)
                envelope->decay_counter = 15;
        }
    }
    else
    {
        envelope->start = false;
        envelope->decay_counter = 15;
        envelope->counter = volume;
    }
}

static void ApuClockEnvelopes(Apu *apu)
{
    ApuClockEnvelope(&apu->pulse1.envelope, apu->pulse1.reg.volume_env, apu->pulse1.reg.counter_halt);
    ApuClockEnvelope(&apu->pulse2.envelope, apu->pulse2.reg.volume_env, apu->pulse2.reg.counter_halt);
    ApuClockEnvelope(&apu->noise.envelope, apu->noise.reg.volume_env, apu->noise.reg.counter_halt);

    //DEBUG_LOG("Pulse 1 envelope counter: %d\n", apu->pulse1.envelope.counter);
    //DEBUG_LOG("Pulse 1 envelope decay counter: %d\n", apu->pulse1.envelope.decay_counter);
    //DEBUG_LOG("Pulse 2 envelope counter: %d\n", apu->pulse2.envelope.counter);
    //DEBUG_LOG("Pulse 2 envelope decay counter: %d\n", apu->pulse2.envelope.decay_counter);
    //DEBUG_LOG("Noise envelope counter: %d\n", apu->noise.envelope.counter);
    //DEBUG_LOG("Noise envelope decay counter: %d\n", apu->noise.envelope.decay_counter);

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

static void ApuClockTriangle(Apu *apu)
{
    if (apu->triangle.timer.raw > 0)
        --apu->triangle.timer.raw;
    else
    {
        apu->triangle.timer.raw = apu->triangle.timer_period.raw;
        if (apu->triangle.length_counter && apu->triangle.linear_counter)
            apu->triangle.seq_pos = (apu->triangle.seq_pos + 1) & 0x1F;
    }

    if (apu->triangle.timer_period.raw < 2)
    {
        apu->triangle.output = 0;
    }
    else if (apu->triangle.length_counter && apu->triangle.linear_counter)
    {
        apu->triangle.output = triangle_table[apu->triangle.seq_pos];
    }
}

static void ApuClockDmc(Apu *apu)
{
    if (apu->dmc.timer > 0)
        --apu->dmc.timer;
    else
    {
        apu->dmc.timer = apu->dmc.timer_period;
        // If the silence flag is clear, the output level changes based on bit 0 of the shift register.
        // If the bit is 1, add 2; otherwise, subtract 2.
        // But if adding or subtracting 2 would cause the output level to leave the 0-127 range, leave the output level unchanged.
        // This means subtract 2 only if the current level is at least 2, or add 2 only if the current level is at most 125.
        if (!apu->dmc.silence)
        {
            const uint8_t bit0 = apu->dmc.shift_reg & 1;
            if (bit0 && apu->dmc.output_level <= 125)
            {
                apu->dmc.output_level += 2;
            }
            else if (!bit0 && apu->dmc.output_level >= 2)
            {
                apu->dmc.output_level -= 2;
            }
        }
        // The right shift register is clocked.
        apu->dmc.shift_reg >>= 1;

        if (!(apu->dmc.bits_remaining--))
        {
            apu->dmc.bits_remaining = 7;
            // Output cycle ending
            // If the sample buffer is empty, then the silence flag is set;
            // otherwise, the silence flag is cleared and the sample buffer is emptied into the shift register.
            if (apu->dmc.empty)
            {
                apu->dmc.silence = true;
            }
            else
            {
                apu->dmc.empty = true;
                apu->dmc.silence = false;
                apu->dmc.shift_reg = apu->dmc.sample_buffer;
            }
        }
    }
}

static void ApuResetFrameCounter(Apu *apu)
{
    apu->frame_counter.reload = apu->frame_counter.control.seq_mode ? 37282 : 29830;
    apu->frame_counter.reset_delay = 2;
    apu->frame_counter.step = 0;
    apu->frame_counter.timer = 0;
    apu->frame_counter.reset = false;

    if (apu->frame_counter.control.seq_mode)
    {
        //printf("ApuResetFrameCounter Mode %d: Step %d cpu cycle: %ld\n", apu->frame_counter.control.seq_mode, apu->frame_counter.step, apu->cycles);
        ApuClockEnvelopes(apu);
        ApuClockLinearCounters(apu);
        ApuClockLengthCounters(apu);
        ApuClockSweeps(apu);
    }
}

// If the write occurs during an APU cycle, the effects occur 3 CPU cycles after the $4017 write cycle.;
// If the write occurs between APU cycles, the effects occurs 4 CPU cycles after the write cycle.
static void ApuWriteFrameCounter(Apu *apu, const uint8_t data)
{
    //printf("\nApu frame counter called on cycle: %d\n", apu->frame_counter.timer);
    apu->frame_counter.control.raw = data;
    apu->frame_counter.reset = true;

    // Seems correct
    apu->status.frame_irq &= ~apu->frame_counter.control.irq_inhibit;
}

static void ApuWritePulse1Sweep(Apu *apu, const uint8_t data)
{
    apu->pulse1.sweep_reg.raw = data;
    apu->pulse1.reload = true;
    ApuUpdatePulseTargetPeriod(&apu->pulse1, true);

    //printf("Set pulse 1 sweep enabled: %d\n", apu->pulse1.sweep_reg.enabled);
    //printf("Set pulse 1 sweep shift count: %d\n", apu->pulse1.sweep_reg.shift_count);
    //printf("Set pulse 1 sweep devider period: %d\n", apu->pulse1.sweep_reg.devider_period);
    //printf("Set pulse 1 sweep negate: %d\n", apu->pulse1.sweep_reg.negate);
}

static void ApuWritePulse2Sweep(Apu *apu, const uint8_t data)
{
    apu->pulse2.sweep_reg.raw = data;
    apu->pulse2.reload = true;
    ApuUpdatePulseTargetPeriod(&apu->pulse2, false);

    //printf("Set pulse 2 sweep enabled: %d\n", apu->pulse2.sweep.enabled);
    //printf("Set pulse 2 sweep shift count: %d\n", apu->pulse2.sweep.shift_count);
    //printf("Set pulse 2 sweep devider period: %d\n", apu->pulse2.sweep.devider_period);
    //printf("Set pulse 2 sweep negate: %d\n", apu->pulse2.sweep.negate);
}

static void ApuDmcWriteSampleAddr(Apu *apu, const uint8_t data)
{
    apu->dmc.sample_addr = 0XC000 + (data * 64);
    //printf("Dmc sample addr: 0x%0X data: %X length: %d\n", apu->dmc.sample_addr, apu->dmc.sample_buffer, apu->dmc.sample_length);
}

// DMC DMA
void ApuDmcDmaUpdate(Apu *apu)
{
    apu->dmc.sample_buffer = BusRead(apu->dmc.addr_counter);
    apu->dmc.empty = false;
    apu->dmc.addr_counter = MAX(0x8000, (apu->dmc.addr_counter + 1) & 0xFFFF);

    if (!(--apu->dmc.bytes_remaining))
    {
        apu->dmc.restart = apu->dmc.control.loop;
        apu->status.dmc_irq = ~apu->dmc.control.loop & apu->dmc.control.irq;
    }
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
            ApuUpdatePulseTargetPeriod(&apu->pulse1, true);
            break;
        case APU_PULSE_1_TIMER_HIGH:
            apu->pulse1.timer_period.high = data & 0x7;
            ApuWritePulse1LengthCounter(apu, data >> 3);
            ApuUpdatePulseTargetPeriod(&apu->pulse1, true);
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
            ApuUpdatePulseTargetPeriod(&apu->pulse2, false);
            break;
        case APU_PULSE_2_TIMER_HIGH:
            apu->pulse2.timer_period.high = data & 0x7;
            ApuWritePulse2LengthCounter(apu, data >> 3);
            ApuUpdatePulseTargetPeriod(&apu->pulse2, false);
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
            apu->noise.timer_period.raw = noise_table[apu->noise.period_reg.period] - 1;
            break;
        case APU_NOISE_LENGTH_COUNTER_LOAD:
            ApuWriteNoiseLengthCounter(apu, data >> 3);
            apu->noise.envelope.start = true;
            break;
        case APU_DMC_CONTROL:
            ApuWriteDmcControl(apu, data);
            break;
        case APU_DMC_DIRECT_LOAD:
            apu->dmc.output_level = data;
            break;
        case APU_DMC_SAMPLE_ADDR:
            ApuDmcWriteSampleAddr(apu, data);
            break;
        case APU_DMC_SAMPLE_LENGTH:
            apu->dmc.sample_length = (data * 16) + 1;
            break;
        case APU_STATUS:
            ApuWriteStatus(apu, data);
            break;
        case APU_FRAME_COUNTER:
            ApuWriteFrameCounter(apu, data);
            break;
        default:
            DEBUG_LOG("Writing to unused Apu reg at addr: 0x%04X\n", addr);
            break;
    }
}

uint8_t ApuReadStatus(Apu *apu, const uint8_t bus_data)
{
    ApuStatus status = {
        .pulse1 = apu->pulse1.length_counter != 0,
        .pulse2 = apu->pulse2.length_counter != 0,
        .triangle = apu->triangle.length_counter != 0,
        .noise = apu->noise.length_counter != 0,
        .dmc = apu->dmc.bytes_remaining != 0,
        .open_bus = bus_data >> 5,
        .frame_irq = apu->status.frame_irq,
        .dmc_irq = apu->status.dmc_irq
    };

    apu->clear_frame_irq = true;

    //printf("Timer Period value: %d\n", apu->dmc.timer_period);
    //printf("Dmc bytes remaining: %d\n", apu->dmc.bytes_remaining);
    return status.raw;
}

static void ApuClockTimers(Apu *apu)
{
    if (apu->pulse1.timer.raw > 0)
        --apu->pulse1.timer.raw;
    else
    {
        apu->pulse1.timer.raw = apu->pulse1.timer_period.raw;
        apu->pulse1.duty_step = (apu->pulse1.duty_step - 1) & 7;
    }

    if (!apu->pulse1.length_counter || apu->pulse1.muting)
    {
        apu->pulse1.output = 0;
    }
    else
    {
        apu->pulse1.output = duty_cycle_table[apu->swap_duty_cycles][apu->pulse1.reg.duty][apu->pulse1.duty_step];
    }

    if (apu->pulse2.timer.raw > 0)
        --apu->pulse2.timer.raw;
    else
    {
        apu->pulse2.timer.raw = apu->pulse2.timer_period.raw;
        apu->pulse2.duty_step = (apu->pulse2.duty_step - 1) & 7;
    }

    if (!apu->pulse2.length_counter || apu->pulse2.muting)
    {
        apu->pulse2.output = 0;
    }
    else
    {
        apu->pulse2.output = duty_cycle_table[apu->swap_duty_cycles][apu->pulse2.reg.duty][apu->pulse2.duty_step];
    }

    if (apu->noise.timer.raw > 0)
        --apu->noise.timer.raw;
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

// This and ApplyFilter are technically for LPF, but to simplify things; it is used for HPF as well
static float ComputeFilterAlpha(float freq, float cutoff)
{
    double dt = 1.0 / freq;
    double rc = 1.0 / (2.0 * M_PI * cutoff);
    return dt / (rc + dt);
}

static float ApplyFilter(float sample, float prev_sample, float alpha)
{
    return alpha * sample + (1.0 - alpha) * prev_sample;
}

static void ApuMixSample(Apu *apu)
{
    float square1 = apu->pulse1.output * apu->pulse1.volume;
    float square2 = apu->pulse2.output * apu->pulse2.volume;

#ifdef APU_FAST_MIXER
    float pulse = 0.00752f * (square1 + square2);
    float tnd_out = 0.00851f * apu->triangle.output + 0.00494f * apu->noise.output + 0.00335f * apu->dmc.output_level;
#else
    float pulse = 95.88 / ((8128.0 / (square1 + square2)) + 100);
    float tnd = 1 / ((apu->triangle.output / 8227.0) + (apu->noise.output / 12241.0) + (apu->dmc.output_level / 22638.0));
    float tnd_out = 159.79 / (tnd + 100);
#endif
    float raw_sample = pulse + tnd_out;
    // Apply a HPF to fix the the DC offset without affecting the FR too much
    apu->mixer.hpf_sample = ApplyFilter(raw_sample, apu->mixer.hpf_sample, apu->mixer.hpf_alpha);
    // Apply a LPF just for the buffer used as the input for soxr, could also just make this lowpass cutoff at 14khz
    apu->mixer.sample = ApplyFilter(raw_sample - apu->mixer.hpf_sample, apu->mixer.sample, apu->mixer.lpf_alpha);
}

static void ApuGetClock(Apu *apu)
{
    if (apu->dmc.restart)
    {
        ApuResetSample(apu);
    }

    apu->status.frame_irq &= !apu->clear_frame_irq;
    apu->clear_frame_irq = false;

    if (apu->frame_counter.reset)
    {
        if (!(--apu->frame_counter.reset_delay))
        {
            ApuResetFrameCounter(apu);
        }
    }
}

static void ApuPutClock(Apu *apu)
{
    ApuClockTimers(apu);
    ApuClockDmc(apu);
    ApuMixSample(apu);

    if (++apu->mixer.accum >= apu->mixer.accum_delta)
    {
        apu->mixer.accum -= apu->mixer.accum_delta;

        apu->mixer.input_buffer[apu->mixer.input_index++] = apu->mixer.sample;
        if (apu->mixer.input_index == apu->mixer.input_len)
        {
            size_t odone;
            error = soxr_process(
                soxr,
                apu->mixer.input_buffer, apu->mixer.input_len,
                NULL,
                apu->mixer.output_buffer, apu->mixer.output_len,
                &odone
            );
            apu->mixer.input_index = 0;
            NonesPutSoundData(apu->mixer.output_buffer, apu->mixer.output_size);
        }
    }
}

void APU_Tick(Apu *apu, bool put_cycle)
{
    SequenceStep step = sequence_table[apu->frame_counter.control.seq_mode][apu->frame_counter.step];

    if (apu->dmc.empty && apu->dmc.bytes_remaining && apu->status.dmc)
    {
        SystemSignalDmcDma();
    }

    if (apu->frame_counter.timer == step.cycles)
    {
        //printf("Sequencer: Framecounter called on cycle: %d cpu cycle: %ld\n", apu->frame_counter.timer, apu->cycles);
        if (step.event == SEQ_CLOCK_QUARTER_FRAME)
        {
            ApuClockEnvelopes(apu);
            ApuClockLinearCounters(apu);
        }
        else if (step.event == SEQ_CLOCK_HALF_FRAME)
        {
            // Half-frame includes quarter frame stuff
            ApuClockEnvelopes(apu);
            ApuClockLinearCounters(apu);
            ApuClockLengthCounters(apu);
            ApuClockSweeps(apu);
        }
        if (step.frame_interrupt)
        {
            apu->status.frame_irq = ~apu->frame_counter.control.irq_inhibit;
            // Don't overwrite the newly set frame irq flag
            apu->clear_frame_irq = false;
        }
        apu->frame_counter.step = (apu->frame_counter.step + 1) % 6;
    }

    ApuClockTriangle(apu);

    if (!put_cycle)
    {
        ApuGetClock(apu);
    }
    else
    {
        ApuPutClock(apu);
    }

    apu->frame_counter.timer %= apu->frame_counter.reload;
    ++apu->frame_counter.timer;
}

void APU_Init(Apu *apu, Arena *arena, const bool swap_duty_cycles, int sample_rate)
{
    memset(apu, 0, sizeof(*apu));
    ApuResetFrameCounter(apu);

    apu->mixer.sample_rate = sample_rate;
    const int samples_per_frame = apu->mixer.sample_rate / 60;
    // Set the sample ratio to be used by soxr, 
    const int soxr_sample_ratio = 3;
    apu->mixer.input_len = samples_per_frame * soxr_sample_ratio;
    apu->mixer.output_len = samples_per_frame;
    apu->mixer.accum_delta = APU_CYCLES_PER_FRAME / apu->mixer.input_len;
    apu->mixer.input_size = apu->mixer.input_len * sizeof(float);
    apu->mixer.output_size = apu->mixer.output_len * sizeof(int16_t);
    apu->mixer.input_buffer = ArenaPush(arena, apu->mixer.input_size);
    apu->mixer.output_buffer = ArenaPush(arena, apu->mixer.output_size);
    //const float max_cutoff = apu->mixer.sample_rate * soxr_sample_ratio * 0.45;
    apu->mixer.lpf_alpha = ComputeFilterAlpha(APU_FREQ, 14000);
    apu->mixer.hpf_alpha = ComputeFilterAlpha(APU_FREQ, 37);

    apu->noise.shift_reg.raw = 1;
    apu->dmc.sample_length = 1;
    apu->dmc.empty = true;
    apu->alignment = 0;
    apu->swap_duty_cycles = swap_duty_cycles;

    soxr_quality_spec_t q_spec = soxr_quality_spec(SOXR_HQ, SOXR_VR);
    soxr_io_spec_t io_spec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_INT16_I);

    soxr = soxr_create(apu->mixer.input_len, apu->mixer.output_len,
                    1, &error, &io_spec, &q_spec, NULL);
}

void APU_Shutdown(Apu *apu)
{
    UNUSED(apu);
    soxr_delete(soxr);
}

void APU_Reset(Apu *apu)
{
    ApuWriteStatus(apu, 0x0);
    ApuResetFrameCounter(apu);
    apu->noise.shift_reg.raw = 1;
    apu->dmc.sample_length = 1;
    apu->dmc.empty = true;
}

