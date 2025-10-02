#include "io.h"
#include <map>
#include <vector>
#include <Arduino.h>

using namespace std;

std::map<int,int> swLedmap = {
    {UPI_SW, UPI_LED},
    {DNI_SW, DNI_LED},
    {UPS_SW, UPS_LED},
    {DNS_SW, DNS_LED},
    {UPLIM_SW, UPLIM_LED},
    {DNLIM_SW, DNLIM_LED}
};

int swState[] = {0,0,0,0,0,0};
int newSwState[] = {0,0,0,0,0,0};
int oldSwState[] = {0,0,0,0,0,0};

void setupIO(){ 
    pinMode(UPI_LED ,OUTPUT);
    pinMode(DNI_LED ,OUTPUT);
    pinMode(UPS_LED ,OUTPUT);
    pinMode(DNS_LED ,OUTPUT);
    pinMode(UPLIM_LED ,OUTPUT);
    pinMode(DNLIM_LED ,OUTPUT);

    pinMode(MOT_UP ,OUTPUT);
    pinMode(MOT_DN ,OUTPUT);

    pinMode(UPLIM_SW ,INPUT);
    pinMode(DNLIM_SW ,INPUT);
    pinMode(UPS_SW ,INPUT);
    pinMode(DNS_SW ,INPUT);
    pinMode(UPI_SW ,INPUT);
    pinMode(DNI_SW ,INPUT);
};

void setLEDsTo(int state){
  digitalWrite(UPI_LED ,state);
  digitalWrite(DNI_LED ,state);
  digitalWrite(UPS_LED ,state);
  digitalWrite(DNS_LED ,state);
  digitalWrite(UPLIM_LED ,state);
  digitalWrite(DNLIM_LED ,state);
};

void setLEDTo(int ledPin, int state){
    digitalWrite(ledPin ,state);
};

void mapSwToLed(){
  for(std::map<int,int>::iterator it = swLedmap.begin(); it != swLedmap.end(); ++it) {
    int swState = digitalRead(it->first);
    digitalWrite(it->second, swState);
    };
};

void readSwitches(int* newSwState){
    int idx = 0;
    // Read all switches
    for(std::map<int,int>::iterator it = swLedmap.begin(); it != swLedmap.end(); ++it) {
        int swState = digitalRead(it->second);
        newSwState[idx] = swState;
        idx++;  
    };
};

bool hasSwChanged(int* oldSwState, int* newSwState){
    bool res = false;
    for(int i=0; i<6; i++){
        if(oldSwState[i] != newSwState[i]){
            res = true;
            oldSwState[i] = newSwState[i];
        }
    }
    return res;
}       


