#include <HX711_ADC.h>
#if defined(ESP8266)|| defined(ESP32) || defined(AVR)
#include <EEPROM.h>
#endif
#include "LoadCell.h"

#if defined(ESP8266)|| defined(ESP32) || defined(AVR)
#include <EEPROM.h>
#endif

LoadCellHX711ADC::LoadCellHX711ADC(uint8_t dout, uint8_t sck) {
  dataPin = dout;
  clockPin = sck;

  loadCell = new HX711_ADC(dataPin, clockPin);

  // Setup copied from hx711-adc example code

  float calibrationValue; // calibration value
  calibrationValue = 26983.9; //43782; // 696.0; // uncomment this if you want to set this value in the sketch
#if defined(ESP8266) || defined(ESP32)
  //EEPROM.begin(512); // uncomment this if you use ESP8266 and want to fetch this value from eeprom
#endif
  //EEPROM.get(calVal_eepromAdress, calibrationValue); // uncomment this if you want to fetch this value from eeprom

  loadCell->begin();
  //loadCell->setReverseOutput();
  unsigned long stabilizingtime = 2000; // tare preciscion can be improved by adding a few seconds of stabilizing time
  boolean _tare = true; //set this to false if you don't want tare to be performed in the next step
  loadCell->start(stabilizingtime, _tare);
  if (loadCell->getTareTimeoutFlag()) {
    Serial.println("Timeout, check MCU>HX711 wiring and pin designations");
  }
  else {
    loadCell->setCalFactor(calibrationValue); // set calibration factor (float)
    Serial.println("Startup is complete");
  }
  while (!loadCell->update());
  Serial.print("Calibration value: ");
  Serial.println(loadCell->getCalFactor());
  Serial.print("HX711 measured conversion time ms: ");
  Serial.println(loadCell->getConversionTime());
  Serial.print("HX711 measured sampling rate HZ: ");
  Serial.println(loadCell->getSPS());
  Serial.print("HX711 measured settlingtime ms: ");
  Serial.println(loadCell->getSettlingTime());
  Serial.println("Note that the settling time may increase significantly if you use delay() in your sketch!");
  if (loadCell->getSPS() < 7) {
    Serial.println("!!Sampling rate is lower than specification, check MCU>HX711 wiring and pin designations");
  }
  else if (loadCell->getSPS() > 100) {
    Serial.println("!!Sampling rate is higher than specification, check MCU>HX711 wiring and pin designations");
  }  
}; 

float* LoadCellHX711ADC::getData() {
  if (loadCell->update()) {
    returnValue = loadCell->getData();
    return &returnValue;
  } 

  return nullptr;
}