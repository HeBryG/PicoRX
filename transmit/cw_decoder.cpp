// CW Decoder for RP2350 - Based on OZ1JHM's Goertzel algorithm approach
// Runs well when using noise reduction + narrow filter
// TODO: implement dynamic settings
#include <cstdio>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "cw_decoder.h"
#include "pico/stdlib.h"

goertzel_cw_decoder_t decoder = {0};

// Initialize the Goertzel-based CW decoder
void cw_decoder_init(uint16_t wpm) {
    // Initialize Goertzel coefficients
    int k = (int)(0.5f + ((GOERTZEL_N * TARGET_FREQ) / SAMPLING_FREQ));
    float omega = (2.0f * M_PI * k) / GOERTZEL_N;
    decoder.coeff = 2.0f * cosf(omega);
    
    // Initialize state
    decoder.Q1 = 0.0f;
    decoder.Q2 = 0.0f;
    decoder.sample_index = 0;
    decoder.magnitude_limit = 50000.0f;        // Adjust for your signal levels
    decoder.magnitude_limit_low = 30000.0f;    // Noise floor threshold
    
    // Initialize timing - more conservative noise blanker
    decoder.nb_time = (SAMPLING_FREQ / 1000) * 12; // Increase to 12ms noise blanker
    decoder.high_time_avg = (SAMPLING_FREQ * 60) / (50 * wpm); // Initialize with expected dit time
    decoder.wpm = wpm;
    
    // Initialize character decoding
    decoder.code[0] = '\0';
    decoder.code_index = 0;
    decoder.stop = false;
    
    // Initialize output buffer
    decoder.char_write_index = 0;
    decoder.char_read_index = 0;
    
    printf("CW Decoder initialized: target=%dHz, samples=%d, coeff=%f\n", 
           (int)TARGET_FREQ, GOERTZEL_N, decoder.coeff);
}

// Add a character to the output buffer
void add_decoded_char(char c) {
    decoder.decoded_chars[decoder.char_write_index] = c;
    decoder.char_write_index = (decoder.char_write_index + 1) % 128;
}

// Decode morse code string to character
void decode_morse_code() {
    if (strlen(decoder.code) == 0) return;
    
    // A-Z
    if (strcmp(decoder.code, ".-") == 0) add_decoded_char('A');
    else if (strcmp(decoder.code, "-...") == 0) add_decoded_char('B');
    else if (strcmp(decoder.code, "-.-.") == 0) add_decoded_char('C');
    else if (strcmp(decoder.code, "-..") == 0) add_decoded_char('D');
    else if (strcmp(decoder.code, ".") == 0) add_decoded_char('E');
    else if (strcmp(decoder.code, "..-.") == 0) add_decoded_char('F');
    else if (strcmp(decoder.code, "--.") == 0) add_decoded_char('G');
    else if (strcmp(decoder.code, "....") == 0) add_decoded_char('H');
    else if (strcmp(decoder.code, "..") == 0) add_decoded_char('I');
    else if (strcmp(decoder.code, ".---") == 0) add_decoded_char('J');
    else if (strcmp(decoder.code, "-.-") == 0) add_decoded_char('K');
    else if (strcmp(decoder.code, ".-..") == 0) add_decoded_char('L');
    else if (strcmp(decoder.code, "--") == 0) add_decoded_char('M');
    else if (strcmp(decoder.code, "-.") == 0) add_decoded_char('N');
    else if (strcmp(decoder.code, "---") == 0) add_decoded_char('O');
    else if (strcmp(decoder.code, ".--.") == 0) add_decoded_char('P');
    else if (strcmp(decoder.code, "--.-") == 0) add_decoded_char('Q');
    else if (strcmp(decoder.code, ".-.") == 0) add_decoded_char('R');
    else if (strcmp(decoder.code, "...") == 0) add_decoded_char('S');
    else if (strcmp(decoder.code, "-") == 0) add_decoded_char('T');
    else if (strcmp(decoder.code, "..-") == 0) add_decoded_char('U');
    else if (strcmp(decoder.code, "...-") == 0) add_decoded_char('V');
    else if (strcmp(decoder.code, ".--") == 0) add_decoded_char('W');
    else if (strcmp(decoder.code, "-..-") == 0) add_decoded_char('X');
    else if (strcmp(decoder.code, "-.--") == 0) add_decoded_char('Y');
    else if (strcmp(decoder.code, "--..") == 0) add_decoded_char('Z');
    
    // Numbers 0-9
    else if (strcmp(decoder.code, "-----") == 0) add_decoded_char('0');
    else if (strcmp(decoder.code, ".----") == 0) add_decoded_char('1');
    else if (strcmp(decoder.code, "..---") == 0) add_decoded_char('2');
    else if (strcmp(decoder.code, "...--") == 0) add_decoded_char('3');
    else if (strcmp(decoder.code, "....-") == 0) add_decoded_char('4');
    else if (strcmp(decoder.code, ".....") == 0) add_decoded_char('5');
    else if (strcmp(decoder.code, "-....") == 0) add_decoded_char('6');
    else if (strcmp(decoder.code, "--...") == 0) add_decoded_char('7');
    else if (strcmp(decoder.code, "---..") == 0) add_decoded_char('8');
    else if (strcmp(decoder.code, "----.") == 0) add_decoded_char('9');
    
    // Symbols
    else if (strcmp(decoder.code, "..--..") == 0) add_decoded_char('?');
    else if (strcmp(decoder.code, ".-.-.-") == 0) add_decoded_char('.');
    else if (strcmp(decoder.code, "--..--") == 0) add_decoded_char(',');
    else if (strcmp(decoder.code, "-.-.--") == 0) add_decoded_char('!');
    else if (strcmp(decoder.code, ".--.-.") == 0) add_decoded_char('@');
    else if (strcmp(decoder.code, "---...") == 0) add_decoded_char(':');
    else if (strcmp(decoder.code, "-....-") == 0) add_decoded_char('-');
    else if (strcmp(decoder.code, "-..-.") == 0) add_decoded_char('/');
    else if (strcmp(decoder.code, "-.--.") == 0) add_decoded_char('(');
    else if (strcmp(decoder.code, "-.--.-") == 0) add_decoded_char(')');
    
    else {
        add_decoded_char('?'); // Unknown character
        // printf("Unknown: %s\n", decoder.code);
    }
    
    // Clear the code buffer
    decoder.code[0] = '\0';
    decoder.code_index = 0;
}

// Main CW decoder processing function
void cw_decoder_process(int32_t audio_sample, uint32_t sample_counter) {
    // Collect samples for Goertzel algorithm
    decoder.test_data[decoder.sample_index] = (int16_t)audio_sample;
    decoder.sample_index++;
    
    // When we have enough samples, run Goertzel
    if (decoder.sample_index >= GOERTZEL_N) {
        decoder.sample_index = 0;
        
        // Reset Goertzel state
        decoder.Q1 = 0.0f;
        decoder.Q2 = 0.0f;
        
        // Run Goertzel algorithm
        for (int i = 0; i < GOERTZEL_N; i++) {
            float Q0 = decoder.coeff * decoder.Q1 - decoder.Q2 + (float)decoder.test_data[i];
            decoder.Q2 = decoder.Q1;
            decoder.Q1 = Q0;
        }
        
        // Calculate magnitude
        float magnitude_squared = (decoder.Q1 * decoder.Q1) + 
                                 (decoder.Q2 * decoder.Q2) - 
                                 (decoder.Q1 * decoder.Q2 * decoder.coeff);
        float magnitude = sqrtf(magnitude_squared);
        
        // Adaptive threshold with better noise rejection
        static float noise_floor = 30000.0f;
        static int noise_samples = 0;
        
        if (magnitude < decoder.magnitude_limit_low) {
            // Update noise floor estimate
            noise_floor = (noise_floor * 0.99f) + (magnitude * 0.01f);
            noise_samples++;
        }
        
        if (magnitude > (noise_floor * 3.0f)) {
            // Only update threshold when we have a strong signal
            decoder.magnitude_limit = decoder.magnitude_limit + 
                                    ((magnitude - decoder.magnitude_limit) / 32.0f); // Very slow adaptation
        }
        
        // Set dynamic minimum threshold based on noise floor
        float min_threshold = noise_floor * 2.5f;
        if (decoder.magnitude_limit < min_threshold) {
            decoder.magnitude_limit = min_threshold;
        }
        
        // Use higher threshold percentage to reduce false triggers
        float threshold = decoder.magnitude_limit * 0.85f;
        bool new_state = (magnitude > threshold);
        
        // Stronger hysteresis to reject noise bursts
        static int state_counter = 0;
        if (new_state != decoder.real_state) {
            if (new_state) {
                state_counter++;
                if (state_counter >= 3) { // Require 3 consecutive detections
                    decoder.real_state = true;
                    state_counter = 0;
                }
            } else {
                state_counter--;
                if (state_counter <= -2) { // Allow faster release
                    decoder.real_state = false;
                    state_counter = 0;
                }
            }
        } else {
            state_counter = 0; // Reset if state is stable
        }
        
        // Debug output every 100 samples to reduce spam
        static int debug_counter = 0;
        if (++debug_counter >= 100) {
            
            debug_counter = 0;
        }
        
        // Noise blanker with better startup handling
        static bool decoder_ready = false;
        if (!decoder_ready && noise_samples > 100) {
            // Wait for noise floor to stabilize before starting decode
            decoder_ready = true;
            // printf("CW Decoder ready - noise floor: %.0f\n", noise_floor);
        }
        
        if (!decoder_ready) {
            // Skip processing until noise floor is established
            decoder.real_state_before = decoder.real_state;
            decoder.filtered_state_before = decoder.filtered_state;
            return;
        }
        
        if (decoder.real_state != decoder.real_state_before) {
            decoder.last_start_time = sample_counter;
        }
        
        // Longer noise blanker time for better noise rejection
        if ((sample_counter - decoder.last_start_time) > decoder.nb_time) {
            if (decoder.real_state != decoder.filtered_state) {
                decoder.filtered_state = decoder.real_state;
                
                // Add extra delay before first character to ensure clean start
                static bool first_tone = true;
                if (first_tone && decoder.filtered_state) {
                    // printf("First tone detected, ready to decode...\n");
                    first_tone = false;
                }
            }
        }
        
        // Process state changes
        if (decoder.filtered_state != decoder.filtered_state_before) {
            decoder.stop = false;
            
            if (decoder.filtered_state) {
                // Tone started
                decoder.start_time_high = sample_counter;
                decoder.low_duration = sample_counter - decoder.start_time_low;
                
                // Calculate speed adjustment factors
                float lack_time = 1.0f;
                if (decoder.wpm > 25) lack_time = 1.0f;
                if (decoder.wpm > 30) lack_time = 1.2f;
                if (decoder.wpm > 35) lack_time = 1.5f;
                
                // Check for character or word spaces
                if (decoder.low_duration > (decoder.high_time_avg * (2 * lack_time)) && 
                    decoder.low_duration < (decoder.high_time_avg * (5 * lack_time))) {
                    // Character space
                    decode_morse_code();
                    // printf("/");
                } else if (decoder.low_duration >= (decoder.high_time_avg * (5 * lack_time))) {
                    // Word space
                    decode_morse_code();
                    add_decoded_char(' ');
                    // printf(" ");
                }
                
            } else {
                // Tone ended
                decoder.start_time_low = sample_counter;
                decoder.high_duration = sample_counter - decoder.start_time_high;
                
                // Update average dit time (rolling 3-sample average)
                if (decoder.high_duration < (2 * decoder.high_time_avg) || decoder.high_time_avg == 0) {
                    decoder.high_time_avg = (decoder.high_duration + decoder.high_time_avg + decoder.high_time_avg) / 3;
                }
                
                // Handle speed decreases
                if (decoder.high_duration > (5 * decoder.high_time_avg)) {
                    decoder.high_time_avg = (decoder.high_duration + decoder.high_time_avg) / 2;
                }
                
                // Classify as dit or dah
                if (decoder.high_duration < (decoder.high_time_avg * 2) && 
                    decoder.high_duration > (decoder.high_time_avg * 0.6f)) {
                    // Dit
                    if (decoder.code_index < 19) {
                        decoder.code[decoder.code_index++] = '.';
                        decoder.code[decoder.code_index] = '\0';
                    }
                    // printf(".");
                    
                } else if (decoder.high_duration > (decoder.high_time_avg * 2) && 
                          decoder.high_duration < (decoder.high_time_avg * 6)) {
                    // Dah
                    if (decoder.code_index < 19) {
                        decoder.code[decoder.code_index++] = '-';
                        decoder.code[decoder.code_index] = '\0';
                    }
                    // printf("-");
                    
                    // Update WPM calculation
                    uint16_t calculated_wpm = 1200 / (decoder.high_duration / 3);
                    decoder.wpm = (decoder.wpm + calculated_wpm) / 2;
                }
            }
        }
        
        // Timeout - decode current character if no activity
        if ((sample_counter - decoder.start_time_low) > (decoder.high_duration * 6) && !decoder.stop) {
            decode_morse_code();
            decoder.stop = true;
        }
        
        // Update state
        decoder.real_state_before = decoder.real_state;
        decoder.filtered_state_before = decoder.filtered_state;
    }
}

// Get decoded character (returns 0 if none available)
char cw_decoder_get_char(void) {
    if (decoder.char_read_index != decoder.char_write_index) {
        char c = decoder.decoded_chars[decoder.char_read_index];
        decoder.char_read_index = (decoder.char_read_index + 1) % 128;
        return c;
    }
    return 0;
}

// Get current WPM estimate
uint16_t cw_decoder_get_wpm(void) {
    return decoder.wpm;
}

// Get signal strength (magnitude)
float cw_decoder_get_magnitude_limit(void) {
    return decoder.magnitude_limit;
}