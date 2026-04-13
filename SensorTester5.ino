#include <Arduino.h>

#include "LC_Sensor_Wire.h" //defines if sensor uses I2C
#include "LC_Sensor.h"

LC_Sensor mysensor = LC_Sensor(); // Instance of a sensor

#ifdef LC_SENSOR_USE_WIRE
// ============================================================================
// WIRE CONFIGURATION A4(SDA) A5(SCL) should work like a read only PCF8574
// ============================================================================
#define I2C_ADDRESS 0x65
#include <Wire.h> 
void requestEvent() {Wire.write(mysensor.virtPort());}
void receiveEvent(int howMany){
  while (Wire.available())
    Wire.read(); // drop everything what is comming
}
#endif
// ============================================================================

void setup()
{
  // put your setup code here, to run once:

#ifdef LC_SENSOR_USE_WIRE
  Wire.begin(I2C_ADDRESS);
  Wire.onRequest(requestEvent);
  Wire.onReceive(receiveEvent);
#endif
  Serial.begin(115200);
  mysensor.begin(0, 100, 0, 0, 0); // works on A0
  delay(300);
  Serial.print("is Sensor running? ");
  if(mysensor.isRunning())
  Serial.println("yes");else Serial.println("no");
}

void loop()
{
  // put your main code here, to run repeatedly:
   uint8_t v = mysensor.virtPort();
  if (v != 0xFF) // Logic is like PCF8574 invers
  {
    Serial.print("0x");
    Serial.print(v, HEX);
    Serial.print("  ");

    for (int i = 7; i >= 0; i--)
    {
      Serial.print(bitRead(v, i));
    }

    Serial.println();
    delay(600);
  }
}
