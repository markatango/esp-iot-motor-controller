#include <Arduino.h>

// void setup() { 
//   Serial.begin(9600); 
// }

// the loop function runs over and over again forever

void led(int state) {
    Serial.flush();
    pinMode(2,OUTPUT);
    digitalWrite(2, state);
    // delay(500);
    // digitalWrite(2,LOW);
    // delay(500);
    Serial.println("2");
}