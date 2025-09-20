#include "cw_keyer.h"
#include "pico/stdlib.h"
#include <cstdio>
#include "pwm_audio_sink.h"
#include <cmath>

#define SINE_TABLE_SIZE 256
#define TONE_FREQ 700          // Hz CW tone
#define AUDIO_SAMPLE_RATE 15000

static int16_t sine_table[SINE_TABLE_SIZE];
static float phase = 0.0f;
static float phase_inc;
int16_t audio_samples[PWM_AUDIO_NUM_SAMPLES];

void __not_in_flash_func(cw_keyer::init_sine_table)(void) {
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        double theta = (2.0 * M_PI * i) / SINE_TABLE_SIZE;
        sine_table[i] = (int16_t)(32767.0 * sin(theta));
    }
    phase_inc = (float)SINE_TABLE_SIZE * (float)TONE_FREQ / (float)AUDIO_SAMPLE_RATE;
}

cw_keyer::cw_keyer(uint8_t paddle_type, uint8_t paris_wpm, button &dit, button &dah)
    : dit(dit), dah(dah)
{
    m_paddle_type = paddle_type;
    init_sine_table();
}

void __not_in_flash_func(cw_keyer::set_sample_rate)(uint32_t sample_rate_Hz, uint8_t paris_wpm) {
  dit_length_samples = (sample_rate_Hz * 60) / (50 * paris_wpm);
}

// Generate audio tone (for sidetone)
void __not_in_flash_func(cw_keyer::generate_tone_block)(int16_t *buffer) {
    for (int i = 0; i < PWM_AUDIO_NUM_SAMPLES; i++) {
        buffer[i] = sine_table[(int)phase];
        phase += phase_inc;
        if (phase >= SINE_TABLE_SIZE) phase -= SINE_TABLE_SIZE;
    }
    pwm_audio_sink_push(buffer, 20);
}

// Silence block
void __not_in_flash_func(cw_keyer::generate_silence_block)(int16_t *buffer) {
    for (int i = 0; i < PWM_AUDIO_NUM_SAMPLES; i++) buffer[i] = 0;
    pwm_audio_sink_push(buffer, 20);
}

// Straight keying
bool __not_in_flash_func(cw_keyer::get_straight)() {
    return dit.is_keyed() || dah.is_keyed();
}

// Envelope shaping (fast ramp for short DITs)
int16_t __not_in_flash_func(cw_keyer::key_shape)(bool pressed)
{
    enum e_state { OFF, RISING, ON, FALLING };
    static e_state state = OFF;
    static uint8_t timer = 0;
    int16_t shape = 0;
    const uint8_t ramp_samples = 8; // very fast ramp for short DITs

    switch (state) {
        case OFF:
            generate_silence_block(audio_samples);
            shape = 0;
            if (pressed) { state = RISING; timer = 0; }
            break;

        case RISING:
            shape = (timer * 32767) / ramp_samples;
            if (++timer >= ramp_samples) { state = ON; shape = 32767; }
            break;

        case ON:
            shape = 32767;
            generate_tone_block(audio_samples);
            if (!pressed) { state = FALLING; timer = ramp_samples; }
            break;

        case FALLING:
            shape = (timer * 32767) / ramp_samples;
            if (timer == 0) { state = OFF; shape = 0; }
            else timer--;
            break;
    }

    return shape;
}

// Return sample for envelope + tone
int16_t __not_in_flash_func(cw_keyer::get_sample)()
{
        return key_shape(get_iambic());
}

bool __not_in_flash_func(cw_keyer::get_iambic)()
{
    bool is_keyed = false;

    switch (keyer_state) {
        case IDLE:
            if (dit.is_keyed()) { keyer_state = DIT; counter = dit_length_samples; }
            else if (dah.is_keyed()) { keyer_state = DAH; counter = dit_length_samples * 3; }
            break;

        case DIT:
          is_keyed = true;
          audio_on = true;          // turn on tone
          if (--counter == 0) {
              if (dit.is_keyed()) { // repeat dit
                  counter = dit_length_samples;
                  keyer_state = DIT;
              }
              else if (dah.is_keyed()) {
                  counter = dit_length_samples * 3;
                  keyer_state = DAH;
              }
              else {
                  counter = dit_length_samples;
                  keyer_state = SPACE;
              }
          }
          break;

      case DAH:
          is_keyed = true;
          audio_on = true;          // turn on tone
          if (--counter == 0) {
              if (dah.is_keyed()) { // repeat dah
                  counter = dit_length_samples*3;
                  keyer_state = DAH;
              }
              else if (dit.is_keyed()) {
                  counter = dit_length_samples;
                  keyer_state = DIT;
              }
              else {
                  counter = dit_length_samples;
                  keyer_state = SPACE;
              }
          }
          break;

        case SPACE:
            audio_on = false;        // silence during space
            if (--counter == 0) {
                if (dit.is_keyed()) { keyer_state = DIT; counter = dit_length_samples; }
                else if (dah.is_keyed()) { keyer_state = DAH; counter = dit_length_samples * 3; }
                else { keyer_state = IDLE; }
            }
            break;

        case DIT_SPACE:
            audio_on = false;        // silence during space
            if (--counter == 0) { 
                if (dah.is_keyed()) { counter = dit_length_samples * 3; keyer_state = DAH; }
                else { keyer_state = IDLE; }
            }
            break;

        case DAH_SPACE:
            audio_on = false;        // silence during space
            if (--counter == 0) { 
                if (dit.is_keyed()) { counter = dit_length_samples; keyer_state = DIT; }
                else { keyer_state = IDLE; }
            }
            break;
    }

    return is_keyed;
}
