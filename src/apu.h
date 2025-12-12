#ifndef APU_H
#define APU_H

typedef struct
{
    uint16_t counter;
    uint16_t decay_counter;
    bool start;
} ApuEnvelope;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t volume_env : 4;
        uint8_t constant_volume : 1;
        uint8_t counter_halt : 1;
        uint8_t duty : 2;
    };
} ApuPulseReg;

typedef union
{
    uint8_t raw;
    struct
    {
        // Shift count (number of bits).
        // If SSS is 0, then behaves like E=0. 
        uint8_t shift_count : 3;
        // Negate flag:
        // 0: add to period, sweeping toward lower frequencies;
        // 1: subtract from period, sweeping toward higher frequencies
        uint8_t negate : 1;
        // The divider's period is P + 1 half-frames
        uint8_t devider_period : 3;
        // Enabled flag 
        uint8_t enabled : 1;
    };
} ApuPulseSweepReg;

typedef union
{
    uint16_t raw;
    struct
    {
        uint16_t low : 8;
        uint16_t high : 3;
    };
} ApuTimer;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t reload_value : 7;
        uint8_t control_halt : 1;
    };
} ApuTriangleLinearCounter;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t pulse1 : 1;
        uint8_t pulse2 : 1;
        uint8_t triangle : 1;
        uint8_t noise : 1;
        uint8_t dmc : 1;
        uint8_t open_bus : 1;
        uint8_t frame_irq : 1;
        uint8_t dmc_irq : 1;
    };
} ApuStatus;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t padding : 6;
        uint8_t irq_inhibit : 1;
        uint8_t seq_mode : 1;
    };
} ApuFrameCounterControl;

// The NES APU frame counter (or frame sequencer) generates low-frequency clocks for the channels and an optional 60 Hz interrupt.
// The name "frame counter" might be slightly misleading because the clocks have nothing to do with the video signal.
// The frame counter contains the following: divider, looping clock sequencer, frame interrupt flag.
// The sequencer is clocked on every other CPU cycle, so 2 CPU cycles = 1 APU cycle.
// The sequencer keeps track of how many APU cycles have elapsed in total, and each step of the sequence will
// occur once that total has reached the indicated amount (with an additional delay of one CPU cycle for the quarter and half frame signals).
// Once the last step has executed, the count resets to 0 on the next APU cycle. 
typedef struct
{
    ApuFrameCounterControl control;
    int timer;
    int reload;
    int reset_delay;
    int16_t step;
    bool interrupt;
    bool reset;
} ApuFrameCounter;

// When the timer clocks the shift register, the following actions occur in order:
// Feedback is calculated as the exclusive-OR of bit 0 and one other bit: bit 6 if Mode flag is set, otherwise bit 1.
// The shift register is shifted right by one bit.
// Bit 14, the leftmost bit, is set to the feedback calculated earlier.
typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t volume_env : 4;
        uint8_t constant_volume : 1;
        uint8_t counter_halt : 1;
    };
} ApuNoiseReg;

typedef union 
{
    uint16_t raw : 15;
    struct
    {
        uint16_t bit0 : 1;
        uint16_t bit1 : 1;
        uint16_t : 4;
        uint16_t bit6 : 1;
        uint16_t : 7;
        uint16_t bit14 : 1;
    };
} ApuNoiseShiftReg;

typedef union
{
    uint16_t raw;
    struct
    {
        uint8_t period : 4;
        uint8_t : 3;
        uint8_t mode : 1;
    };
} ApuNoisePeriodReg;

typedef union
{
    uint8_t raw;
    struct
    {
        uint8_t freq_rate : 4;
        uint8_t padding : 2;
        uint8_t loop : 1;
        uint8_t irq : 1;
    };
} ApuDmcControl;

typedef struct
{
    ApuEnvelope envelope;
    uint16_t sweep_counter;
    uint16_t target_period;
    uint16_t volume;
    uint16_t output;
    uint16_t length_counter;
    int16_t duty_step;
    // External
    ApuTimer timer_period;
    // Internal timer
    ApuTimer timer;
    ApuPulseReg reg;
    ApuPulseSweepReg sweep_reg;
    bool reload;
    bool muting;
    uint8_t length_counter_load : 5;
} ApuPulse;

typedef struct
{
    struct
    {
        float *input_buffer;
        int16_t *output_buffer;
        float sample;
        float sample_rate;
        float accum;
        float accum_delta;
        float hpf_sample;
        float lpf_alpha;
        float hpf_alpha;
        int input_index;
        int input_len;
        int output_len;
        int input_size;
        int output_size;
    } mixer;

    ApuPulse pulse1;
    ApuPulse pulse2;

    struct {
        ApuTriangleLinearCounter reg;
        ApuTimer timer_period;
        ApuTimer timer;
        uint16_t seq_pos;
        bool reload;
        uint16_t output;
        uint16_t length_counter;
        uint16_t linear_counter;
        uint8_t length_counter_load : 5;
    } triangle;

    struct {
        ApuNoiseReg reg;
        ApuEnvelope envelope;
        ApuNoisePeriodReg period_reg;
        ApuNoiseShiftReg shift_reg;
        ApuTimer timer_period;
        ApuTimer timer;
        uint16_t volume;
        uint16_t output;
        uint16_t length_counter;
        uint8_t length_counter_load : 5;
    } noise;

    struct {
        ApuDmcControl control;
        uint16_t timer_period;
        uint16_t timer;
        uint16_t sample_addr;
        uint16_t sample_length;
        uint8_t sample_buffer;
        uint16_t bytes_remaining;
        uint16_t addr_counter;
        uint16_t bits_remaining;
        uint8_t shift_reg;
        bool empty;
        bool silence;
        bool restart;
        bool looped;
        uint8_t output_level : 7;
    } dmc;

    ApuFrameCounter frame_counter;
    ApuStatus status;

    int alignment;
    bool clear_frame_irq;
    bool swap_duty_cycles;
} Apu;

typedef enum 
{
    SEQ_CLOCK_NONE,
    // Envelopes & triangle's linear counter
    SEQ_CLOCK_QUARTER_FRAME,
    // Length counters & sweep units
    SEQ_CLOCK_HALF_FRAME,
} SequencerClockEvent;

typedef struct
{
    int cycles;
    SequencerClockEvent event;
    // Frame interrupt flag 
    bool frame_interrupt;
} SequenceStep;

#define APU_FREQ 894886.5
#define APU_CYCLES_PER_FRAME 14890.0f
#define LOW_PASS_CUTOFF 14000

#define APU_PULSE_1_DUTY 0x4000
#define APU_PULSE_1_SWEEP 0x4001
#define APU_PULSE_1_TIMER_LOW 0x4002
#define APU_PULSE_1_TIMER_HIGH 0x4003

#define APU_PULSE_2_DUTY 0x4004
#define APU_PULSE_2_SWEEP 0x4005
#define APU_PULSE_2_TIMER_LOW 0x4006
#define APU_PULSE_2_TIMER_HIGH 0x4007

#define APU_TRIANGLE_LINEAR_COUNTER 0x4008
#define APU_TRIANGLE_TIMER_LOW 0x400A
#define APU_TRIANGLE_TIMER_HIGH 0x400B

#define APU_NOISE 0x400C
#define APU_NOISE_PERIOD 0x400E
#define APU_NOISE_LENGTH_COUNTER_LOAD 0x400F

#define APU_DMC_CONTROL 0x4010
#define APU_DMC_DIRECT_LOAD 0x4011
#define APU_DMC_SAMPLE_ADDR 0x4012
#define APU_DMC_SAMPLE_LENGTH 0x4013

#define APU_STATUS 0x4015
#define APU_FRAME_COUNTER 0x4017

uint8_t ApuReadStatus(Apu *apu, const uint8_t bus_data);
void WriteAPURegister(Apu *apu, const uint16_t addr, const uint8_t data);
bool PollApuIrqs(Apu *apu);
void ApuDmcDmaUpdate(Apu *apu);
void APU_Init(Apu *apu, Arena *arena, const bool swap_duty_cycles, int sample_rate);
void APU_Tick(Apu *apu, bool put_cycle);
void APU_Reset(Apu *apu);
void APU_Shutdown(Apu *apu);

#endif
