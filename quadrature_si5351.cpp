//  _  ___  _   _____ _     _                 
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___ 
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/    
//
// Copyright (c) Jonathan P Dawson 2024
// filename: quadrature_si5351.cpp
// description:
//
// Quadrature Oscillator using si5351
//
// Generates a quadrature oscillator using si5351.
// Works over ~50kHz to ~30MHz
//
// For high frequencies (above 5MHz) uses the "Hans Summers" phase offset technique
// See:
// https://github.com/etherkit/Si5351Arduino?tab=readme-ov-file#phase
//
// For low frequencies (below 5MHz) uses the "Uebo" delay technique
// See:
// https://tj-lab.org/2020/08/27/si5351%e5%8d%98%e4%bd%93%e3%81%a73mhz%e4%bb%a5%e4%b8%8b%e3%81%ae%e7%9b%b4%e4%ba%a4%e4%bf%a1%e5%8f%b7%e3%82%92%e5%87%ba%e5%8a%9b%e3%81%99%e3%82%8b/
//
// License: MIT
//

#include <cmath>
#include <cstdio>
#include "quadrature_si5351.h"
#include "pico/stdlib.h"

bool quad_si5351 :: initialise(i2c_inst_t *i2c, uint8_t sda_pin, uint8_t scl_pin, uint8_t address, uint32_t crystal_frequency_Hz)
{
    i2c_init(i2c, 400 * 1000);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);
    m_i2c = i2c;
    m_address = address; //0x60 or 0x61
    m_crystal_frequency_Hz = crystal_frequency_Hz;

    stop();

    const uint8_t reset_sequence[] = {
      16,
      static_cast<uint8_t>(0x80),
      static_cast<uint8_t>(0x80),
      static_cast<uint8_t>(0x80),
      static_cast<uint8_t>(0x80),
      static_cast<uint8_t>(0x80),
      static_cast<uint8_t>(0x80),
      static_cast<uint8_t>(0x80),
      static_cast<uint8_t>(0x80)
    };
    int ret = i2c_write_blocking(m_i2c, m_address, reset_sequence, 9, false);  
    return ret != PICO_ERROR_GENERIC;
  init_clk2();

}

void quad_si5351 :: start()
{
  //high impedance when disabled
  write_reg(SI_CLK_DIS_STATE, 0xAA);
  write_reg(SI_OUPUT_ENABLE, 0xFC);
}

void quad_si5351 :: stop()
{
  //high impedance when disabled
  write_reg(SI_CLK_DIS_STATE, 0xAA);
  write_reg(SI_OUPUT_ENABLE, 0xFF);
}

void quad_si5351 :: write_reg(uint8_t address, uint8_t data)
{
  const uint8_t command[] = {address, data};
	i2c_write_blocking(m_i2c, m_address, command, 2, false);
}

void quad_si5351 :: crystal_load(uint8_t load)
{
  write_reg(183, (load<<6) | 0x12);
}

void quad_si5351 :: configure_pll(uint8_t pll, uint8_t a, uint32_t b, uint32_t c)
{

  //https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN619.pdf
  //section 3.2

	const uint32_t P1 = (128 * a) + ((128*b)/c) - 512;
	const uint32_t P2 = (128 * b) - (c*(128*b/c));
	const uint32_t P3 = c;
 
  write_reg(pll+0, (P3 & 0x0000FF00) >> 8);
  write_reg(pll+1, P3 & 0x000000FF);
  write_reg(pll+2, (P1 & 0x00030000) >> 16);
  write_reg(pll+3, (P1 & 0x0000FF00) >> 8);
  write_reg(pll+4, P1 & 0x000000FF);
  write_reg(pll+5, ((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16));
  write_reg(pll+6, (P2 & 0x0000FF00) >> 8);
  write_reg(pll+7, P2 & 0x000000FF);

}

void quad_si5351 :: configure_phase_offset(uint8_t clk, uint8_t phase_offset)
{
  write_reg(clk+165, phase_offset & 0x7f);
}

void quad_si5351 :: configure_multisynth(uint8_t synth, uint32_t divider, uint8_t rDiv)
{
	const uint32_t P1 = 128 * divider - 512;
	const uint32_t P2 = 0;
	const uint32_t P3 = 1;

	write_reg(synth+0,(P3 & 0x0000FF00) >> 8);
	write_reg(synth+1,(P3 & 0x000000FF));
	write_reg(synth+2,((P1 & 0x00030000) >> 16) | rDiv);
	write_reg(synth+3,(P1 & 0x0000FF00) >> 8);
	write_reg(synth+4,(P1 & 0x000000FF));
	write_reg(synth+5,((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16));
	write_reg(synth+6,(P2 & 0x0000FF00) >> 8);
	write_reg(synth+7,(P2 & 0x000000FF));
}

void quad_si5351 :: adjust_phase(uint8_t synth, uint32_t a, uint32_t b, uint32_t c, uint8_t rDiv, uint32_t delay_us)
{
	const uint32_t P1 = 128 * a - 512;
	const uint32_t P2 = 0;
	const uint32_t P3 = 1;

  //https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN619.pdf
  //section 4.1.2
	const uint32_t P1_offset = (128 * a) + ((128*b)/c) - 512;
	const uint32_t P2_offset = (128 * b) - (c*(128*b/c));
	const uint32_t P3_offset = c;
  
  write_reg(synth+0,(P3_offset & 0x0000FF00) >> 8);
	write_reg(synth+1,(P3_offset & 0x000000FF));
	write_reg(synth+2,((P1_offset & 0x00030000) >> 16) | rDiv);
	write_reg(synth+3,(P1_offset & 0x0000FF00) >> 8);
	write_reg(synth+4,(P1_offset & 0x000000FF));
	write_reg(synth+5,((P3_offset & 0x000F0000) >> 12) | ((P2_offset & 0x000F0000) >> 16));
	write_reg(synth+6,(P2_offset & 0x0000FF00) >> 8);
	write_reg(synth+7,(P2_offset & 0x000000FF));

  sleep_us(delay_us);

	write_reg(synth+0,(P3 & 0x0000FF00) >> 8);
	write_reg(synth+1,(P3 & 0x000000FF));
	write_reg(synth+2,((P1 & 0x00030000) >> 16) | rDiv);
	write_reg(synth+3,(P1 & 0x0000FF00) >> 8);
	write_reg(synth+4,(P1 & 0x000000FF));
	write_reg(synth+5,((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16));
	write_reg(synth+6,(P2 & 0x0000FF00) >> 8);
	write_reg(synth+7,(P2 & 0x000000FF));
}

// 0 = 2mA, 1 = 4mA, 2 = 6mA, 3 = 8mA
void quad_si5351 :: set_drive(uint8_t drive_strength)
{
  m_drive_strength = drive_strength & 3;
}

double quad_si5351 :: set_frequency_hz(uint32_t frequency)
{

  //uint8_t status = 0;
	//i2c_write_blocking(m_i2c, m_address, &status, 1, true);
  //i2c_read_blocking(m_i2c, m_address, &status, 1, false);

  if(frequency > 5000000)
  {
    return set_frequency_hz_high(frequency);
  } 
  else 
  {
    return set_frequency_hz_low(frequency);
  }
}

//for frequencies above 5MHz, quadrature clocks can be generated using the phase register
double quad_si5351 :: set_frequency_hz_high(uint32_t frequency_Hz)
{

  if(m_mode == low_mode)
  {
    m_pll_needs_reset = true;
    m_mode = high_mode;
  }

  //set the PLL to an even multiple of required frequency
	uint32_t divider = 2*((600000000+(2*frequency_Hz))/(2*frequency_Hz));
	const uint32_t pll_frequency = divider * frequency_Hz;
  if(divider != m_divider) m_pll_needs_reset = true;


	const uint32_t multiplier_integer_part = pll_frequency / m_crystal_frequency_Hz;
	const uint64_t multiplier_fractional_part = pll_frequency % m_crystal_frequency_Hz;
  const uint32_t multiplier_denominator = 1048575u;
  const uint64_t multiplier_numerator = (multiplier_fractional_part * multiplier_denominator) / m_crystal_frequency_Hz;
  const double exact_pll_frequency = m_crystal_frequency_Hz * (multiplier_integer_part + ((double)multiplier_numerator/multiplier_denominator));
  const double exact_frequency = exact_pll_frequency/divider;

	// Set up PLL A with the calculated multiplication ratio
	configure_pll(SI_SYNTH_PLL_A, multiplier_integer_part, multiplier_numerator, multiplier_denominator);
  configure_multisynth(SI_SYNTH_MS_0, divider, SI_R_DIV_1);
  configure_multisynth(SI_SYNTH_MS_1, divider, SI_R_DIV_1);
  configure_phase_offset(0, 0);
  configure_phase_offset(1, divider);

  if(m_pll_needs_reset)
  {
    write_reg(SI_PLL_RESET, 0xA0);
    m_pll_needs_reset = false;
  }

  write_reg(SI_CLK0_CONTROL, 0x4C | SI_CLK_SRC_PLL_A | m_drive_strength);
  write_reg(SI_CLK1_CONTROL, 0x4C | SI_CLK_SRC_PLL_A | m_drive_strength);

  m_frequency = frequency_Hz;
  m_divider = divider;

  return exact_frequency;
}

//for frequencies below 5MHz, quadrature clocks can be generated using "manual" phase adjustment
double quad_si5351 :: set_frequency_hz_low(uint32_t frequency_Hz)
{

  if(m_mode == high_mode)
  {
    m_pll_needs_reset = true;
    m_mode = low_mode;
  }

  //set the PLL to an even multiple of required frequency
	uint32_t divider = 2*((600000000+(2*frequency_Hz))/(2*frequency_Hz));
  uint32_t rdiv = 1;
	const uint32_t pll_frequency = divider * frequency_Hz;

  if(divider > 2048) {
    rdiv = 32;
    divider /= 32;
  }

  if(divider != m_divider) m_pll_needs_reset = true;
  if(rdiv != m_rdiv) m_pll_needs_reset = true;
  
	const uint32_t multiplier_integer_part = pll_frequency / m_crystal_frequency_Hz;
	const uint64_t multiplier_fractional_part = pll_frequency % m_crystal_frequency_Hz;
  const uint32_t multiplier_denominator = 1048575u;
  const uint64_t multiplier_numerator = (multiplier_fractional_part * multiplier_denominator) / m_crystal_frequency_Hz;
  const double exact_pll_frequency = m_crystal_frequency_Hz * (multiplier_integer_part + ((double)multiplier_numerator/multiplier_denominator));
  const double exact_frequency = exact_pll_frequency/(divider*rdiv);

	// Set up PLL A with the calculated multiplication ratio
	configure_pll(SI_SYNTH_PLL_A, multiplier_integer_part, multiplier_numerator, multiplier_denominator);


  if(m_pll_needs_reset)
  {
    configure_multisynth(SI_SYNTH_MS_0, divider, rdiv==32?SI_R_DIV_32:SI_R_DIV_1);
    configure_multisynth(SI_SYNTH_MS_1, divider, rdiv==32?SI_R_DIV_32:SI_R_DIV_1);
    configure_phase_offset(0, 0);
    configure_phase_offset(1, 0);
    write_reg(SI_PLL_RESET, 0xA0);
    sleep_us(100000);

    //reduce 1 clock by 4Hz, after 62.5ms it should be 1/4 cycle behind
    const double adjusted_frequency = exact_frequency-4.0;
    const double adjusted_divider = exact_pll_frequency/(rdiv*adjusted_frequency);
    const uint32_t adjusted_divider_integer_part = divider;
    const uint32_t adjusted_divider_denominator = 1048575u;
    const uint32_t adjusted_divider_numerator = round((adjusted_divider - adjusted_divider_integer_part)*adjusted_divider_denominator);
    const double actual_adjusted_frequency = exact_pll_frequency/(rdiv*(adjusted_divider_integer_part+((double)adjusted_divider_numerator/adjusted_divider_denominator)));
    const double frequency_difference = exact_frequency-actual_adjusted_frequency;
    const uint32_t quarter_cycle_delay_us = 0.25e6/frequency_difference;

    adjust_phase(SI_SYNTH_MS_1, adjusted_divider_integer_part, adjusted_divider_numerator, adjusted_divider_denominator, rdiv==32?SI_R_DIV_32:SI_R_DIV_1, quarter_cycle_delay_us);
    m_pll_needs_reset = false;
  }

  write_reg(SI_CLK0_CONTROL, 0x4C | SI_CLK_SRC_PLL_A | m_drive_strength);
  write_reg(SI_CLK1_CONTROL, 0x4C | SI_CLK_SRC_PLL_A | m_drive_strength);

  m_frequency = frequency_Hz;
  m_divider = divider;
  m_rdiv = rdiv;

  return exact_frequency;
}


// CLK2 initialization - sets up independent clock output for CW/transmit
void quad_si5351::init_clk2()
{
    // Add small delay for I2C stability
    //sleep_us(1000);
    
    // Configure CLK2 control register with proper drive strength
    // 0x4C = MS2 as source, not inverted, powered up, integer mode
    write_reg(SI_CLK2_CONTROL, 0x4C | SI_CLK_SRC_PLL_B | m_drive_strength);
    
    //sleep_us(100);
    
    // Initialize CLK2 phase offset to 0
    configure_phase_offset(2, 0);
    
    //sleep_us(100);
    
    // Start with CLK2 disabled (will be enabled when needed)
    clk2_output_enable(false);
    m_clk2_enabled = false;
    printf("CLK2 initialized with drive strength %d\n", m_drive_strength);
}

// Enhanced write_reg with error checking (add this version or modify existing)
void quad_si5351::write_reg_safe(uint8_t address, uint8_t data)
{
    const uint8_t command[] = {address, data};
    int result = i2c_write_blocking(m_i2c, m_address, command, 2, false);
    
    if (result == PICO_ERROR_GENERIC || result == PICO_ERROR_TIMEOUT) {
        printf("I2C ERROR: Failed to write reg 0x%02X = 0x%02X (result=%d)\n", 
               address, data, result);
        
        // Attempt I2C bus recovery
        sleep_us(1000);  // 10ms delay
        
        // Try once more
        result = i2c_write_blocking(m_i2c, m_address, command, 2, false);
        printf("I2C RETRY: result=%d\n", result);
    }
}

// Set CLK2 frequency with enhanced error handling
double quad_si5351::set_clk2_frequency_hz(uint32_t frequency_Hz)
{
    // printf("Setting CLK2 frequency to %lu Hz\n", frequency_Hz);
    
    // Use PLL B for CLK2 to avoid interference with quadrature clocks on PLL A
    uint32_t divider = 2*((600000000+(2*frequency_Hz))/(2*frequency_Hz));
    const uint32_t pll_frequency = divider * frequency_Hz;
    
    // Calculate PLL B multiplier
    const uint32_t multiplier_integer_part = pll_frequency / m_crystal_frequency_Hz;
    const uint64_t multiplier_fractional_part = pll_frequency % m_crystal_frequency_Hz;
    const uint32_t multiplier_denominator = 1048575u;
    const uint64_t multiplier_numerator = (multiplier_fractional_part * multiplier_denominator) / m_crystal_frequency_Hz;
    const double exact_pll_frequency = m_crystal_frequency_Hz * (multiplier_integer_part + ((double)multiplier_numerator/multiplier_denominator));
    const double exact_frequency = exact_pll_frequency/divider;
    if (frequency_Hz == m_clk2_frequency) {
        return exact_frequency;
    }    
    printf("Setting CLK2 frequency to %lu Hz\n", frequency_Hz);  // Move here
    printf("PLL B: mult=%lu.%llu/%lu, div=%lu\n", 
          multiplier_integer_part, multiplier_numerator, multiplier_denominator, divider);
    
    // Configure PLL B for CLK2 with error checking
    configure_pll(SI_SYNTH_PLL_B, multiplier_integer_part, multiplier_numerator, multiplier_denominator);
    //sleep_us(100);
    
    // Configure multisynth 2 for CLK2
    configure_multisynth(SI_SYNTH_MS_2, divider, SI_R_DIV_1);
    //sleep_us(100);
    
    // Reset PLL B only - avoid affecting PLL A
    write_reg(SI_PLL_RESET, 0x80);  // Reset PLL B only
    //sleep_us(500);  // Wait for PLL to stabilize
    
    // Update CLK2 control register
    write_reg(SI_CLK2_CONTROL, 0x4C | SI_CLK_SRC_PLL_B | m_drive_strength);
    //sleep_us(100);
    
    m_clk2_frequency = frequency_Hz;
    
    printf("CLK2 configured: %.1f Hz (requested %lu Hz)\n", exact_frequency, frequency_Hz);
    return exact_frequency;
}


//  keep quadrature clocks always enabled, only control CLK2
void quad_si5351::clk2_output_enable(bool enable)
{
    if (enable && !m_clk2_enabled) {
        // Enable CLK2 while keeping CLK0,CLK1 enabled for RX
        write_reg(SI_OUPUT_ENABLE, 0xF8);  // CLK0,CLK1,CLK2 enabled (bits 0,1,2 = 0)
        m_clk2_enabled = true;
        printf("CLK2 output ENABLED\n");
    } else if (!enable && m_clk2_enabled) {
        // Disable only CLK2, keep CLK0,CLK1 enabled for RX
        write_reg(SI_OUPUT_ENABLE, 0xFC);  // CLK0,CLK1 enabled, CLK2 disabled (bit 2 = 1)
        m_clk2_enabled = false;
        printf("CLK2 output DISABLED\n");
    }
    
    // Add delay for I2C bus recovery
    //sleep_us(1000);
}

// Set CLK2 phase offset (0-127, where 127 = 180 degrees)
void quad_si5351::set_clk2_phase(uint8_t phase_offset)
{
    // Phase offset only works with integer dividers
    // Each step = 360° / (4 * divider)
    // For most applications, phase_offset of 0-127 covers 0-180 degrees
    configure_phase_offset(2, phase_offset & 0x7F);
    
    // Phase change requires PLL reset to take effect
    write_reg(SI_PLL_RESET, 0x80);  // Reset PLL B only
    
    // printf("CLK2 phase set to %d (%.1f degrees)\n", 
    //        phase_offset, (phase_offset * 180.0) / 127.0);
}