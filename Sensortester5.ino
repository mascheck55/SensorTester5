#include <Arduino.h>
#include "LC_Sensor.h"
#define Repeats 5
#define HOLD 150

LC_Sensor mysensor = LC_Sensor();

void setup()
{
  // put your setup code here, to run once:

  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  mysensor.begin(0, 0, 0, 0,7);
  delay(300);
  Serial.println(mysensor.Debug());
  int Zero = mysensor.zero(7); // Messung eines funktionierenden Sensors abrufen
  mysensor.end();
  mysensor.begin(0, 10, 3, Zero,0); // mit neuem Messwert starten
  Serial.println(Zero);
  Serial.println(mysensor.reCalibrate(7));
}

void loop()
{
  // put your main code here, to run repeatedly:
  // i from 0 to 7
  uint8_t i = 7;
  digitalWrite(LED_BUILTIN, mysensor.activ(i));
  
}
