// CW Decoder for RP2350
#ifndef CW_DECOD_H
#define CW_DECOD_H
#include <math.h>
#include <stdbool.h>
#include "pico/stdlib.h"
// Goertzel algorithm parameters
const int GOERTZEL_N = 48;             // Number of samples for Goertzel
const int CW_DECODER_TARGET_FREQ = 700;       // CW tone frequency to detect
const int CW_DECODER_SAMPLING_FREQ = 15000;     // sample rate
// Timing and state variables
typedef struct {
    // Goertzel algorithm state
    float coeff;
    float Q1, Q2;
    float magnitude_limit;
    float magnitude_limit_low;
    int16_t test_data[GOERTZEL_N];
    uint8_t sample_index;
    
    // State detection
    bool real_state;
    bool real_state_before;
    bool filtered_state;
    bool filtered_state_before;
    
    // Timing variables
    uint32_t last_start_time;
    uint32_t start_time_high;
    uint32_t start_time_low;
    uint32_t high_duration;
    uint32_t low_duration;
    uint32_t high_time_avg;
    uint32_t low_time_avg;
    
    // Noise blanker
    uint32_t nb_time;              // Noise blanker time in samples
    
    // Character decoding
    char code[20];
    uint8_t code_index;
    bool stop;
    uint16_t wpm;
    
    // Output buffer
    char decoded_chars[128];
    uint8_t char_write_index;
    uint8_t char_read_index;
    
} goertzel_cw_decoder_t;
extern goertzel_cw_decoder_t decoder;
void cw_decoder_init(uint16_t wpm);
void add_decoded_char(char c);

void decode_morse_code();
void cw_decoder_update_settings(uint16_t wpm, uint8_t m_limit, uint8_t m_limit_low, uint8_t t_freq, uint8_t s_freq, int nb_ms);
void cw_decoder_process(int32_t audio_sample, uint32_t sample_counter);
char cw_decoder_get_char(void);
uint16_t cw_decoder_get_wpm(void);
float cw_decoder_get_magnitude_limit(void);
#endif