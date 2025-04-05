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
    {3728,  true, false, false },
    {7456,  true, true,  false },
    {11185, true, false, false },
    {0, true, true,  true  }
};

static const SequenceStep sequence_mode_1_table[] =
{
    { 3728,  true,  false, false },
    { 7456,  true,  true,  false },
    { 11185, true,  false, false },
    { 14914, false, false, false },
    { 0, true,  true,  false }
};

static const uint8_t length_counter_table_low[] =
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
};

static const uint8_t length_counter_table_high[] =
{
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

static float generate_duty_sample(int input, float volume)
{
    //printf("sample input: %d\n", input);
    //uint8_t bit = duty_patterns[duty][phase & 7];  // wrap to 0–7
    return (input ? 1.0f : -1.0f) * volume;
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
    apu->pulse1.envelope.start = true;
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
    apu->pulse1.envelope.start = true;
}

static void ApuWritePulse1LengthCounter(Apu *apu, const uint8_t data)
{
    apu->pulse1.length_counter_load = data;

    if (apu->pulse1.length_counter_load < 0x10)
        apu->pulse1.length_counter = length_counter_table_low[apu->pulse1.length_counter_load];
    else
        apu->pulse1.length_counter = length_counter_table_high[apu->pulse1.length_counter_load % 0x10];

}

static void ApuWritePulse2LengthCounter(Apu *apu, const uint8_t data)
{
    apu->pulse2.length_counter_load = data;

    if (apu->pulse2.length_counter_load < 0x10)
        apu->pulse2.length_counter = length_counter_table_low[apu->pulse2.length_counter_load];
    else
        apu->pulse2.length_counter = length_counter_table_high[apu->pulse2.length_counter_load % 0x10];

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
    if (!apu->status.channel1)
    {
        apu->pulse1.length_counter = 0;
    }

    if (!apu->status.channel2)
    {
        apu->pulse2.length_counter = 0;
    }

    if (!apu->status.triangle)
    {
        apu->triangle.length_counter = 0;
        //apu->triangle.linear_counter.control_halt = 1;
    }

    apu->status.dmc_interrupt = 0;
}

static void ApuWritePulse1Sweep(Apu *apu, const uint8_t data)
{
    apu->pulse1.sweep.raw = data;
    apu->pulse1.reload = 1;

    //printf("Set pulse 1 sweep enabled: %d\n", apu->pulse1.sweep.enabled);
    //printf("Set pulse 1 sweep shift count: %d\n", apu->pulse1.sweep.shift_count);
    //printf("Set pulse 1 sweep devider period: %d\n", apu->pulse1.sweep.devider_period);
    //printf("Set pulse 1 sweep negate: %d\n", apu->pulse1.sweep.negate);
}

static void ApuWritePulse2Sweep(Apu *apu, const uint8_t data)
{
    apu->pulse2.sweep.raw = data;
    apu->pulse2.reload = 1;

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
            break;
        case APU_PULSE_1_TIMER_HIGH:
            apu->pulse1.timer_period.high = data & 0x7;
            ApuWritePulse1LengthCounter(apu, data >> 3);
            break;
        case APU_PULSE_2_DUTY:
            ApuWritePulse2Duty(apu, data);
            break;
        case APU_PULSE_2_SWEEP:
            ApuWritePulse2Sweep(apu, data);
            break;
        case APU_PULSE_2_TIMER_LOW:
            apu->pulse2.timer_period.low = data;
            break;
        case APU_PULSE_2_TIMER_HIGH:
            apu->pulse2.timer_period.high = data & 0x7;
            ApuWritePulse2LengthCounter(apu, data >> 3);
            break;
        case APU_TRIANGLE_LINEAR_COUNTER:
            apu->triangle.linear_counter.raw = data;
            break;
        case APU_TRIANGLE_TIMER_LOW:
            apu->triangle.timer_period.low = data;
            break;
        case APU_TRIANGLE_TIMER_HIGH:
            apu->triangle.timer_period.high = data & 0x7;
            apu->triangle.linear_counter_load = data >> 3;
            break;
        case APU_STATUS:
            ApuWriteStatus(apu, data);
            break;
        case APU_FRAME_COUNTER:
            apu->frame_counter.control.raw = data;
            break;
        default:
            printf("Writing to unfinished Apu reg at addr: 0x%04X\n", addr);
            break;
    }
}

static uint8_t ApuReadStatus(Apu *apu)
{
    return apu->status.raw;
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
        case 3728 + 1:
            apu->sequence_step = 1;
            break;
        case 7456 + 1:
            apu->sequence_step = 2;
            break;
        case 11185 + 1:
            apu->sequence_step = 3;
            break;
        case 1:
            apu->sequence_step = 4;
            break;
    }
}

static void ApuFrameMode1SetSeqStep(Apu *apu)
{
    switch (apu->cycle_counter) {
        case 3728 + 1:
            apu->sequence_step = 1;
            break;
        case 7456 + 1:
            apu->sequence_step = 2;
            break;
        case 11185:
            apu->sequence_step = 3;
            break;
        case 14914 + 1:
            apu->sequence_step = 4;
            break;
        case 1:
            apu->sequence_step = 5;
            break;
    }
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

    //printf("Pulse 1 length counter: %d\n", apu->pulse1.length_counter);
    //printf("Pulse 2 length counter: %d\n", apu->pulse2.length_counter);
    //if (apu->triangle.length_counter && !apu->triangle.reg.counter_halt)
    //{
    //    apu->pulse2.length_counter--;
    //}
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

    if (apu->pulse1.length_counter == 0)
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

    if (apu->pulse2.length_counter == 0)
    {
        apu->pulse2.output = 0;
    }
    else
    {
        apu->pulse2.output = duty_cycle_table[apu->pulse2.reg.duty][apu->pulse2.duty_step];
    }

    if (apu->triangle.timer.raw > 0)
        apu->triangle.timer.raw--;
    else
        apu->triangle.timer = apu->triangle.timer_period;

    float sample_l = generate_duty_sample(apu->pulse1.output, apu->pulse1.volume / 15.f);
    float sample_r = generate_duty_sample(apu->pulse2.output, apu->pulse2.volume / 15.f);

    float sample_sum = (sample_l + sample_r) * 0.2f;
    apu->pulse_mix = sample_sum; //95.88 / ((8128.0 / (sample_l + sample_r)) + 100.0);
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
    }

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
    }

    DEBUG_LOG("Pulse 1 envelope counter: %d\n", apu->pulse1.envelope.counter);
    DEBUG_LOG("Pulse 1 envelope decay counter: %d\n", apu->pulse1.envelope.decay_counter);
    DEBUG_LOG("Pulse 2 envelope counter: %d\n", apu->pulse2.envelope.counter);
    DEBUG_LOG("Pulse 2 envelope decay counter: %d\n", apu->pulse2.envelope.decay_counter);

    if (apu->pulse1.reg.constant_volume)
        apu->pulse1.volume = apu->pulse1.reg.volume_env;
    else
        apu->pulse1.volume = apu->pulse1.envelope.decay_counter;

    if (apu->pulse2.reg.constant_volume)
        apu->pulse2.volume = apu->pulse2.reg.volume_env;
    else
        apu->pulse2.volume = apu->pulse2.envelope.decay_counter;
}

void APU_Init(Apu *apu)
{
    memset(apu, 0, sizeof(*apu));
}

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
        if ((cpu_cycles - apu_cycles_to_run) & 1)
        {
            SequenceStep step;
            if (apu->frame_counter.control.sequencer_mode == 0)
            {
                apu->cycle_counter = apu->cycle_counter % 14914;
                ApuFrameMode0SetSeqStep(apu);
                step = sequence_mode_0_table[apu->sequence_step];
                if (apu->sequence_step == 4 && apu->cycle_counter == 1)
                {
                    //downsample_to_44khz(apu->buffer, apu->outbuffer);
                    //NonesPutSoundData(apu);
                }
            }
            else
            {
                apu->cycle_counter = apu->cycle_counter % 18640;
                ApuFrameMode1SetSeqStep(apu);
                step = sequence_mode_1_table[apu->sequence_step];
                if (apu->sequence_step == 5 && apu->cycle_counter == 1)
                {
                    //downsample_to_44khz(apu->buffer, apu->outbuffer);
                    //NonesPutSoundData(apu);
                    //printf("curr sample: %d\n", current_sample);
                    //current_sample = 0;
                }
            }

            if (apu->cycle_counter == step.cycles)
            {
                if (step.quarter_frame_clock)
                {
                    ApuClockLengthCounters(apu);
                    ApuClockEnvelopes(apu);
                }

                if (step.frame_interrupt && !apu->frame_counter.control.interrupt_inhibit)
                    apu->frame_counter.interrupt = true;
            }

            ApuClockTimers(apu);

            if (apu->status.channel1)
            {
                if (apu->pulse1.sweep.enabled)
                    apu->pulse1.timer.raw >>= apu->pulse1.sweep.shift_count;
            }

            if (apu->status.channel2)
            {
                if (apu->pulse2.sweep.enabled)
                    apu->pulse2.timer.raw >>= apu->pulse2.sweep.shift_count;
            }
            apu->cycle_counter++;
        }

        if (apu->current_sample >= 29780)
        {
            NonesPutSoundData(apu);
            apu->current_sample = 0;
        }
        apu->buffer[apu->current_sample++] = apu->pulse_mix;

        apu_cycles_to_run--;
    }
}

void APU_Reset(void)
{

}

static bool irq_triggered = false;

bool APU_IrqTriggered(void)
{
    if (irq_triggered)
    {
        irq_triggered = false; // Clear the flag after reading
        return true;
    }
    return false;
}
