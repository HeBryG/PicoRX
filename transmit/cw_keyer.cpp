#include "cw_keyer.h"
#include "pico/stdlib.h"
#include "pwm_audio_sink.h"
#include <cmath>
#include <string.h>
#include <cstdio>

#define SINE_TABLE_SIZE 256
#define TONE_FREQ 700          // Hz CW tone
#define AUDIO_SAMPLE_RATE 15000
uint16_t ramp_length_samples;
static int16_t sine_table[SINE_TABLE_SIZE];
static float phase = 0.0f;
static float phase_inc;
int16_t audio_samples[PWM_AUDIO_NUM_SAMPLES];
int audio_samples_fill = 0;
cw_keyer::cw_keyer(uint8_t paddle_type, uint8_t paris_wpm, button &dit, button &dah)
    : dit(dit), dah(dah)
{
    timer = 0;

    m_paddle_type = paddle_type;
    init_sine_table();
}

void __not_in_flash_func(cw_keyer::init_sine_table)(void) {
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        double theta = (2.0 * M_PI * i) / SINE_TABLE_SIZE;
        sine_table[i] = (int16_t)(32767.0 * sin(theta));
    }
    phase_inc = (float)SINE_TABLE_SIZE * (float)TONE_FREQ / (float)AUDIO_SAMPLE_RATE;
}

void __not_in_flash_func(cw_keyer::set_sample_rate)(uint32_t sample_rate_Hz, uint8_t paris_wpm) {
  dit_length_samples = (sample_rate_Hz * 60) / (50 * paris_wpm);
  // Set a fixed, fast ramp time (e.g., 5ms) for crisp on/off keying and to avoid attenuation on short dits.
  // This value should be used for PA key shaping, not the sidetone.
  ramp_length_samples = (sample_rate_Hz / 1000) * 5; // 5ms ramp
  // Add a minimum ramp time to avoid a harsh tone for extremely fast speeds
  if (ramp_length_samples < 5) {
      ramp_length_samples = 5;
  }
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
    bool keyed = get_iambic() || get_straight();
    return key_shape(keyed);
}


bool __not_in_flash_func(cw_keyer::get_iambic)() {
    bool is_keyed = false;

    switch (keyer_state) {
        case IDLE:
            // Wait for a paddle press
            if (dit.is_keyed()) {
                keyer_state = DIT;
                counter = dit_length_samples;
            } else if (dah.is_keyed()) {
                keyer_state = DAH;
                counter = dit_length_samples * 3;
            }
            break;

        case DIT:
            is_keyed = true;
            // If the dit paddle is released, transition to the next state
            if (!dit.is_keyed()) {
                // Check for a memory of a pending dah or if both paddles are released
                if (dah.is_keyed()) {
                    keyer_state = DAH_SPACE;
                    counter = dit_length_samples;
                } else {
                    keyer_state = SPACE;
                    counter = dit_length_samples;
                }
            } else if (m_paddle_type == IAMBIC_B && dit.is_keyed() && dah.is_keyed()) {
                // For IAMBIC_B, pressing the other paddle during an element
                // signals the next element.
                keyer_state = DAH_SPACE;
                counter = dit_length_samples;
            } else if (!counter--) {
                // If the element's duration is complete, transition to space.
                keyer_state = SPACE;
                counter = dit_length_samples;
            }
            break;

        case DAH:
            is_keyed = true;
            // If the dah paddle is released, transition to the next state
            if (!dah.is_keyed()) {
                if (dit.is_keyed()) {
                    keyer_state = DIT_SPACE;
                    counter = dit_length_samples;
                } else {
                    keyer_state = SPACE;
                    counter = dit_length_samples;
                }
            } else if (m_paddle_type == IAMBIC_B && dit.is_keyed() && dah.is_keyed()) {
                keyer_state = DIT_SPACE;
                counter = dit_length_samples;
            } else if (!counter--) {
                keyer_state = SPACE;
                counter = dit_length_samples;
            }
            break;

        case SPACE:
            // This state handles the inter-element space and the memory function.
            if (dit.is_keyed()) {
                keyer_state = DIT;
                counter = dit_length_samples;
            } else if (dah.is_keyed()) {
                keyer_state = DAH;
                counter = dit_length_samples * 3;
            } else if (!counter--) {
                // If no paddle is pressed and the space timeout is over, go back to IDLE
                keyer_state = IDLE;
            }
            break;

        case DAH_SPACE:
            // This state is the transition space before a forced DAH
            if (!counter--) {
                keyer_state = DAH;
                counter = dit_length_samples * 3;
            }
            break;
        
        case DIT_SPACE:
            // This state is the transition space before a forced DIT
            if (!counter--) {
                keyer_state = DIT;
                counter = dit_length_samples;
            }
            break;
    }

    return is_keyed;
}

void __not_in_flash_func(cw_keyer::generate_tone_block)() {
    // Get the current keyed state (true if either key is pressed).
    bool keyed = get_iambic() || get_straight();
    
    // Fill the audio buffer with samples.
    for (int i = 0; i < PWM_AUDIO_NUM_SAMPLES; i++) {
        // Generate a raw sine wave sample.
        int16_t raw_sine_sample = sine_table[(int)phase];
        
        // Sidetone is now a simple on/off, not shaped.
        if (keyed) {
            audio_samples[i] = raw_sine_sample;
        } else {
            audio_samples[i] = 0;
        }
        
        // Increment phase for the next sample.
        phase += phase_inc;
        if (phase >= SINE_TABLE_SIZE) {
            phase -= SINE_TABLE_SIZE;
        }
    }
    
    // Push the filled buffer to the audio sink.
    // The '20' parameter seems to be a hardcoded gain, but it's
    // already in your code, so we'll keep it for now.
    pwm_audio_sink_push(audio_samples, 20);
}



void __not_in_flash_func(cw_keyer::handle_keyer_monitor)() {
    generate_tone_block();
}
