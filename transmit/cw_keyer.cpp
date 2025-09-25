// CW KEYER BASED ON dawsonjon W9RAN branch WORK
// INTEGRATION + MODIFICATIONS BY EA4IIF
#include "cw_keyer.h"
#include "pico/stdlib.h"
#include "pwm_audio_sink.h"
#include "hardware/pwm.h"
#include "pins.h"
#include <cmath>
#include <string.h>
#include <cstdio>

#define TONE_FREQ 700          // Hz CW tone
#define AUDIO_SAMPLE_RATE 15000    // From audio_sample_rate

uint16_t ramp_length_samples;
int16_t audio_samples[PWM_AUDIO_NUM_SAMPLES];
int audio_samples_fill = 0;


cw_keyer::cw_keyer(uint8_t paddle_type, uint8_t paris_wpm, button &dit, button &dah)
    : dit(dit), dah(dah)
{
    timer = 0;
    m_paddle_type = paddle_type;
    set_sample_rate(15000, paris_wpm);
    init_sine_table();
}

void cw_keyer::change_paddle_type(uint8_t paddle_type) {
    m_paddle_type = paddle_type;
}

void __not_in_flash_func(cw_keyer::init_sine_table)(void) {
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        double theta = (2.0 * M_PI * i) / SINE_TABLE_SIZE;
        sine_table[i] = (int16_t)(32767.0 * sin(theta));
    }
    phase_inc = (float)SINE_TABLE_SIZE * (float)TONE_FREQ / (float)AUDIO_SAMPLE_RATE;
}

void __not_in_flash_func(cw_keyer::set_sample_rate)(uint32_t sample_rate_Hz, uint8_t paris_wpm) {
    // PARIS = 50 dot units (dits + spaces)
    // WPM = words per minute
    // Dit time in milliseconds = 1200 / WPM
    // Dit time in samples = (sample_rate * 1200) / (WPM * 1000)
    
    dit_length_samples = (sample_rate_Hz * 60) / (50 * paris_wpm);
    dah_length_samples = dit_length_samples * 3;
    element_space_samples = dit_length_samples;
    
    // Ramp should be short relative to dit length (max 10% of dit)
    ramp_length_samples = dit_length_samples / 10;
    if (ramp_length_samples < 5) {
        ramp_length_samples = 5; // Minimum 5 samples for smooth ramp
    }
    if (ramp_length_samples > 20) {
        ramp_length_samples = 20; // Maximum 20 samples to keep crisp
    }
    
    /* printf("CW Timing: %d WPM, Dit=%lu samples (%.1fms), Ramp=%d samples\n", 
           paris_wpm, dit_length_samples, 
           (float)dit_length_samples * 1000.0f / sample_rate_Hz,
           ramp_length_samples); */
}

bool __not_in_flash_func(cw_keyer::get_straight)()
{
  return dit.is_keyed() || dah.is_keyed(); 
}
int16_t __not_in_flash_func(cw_keyer::key_shape)(bool pressed)
{
  enum e_state{off, rising, on, falling};
  static e_state state = off;
  static int32_t ramp_counter = 0;
  int16_t shape = 0;

  if (pressed) {
      if (state == off) {
          state = rising;
          ramp_counter = 0;
      } else if (state == falling) {
          // If a key is pressed during a falling ramp, reverse to rising
          state = rising;
      }
  } else {
      if (state == on) {
          state = falling;
          ramp_counter = ramp_length_samples;
      } else if (state == rising) {
          // If key is released during a rising ramp, reverse to falling
          state = falling;
      }
  }

  switch(state)
  {
      case off:
          shape = 0;
          break;

      case rising:
          if(ramp_counter < ramp_length_samples) {
              shape = (32767 * ramp_counter) / ramp_length_samples;
              ramp_counter++;
          } else {
              shape = 32767;
              state = on;
          }
          break;

      case on:
          shape = 32767;
          break;

      case falling:
          if(ramp_counter > 0) {
              shape = (32767 * ramp_counter) / ramp_length_samples;
              ramp_counter--;
          } else {
              shape = 0;
              state = off;
          }
          break;
  }
  return shape;
}

int16_t __not_in_flash_func(cw_keyer::get_sample)()
{
    int16_t shape = key_shape(get_straight());
    return shape;
}

void cw_keyer::reset_sample_counter() {
    sample_counter = 0;
    element_start_sample = 0;
    element_end_sample = 0;
    tone_active = false;
    keyer_state = IDLE;
    phase = 0.0f;
}

bool __not_in_flash_func(cw_keyer::update_keyer_state)() {
    is_keyed = false;
    
    // Handle straight key mode
    if (m_paddle_type == STRAIGHT) {
        bool key_pressed = dit.is_keyed() || dah.is_keyed();
        if (key_pressed) {
            keyer_state = DIT; // Use DIT state for straight key
            tone_active = true;
            return true;
        } else {
            tone_active = false;
            keyer_state = IDLE;
            return false;
        }
    }
    
    // IAMBIC MODE state machine
    switch (keyer_state) {
        case IDLE:
            if (dit.is_keyed()) {
                keyer_state = DIT;
                element_start_sample = sample_counter;
                element_end_sample = sample_counter + dit_length_samples;
                tone_active = true;
            } else if (dah.is_keyed()) {
                keyer_state = DAH;
                element_start_sample = sample_counter;
                element_end_sample = sample_counter + dah_length_samples;
                tone_active = true;
            }
            break;
            
        case DIT:
            if (sample_counter >= element_end_sample) {
                // Dit finished, start inter-element space
                keyer_state = SPACE;
                element_start_sample = sample_counter;
                element_end_sample = sample_counter + element_space_samples;
                tone_active = false;
                
                // Iambic memory: if dah is pressed during dit, queue it
                if (dah.is_keyed()) {
                    keyer_state = DAH_SPACE;
                }
            } else {
                is_keyed = true; // Keep tone on during dit
                
                // Iambic B: both paddles pressed during element
                if (m_paddle_type == IAMBIC_B && dah.is_keyed()) {
                    // Remember to play dah after space
                    // Don't interrupt current dit
                }
            }
            break;
            
        case DAH:
            if (sample_counter >= element_end_sample) {
                // Dah finished, start inter-element space
                keyer_state = SPACE;
                element_start_sample = sample_counter;
                element_end_sample = sample_counter + element_space_samples;
                tone_active = false;
                
                // Iambic memory: if dit is pressed during dah, queue it
                if (dit.is_keyed()) {
                    keyer_state = DIT_SPACE;
                }
            } else {
                is_keyed = true; // Keep tone on during dah
                
                // Iambic B: both paddles pressed during element
                if (m_paddle_type == IAMBIC_B && dit.is_keyed()) {
                    // Remember to play dit after space
                    // Don't interrupt current dah
                }
            }
            break;
            
        case SPACE:
            if (sample_counter >= element_end_sample) {
                // Space finished, check for next element
                if (dit.is_keyed()) {
                    keyer_state = DIT;
                    element_start_sample = sample_counter;
                    element_end_sample = sample_counter + dit_length_samples;
                    tone_active = true;
                } else if (dah.is_keyed()) {
                    keyer_state = DAH;
                    element_start_sample = sample_counter;
                    element_end_sample = sample_counter + dah_length_samples;
                    tone_active = true;
                } else {
                    keyer_state = IDLE;
                }
            }
            break;
            
        case DAH_SPACE:
            // Waiting in space to play queued dah
            if (sample_counter >= element_end_sample) {
                keyer_state = DAH;
                element_start_sample = sample_counter;
                element_end_sample = sample_counter + dah_length_samples;
                tone_active = true;
            }
            break;
            
        case DIT_SPACE:
            // Waiting in space to play queued dit
            if (sample_counter >= element_end_sample) {
                keyer_state = DIT;
                element_start_sample = sample_counter;
                element_end_sample = sample_counter + dit_length_samples;
                tone_active = true;
            }
            break;
    }
    
    return tone_active;
}

void __not_in_flash_func(cw_keyer::generate_tone_block)() {
    for (int i = 0; i < PWM_AUDIO_NUM_SAMPLES; i++) {
        bool keyed = update_keyer_state();
        int idx = ((int)phase) & (SINE_TABLE_SIZE - 1);
        int16_t raw_sine_sample = sine_table[idx];

        // AUDIO: no shaping here, just gate with tone_active
        audio_samples[i] = keyed ? raw_sine_sample : 0;

        // advance phase
        phase += phase_inc;
        if (phase >= (float)SINE_TABLE_SIZE) phase -= (float)SINE_TABLE_SIZE;
    }

    // debug first sample of the block:
    // printf("audio_samples[0]=%d phase_inc=%f tone=%d shape=%d\n", audio_samples[0], phase_inc, tone_active, key_shape(tone_active));

    // Push to audio sink
    pwm_audio_sink_push(audio_samples, 25);
}
