#include <Arduino.h>

void led(int state) {
    pinMode(2,OUTPUT);
    digitalWrite(2, state);
}