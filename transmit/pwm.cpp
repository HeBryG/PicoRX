//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Copyright (c) Jonathan P Dawson 2023
// filename: pwm.cpp
// description: PWM Magnitude for Ham Transmitter
// License: MIT
//

#include "pwm.h"
#include "stdio.h"

pwm::pwm(const uint8_t magnitude_pin) {
  printf("PWM INIT==========\n");
  m_magnitude_pin = magnitude_pin;
  m_pwm_slice = pwm_gpio_to_slice_num(magnitude_pin);
  m_pwm_channel = pwm_gpio_to_channel(magnitude_pin);
  
  gpio_set_function(magnitude_pin, GPIO_FUNC_PWM);
  gpio_set_drive_strength(magnitude_pin, GPIO_DRIVE_STRENGTH_12MA);
  
  // Configure for uSDX-style 32kHz PWM frequency
  const uint16_t pwm_max = 254; // 8-bit PWM (0-254)
  const float clock_div = 15.26f; // Gives ~32.1kHz PWM frequency
  
  pwm_config config = pwm_get_default_config();
  pwm_config_set_clkdiv(&config, clock_div);
  pwm_config_set_wrap(&config, pwm_max);
  pwm_config_set_output_polarity(&config, false, false);
  pwm_init(m_pwm_slice, &config, true);
  pwm_set_chan_level(m_pwm_slice, m_pwm_channel, 0);
  
  printf("PWM configured: slice=%d, channel=%d, freq=%.1fkHz\n", 
         m_pwm_slice, m_pwm_channel, 125000000.0f / (clock_div * (pwm_max + 1)) / 1000.0f);
}

pwm::~pwm() {
  // disable GPIO, pullup/pulldown resistors should be installed
  // to switch off transistors when pin is high impedance
  gpio_deinit(m_magnitude_pin);
}

void __not_in_flash_func(pwm::output_sample)(uint16_t magnitude, uint8_t pwm_min, uint8_t pwm_max, uint8_t pwm_threshold) {
    // Scale from 16-bit magnitude (0-65535) to PWM range (pwm_min to pwm_max)
    uint32_t scaled_magnitude = ((uint32_t)magnitude * (pwm_max - pwm_min)) / 65535;
    
    // Apply threshold gating with hang time for CW operation
    static uint8_t hang_time = 0;
    if (scaled_magnitude > pwm_threshold) {
        hang_time = 255;  // Reset hang timer when signal exceeds threshold
    } else if (hang_time > 0) {
        hang_time--;  // Decrement hang timer
    }
    
    uint8_t pwm_output;
    if (hang_time > 0) {
        // Signal is active - add minimum bias and apply scaling
        pwm_output = (uint8_t)(scaled_magnitude + pwm_min);
        
        // Ensure we don't exceed maximum PWM value
        if (pwm_output > pwm_max) {
            pwm_output = pwm_max;
        }
    } else {
        // Signal below threshold and hang time expired - turn off
        pwm_output = 0;
    }
    
    // Output the PWM level to control PA envelope
    pwm_set_chan_level(m_pwm_slice, m_pwm_channel, pwm_output);
}