double Vfwd;   // Forward voltage
double Vrev;   // Reverse voltage
double Pfwd;   // Forward power
double Prev;   // Reverse power
double SWR;    // VSWR
double Gamma;  // Reflection coefficient
double Vbat;   // Battery voltage
const double conversion_factor = 3.3f / 1024; // Default resolution is 10bit


double CalculatePfwd();
double CalculateSWR();
void setup_vswr_meter();