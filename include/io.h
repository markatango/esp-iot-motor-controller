#ifndef IOH
#define IOH

#define UPI_LED 2
#define DNI_LED 4
#define UPS_LED 5
#define DNS_LED 18
#define UPLIM_LED 19
#define DNLIM_LED 21

#define A 18
#define B 19
#define C 25
#define D 33
#define WIFI_LED 2

#define UPLIM_SW 13
#define DNLIM_SW 12
#define UPS_SW 14
#define DNS_SW 27
#define UPI_SW 22
#define DNI_SW 23

#define ADC1_CH6 34
#define ADC1_CH7 35

#define NUM_DIGITAL_INPUTS 6
#define NUM_DIGITAL_OUTPUTS 5

extern int swState[6]; 
extern int newSwState[6]; 
extern int oldSwState[6]; 

void readSwitches();
void setLEDsTo(int state);
void setupIO();
bool hasSwChanged();
void mapSwToLed();
void setLEDTo(int ledPin, int state);  
void printSwStates();

#endif