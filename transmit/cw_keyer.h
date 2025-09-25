#ifndef __CW_KEYER__
#define __CW_KEYER__

#include <cstdint>
#include "button.h"

const uint8_t STRAIGHT = 0;
const uint8_t IAMBIC_A = 1;
const uint8_t IAMBIC_B = 2;
#define SINE_TABLE_SIZE 256

enum e_keyer_state {IDLE, DIT, DAH, DIT_SPACE, DAH_SPACE, SPACE};

class cw_keyer
{
private:
    uint16_t counter = 0;
    bool both_pressed = false;
    int8_t timer;
    uint32_t keyer_state_last_change = 0;
    
    // Private methods
    bool dit_is_pressed();
    bool dah_is_pressed();
    bool get_straight();
    
    // Button references
    button &dit;
    button &dah;

public:
    // ===================================================================
    // PUBLIC VARIABLES - These need to be accessible from transmit_cw()
    // ===================================================================
    
    // Sine wave generation
    float phase;
    float phase_inc;
    int16_t sine_table[SINE_TABLE_SIZE];
    
    // Timing variables
    uint32_t sample_counter;
    uint32_t dit_length_samples;        // Changed from uint16_t to uint32_t
    uint32_t dah_length_samples;        // Changed from uint16_t to uint32_t
    uint32_t element_space_samples;     // Changed from uint16_t to uint32_t
    uint32_t element_start_sample;
    uint32_t element_end_sample;
    
    // State variables
    e_keyer_state keyer_state = IDLE;
    bool tone_active;
    uint8_t m_paddle_type;
    bool is_keyed;
    // ===================================================================
    // PUBLIC METHODS
    // ===================================================================
    
    // Constructor (fixed - only one declaration)
    cw_keyer(uint8_t paddle_type, uint8_t paris_wpm, button &dit, button &dah);
    
    // Core keyer functions
    bool update_keyer_state();
    int16_t get_sample();
    int16_t key_shape(bool on);
    void reset_sample_counter();
    void set_sample_rate(uint32_t sample_rate_Hz, uint8_t paris_wpm);
    void change_paddle_type(uint8_t paddle_type);
    
    // Audio generation
    void init_sine_table();
    void generate_tone_block();
    void generate_audio_samples(int16_t* buffer, int num_samples);

    

};

#endif