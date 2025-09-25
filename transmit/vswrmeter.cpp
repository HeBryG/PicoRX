#include "pico/stdlib.h"
#include "hardware/adc.h"
#include <stdio.h>
#include "pins.h"
#include "vswrmeter.h"
#include "math.h"


// CALIBRATION FACTORS
// The tolerance on the Atmega328P's internal voltage reference is 1.0 to 1.2 Volts.
// The actual voltage should be measured on pin 21 and entered below.

// The response of the 1N5711 diodes follows a curve represented by the equation:
// Pfwd = aVfwd^2 + bVfwd (and the same for Prev and Vrev)
// where a and b are constants determined by plotting Power In vs Vfwd and performing a curve fit.
// An excellent tool for this can be found at https://veusz.github.io/
// The values of a and b below can be adjusted if necessary to improve accuracy (try adjusting b first).
// Dhiru - Use https://keisan.casio.com/exec/system/14059932254941 for quadratic regression analysis.

// const double IntRef = 1.1;
// const double IntRef = 3.3; // for RP2040
// const double a = 1.0;
// const double b = 0.75;

// https://keisan.casio.com/exec/system/14059932254941
const double a = 1.1378;
const double b = 1.2538;
const double c = -0.08555;


double CalculatePfwd()
{
  /* double Vadc0 = analogRead(A0); // Read ADC0 (pin 23)
    Vadc0 = constrain(Vadc0, 1, 1023); // Prevent divide-by-zero when calculating Gamma
    Vfwd = 2.8 * ((Vadc0 + 0.5) * IntRef / 1024); // Scaled up by R3 & R4
    Pfwd = a * sq(Vfwd) + b * Vfwd; */
  adc_select_input(3);
  double Vadc = adc_get_selected_input(); // Read ADC1
  Vadc = constrain(Vadc, 1, (1 << 10) - 1); // Prevent divide-by-zero when calculating Gamma
  Vfwd = 2.8 * ((Vadc + 0.5) * conversion_factor); // Scaled up by R3 & R4
  // Serial.println(Vadc);
  // Serial.println(Vfwd);
  Pfwd = a * sqrt(Vfwd) + b * Vfwd + c;
  if (Pfwd < 0) {
    Prev = 0;
  }
  printf("%d", Pfwd);

  return Pfwd;
}

double CalculateSWR()
{
  /* double Vadc1 = analogRead(A1); // Read ADC1 (pin 24)
    Vrev = 2.8 * ((Vadc1 + 0.5) * IntRef / 1024); // Scaled up by R7 & R8 */
  adc_select_input(2);
  double Vadc = adc_get_selected_input(); // Read ADC0
  Vrev = 2.8 * ((Vadc + 0.5) * conversion_factor); // Scaled up by R7 & R8 */
  Prev = a * sqrt(Vrev) + b * Vrev + c;
  if (Prev < 0.01) { // exclude residual values
    Prev = 0;
  }
  printf("%d", Prev);
  Gamma = sqrt(Prev / Pfwd); // Calculate reflection coefficient
  SWR = (1 + Gamma) / (1 - Gamma);
  SWR = constrain(SWR, 1, 99.9);
  printf("%d",SWR);
  return SWR;
}

void setup_vswr_meter()
{
    adc_init();

    // TODO: change Name of ADCs input,
    // PIN_MIC and PIN_BATTERY should only be available for 
    // rp2350b core module
    adc_gpio_init(PIN_MIC);
    adc_gpio_init(PIN_BATTERY);
    adc_set_clkdiv(0);
}


