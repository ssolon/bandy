#ifndef LOAD_CELL_H_
#define LOAD_CELL_H_

#include <Arduino.h>
#include <HX711_ADC.h>

class LoadCell {
  public:
    uint8_t clockPin;
    uint8_t dataPin;

    virtual float* getData() = 0;
    virtual void tare() = 0;
    virtual bool timeout() = 0;

    virtual void powerDown();
    virtual void powerUp();
};

class LoadCellHX711ADC : public LoadCell {
  HX711_ADC* pLoadCell;
  float returnValue;

  public:

  // Return current (next) value or null if no next reading available.
  virtual float* getData();

  virtual void tare() {
    Serial.println("Tare called");
    pLoadCell->tare();
  }

  virtual bool timeout() {
    return pLoadCell->getSignalTimeoutFlag();
  }

  virtual void powerDown() {
    pLoadCell->powerDown();
  }

  virtual void powerUp() {
    pLoadCell->powerUp();
  }
  LoadCellHX711ADC(uint8_t clockPin, uint8_t dataPin);
};

#endif