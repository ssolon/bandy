/*
    Video: https://www.youtube.com/watch?v=oCMOYS71NIU
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
    Ported to Arduino ESP32 by Evandro Copercini
    updated by chegewara
   Create a BLE server that, once we receive a connection, will send periodic notifications.
   The service advertises itself as: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
   And has a characteristic of: beb5483e-36e1-4688-b7f5-ea07361b26a8
   The design of creating the BLE server is:
   1. Create a BLE Server
   2. Create a BLE Service
   3. Create a BLE Characteristic on the Service
   4. Create a BLE Descriptor on the characteristic
   5. Start the service.
   6. Start advertising.
   A connect hander associated with the server starts a background task that performs notification
   every couple of seconds.
*/
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLE2904.h>
#include <HX711_ADC.h>
#include "OneButton.h"

#include "LoadCell.h"

BLEServer* pServer = NULL;
BLECharacteristic* pBatteryLevelCharacteristic = NULL;
BLECharacteristic* pResistanceCharacteristic = NULL;
BLECharacteristic* pButton1Characteristic = NULL;

bool deviceConnected = false;
bool oldDeviceConnected = false;

// Dummy value for testing characteristic
uint8_t batteryLevel = 80;

// Timeout value for sleep
const uint16_t sleepTimeoutMillis = 10 * 1000;

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "(uint16_t)0x1826" //"9b3116d8-f7e0-11ec-b939-0242ac120002"
#define CHARACTERISTIC_UUID "a6351a0c-f7e0-11ec-b939-0242ac120002"

const  uint16_t GATT_UNIT_MASS_KILOGRAM = (uint16_t) 0x2702;

const BLEUUID batteryServiceUUID = BLEUUID((uint16_t) 0x180f);
const BLEUUID batteryLevelUUID = BLEUUID((uint16_t) 0x2a19);

const BLEUUID fitnessServiceUUID = BLEUUID((uint16_t)0x1826);
const BLEUUID resistanceCharacteristicUUID = BLEUUID("a6351a0c-f7e0-11ec-b939-0242ac120002");
const BLEUUID button1CharacteristicUUID = BLEUUID("5b0d3458-d6f5-4ac4-936b-18654e9db83b");

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      Serial.println("client disconnected");
      deviceConnected = false;
    }
};

//pins:
const int threshold = 30;

const int LED_YELLOW = 17;
const int LED_GREEN = 16;

const int HX711_dout = 26; //mcu > HX711 dout pin
const int HX711_sck = 25; //mcu > HX711 sck pin

// I2C Pins
const int BANDY_SDA = 22;
const int BANDY_SDL = 23;

const int MPU6050_INT = 32; // Interrupt pin

const gpio_num_t BUTTON_1 = GPIO_NUM_33;

const gpio_num_t WAKEUP_BUTTON = BUTTON_1;

const int TILT = MPU6050_INT;

const int LED_WAITING = LED_YELLOW;
const int LED_CONNECTED = LED_GREEN;

#ifdef HAS_BUZZER
const int BUZZER = 13;
#endif

LoadCell* pLoadCell;
OneButton button1(BUTTON_1);

// Tilt stuff
int lastTiltRead=0;
long lastTiltTime=0;
const int tiltDebounce = 100;

// Type for values
typedef int16_t value_t;

// Values are scaled
const uint8_t valueScale = 10;
const value_t EPSILON = std::round(0.1f * valueScale);

// Last value read (but maybe not sent?)
value_t lastValue = 0;
value_t notifyValue = 0;

// Types for buttons
typedef uint8_t boolean_t;
typedef boolean_t buttonValue_t;

boolean_t button1Value = true;

//#define LOG_RAW_VALUES

#ifdef LOG_RAW_VALUES
float rawLastValue = 0.0;
#endif

// Explicitly request a notification
bool notify = false;

// Millis last time we did something
unsigned long lastMillis;

// Millis at last value handled (but maybe not sent?)
unsigned long lastValueMillis;
const unsigned long minIntervalToSend = 0;

// We only care about limited precision in our readings so fix so all match
value_t fixValue(float value) {
  return std::round(value * valueScale);
}

// Float representation of value_t
float floatOf(value_t value) {
  return float(value)/valueScale;
}

// Ignore small differences
bool isChanged(value_t v1, value_t v2) {
  value_t delta = std::abs(v1-v2);
  return (delta > EPSILON) || delta > 0 && v2 == 0;
}
  
void setState() {
  digitalWrite(LED_WAITING, deviceConnected ? LOW : HIGH);
  digitalWrite(LED_CONNECTED, deviceConnected ? HIGH : LOW);
#ifdef HAS_BUZZER
  tone(BUZZER, deviceConnected ? 440 : 150, 250);
#endif
}

void flicker(int pin) {
  ledcAttachPin(pin, 0);
  int channel = 0;
  int freq = 1;
  int resolution = 12;
  int result=ledcSetup(channel, freq, resolution);
  ledcWrite(0, 128);
}

void doTare() {
  pLoadCell->tare();
}

void setupDeepSleep() {
  pLoadCell->powerDown();
#ifdef HAS_BUZZER
  tone(BUZZER, 75, 250);
#endif
  delay(250);
}

void startDeepSleep() {
  BLEDevice::deinit();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER); // Cause it keeps waking us up 
  esp_sleep_enable_ext0_wakeup(WAKEUP_BUTTON, 0);
  esp_deep_sleep_start();
}

void button1Click() {
  if (deviceConnected) {
    Serial.println("Notify button1Click");
    pButton1Characteristic->setValue((uint8_t*)&button1Value, sizeof(button1Value));
    pButton1Characteristic->notify();
  }
}
void button1DoubleClick() {
  doTare();
}

void button1LongPressStart() {
  setupDeepSleep();
}

void button1LongPressEnd() {
  startDeepSleep();
}

int testLastChangeMillis = 0;
int testLastRead=0;;

bool tilt() {
  int newTiltRead = digitalRead(TILT);

//#define TEST_TILT 
#ifdef TEST_TILT
  if (newTiltRead != testLastRead) {
    Serial.print("delta=");
    Serial.print(millis() - testLastChangeMillis);
    Serial.print(" ");
    Serial.print(testLastRead);
    Serial.print(" -> ");
    Serial.println(newTiltRead);
    testLastChangeMillis = millis();
    testLastRead = newTiltRead;
  }
#endif

  bool tilt = false;

  if (lastTiltRead != newTiltRead && (millis() - lastTiltTime) > tiltDebounce) {
      lastTiltRead = newTiltRead;
      lastTiltTime = millis();
      tilt = true;
  }

  return tilt;
}

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason){
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.print("Wakeup was not caused by deep sleep: "); Serial.println(wakeup_reason); break;
  }
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);

  print_wakeup_reason();

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_WAITING, OUTPUT);
  pinMode(LED_CONNECTED, OUTPUT);

  pinMode(TILT, INPUT_PULLUP);
  
  // Setup load cell
  pLoadCell = new LoadCellHX711ADC(HX711_dout, HX711_sck);
  
  // Seems to clear up problems when waking up from sleep and shoudn't hurt otherwise
  // pLoadCell->powerDown();
  // pLoadCell->powerUp();

  // Setup tare request
  pinMode(BUTTON_1, INPUT_PULLUP);
  button1.attachClick(button1Click);
  button1.attachDoubleClick(button1DoubleClick);
  button1.attachLongPressStart(button1LongPressStart);
  button1.attachLongPressStop(button1LongPressEnd);
  
  // Create the BLE Device
  BLEDevice::init("Bandy");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pBatteryService = pServer->createService(batteryServiceUUID);
  pBatteryLevelCharacteristic = pBatteryService->createCharacteristic(
      batteryLevelUUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_NOTIFY
  );
  pBatteryLevelCharacteristic->addDescriptor(new BLE2902());

  // Static initialization for now
  pBatteryLevelCharacteristic->setValue((uint8_t*)&batteryLevel, 1);

  // Create the fitness Service
  BLEService *pService = pServer->createService(fitnessServiceUUID);

  // Create Resistance Characteristic
  
  pResistanceCharacteristic = pService->createCharacteristic(
                      resistanceCharacteristicUUID,
                      BLECharacteristic::PROPERTY_READ   |
                      // BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY //|
                      // BLECharacteristic::PROPERTY_INDICATE
                    );
  
  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pResistanceCharacteristic->addDescriptor(new BLE2902());

  BLE2904* pResistanceDescriptor = new BLE2904();
  pResistanceDescriptor->setFormat(BLE2904::FORMAT_SINT16);
  pResistanceDescriptor->setUnit(GATT_UNIT_MASS_KILOGRAM);
  pResistanceDescriptor->setExponent(-valueScale);
  pResistanceCharacteristic->addDescriptor(pResistanceDescriptor);

  BLEDescriptor* pResistanceDescriptionDescriptor = new BLEDescriptor(BLEUUID((uint16_t)0x2901), 8);
  pResistanceDescriptionDescriptor->setValue("Resist");
  pResistanceCharacteristic->addDescriptor(pResistanceDescriptionDescriptor);

  // Create Button1 characteristic. Will notify on click

  pButton1Characteristic = pService->createCharacteristic(
    button1CharacteristicUUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  pButton1Characteristic->addDescriptor(new BLE2902());

  BLE2904* pButton1Descriptor = new BLE2904();
  pButton1Descriptor->setFormat(BLE2904::FORMAT_UINT8);
  pButton1Characteristic->addDescriptor(pButton1Descriptor);

  BLEDescriptor* pButton1DescriptionDescriptor = new BLEDescriptor(BLEUUID((uint16_t)0x2901), 8);
  pButton1DescriptionDescriptor->setValue("Button1");
  pButton1Characteristic->addDescriptor(pButton1DescriptionDescriptor);

  // Start the service(s)
  pService->start();
  pBatteryService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(fitnessServiceUUID);
  pAdvertising->addServiceUUID(batteryServiceUUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();

  Serial.println("Waiting a client connection to notify...");
  setState();
  lastMillis = millis();
  
}

void loop() {

  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
    setState();
    delay(500); // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("Restart advertising");
    oldDeviceConnected = deviceConnected;
    notifyValue = 0;
  }
 
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
    // do stuff here on connecting
    Serial.println("Client connected");
    oldDeviceConnected = deviceConnected;
    setState();
    notifyValue = 0;
    notify = true;
  }
  //!!!! Test out touch
  //digitalWrite(LED_BUILTIN, touchRead(T0) < threshold ? HIGH : LOW);

  // Get a scale reading if ready

  float* nextValue = pLoadCell->getData();
  if (nextValue) {
    if (pLoadCell->timeout()) {
      // TODO Report errors with an error characteristic?
      Serial.println("!!!! LOADCELL TIMEOUT !!!!\n");
    }

    value_t fixedNextValue = fixValue(*nextValue);
#ifdef LOG_RAW_VALUES
    //!!!! For testing let's see how the values "vary"
    if (*nextValue != rawLastValue) {
      Serial.print(millis());
      Serial.print("\t");
      Serial.println(rawLastValue);
      rawLastValue = *nextValue;
    }
    //!!!! end testing
#endif

    if (isChanged(notifyValue, fixedNextValue) || notify) {
      // Serial.printf("notifyValue=%f fixedNextValue=%f\n", floatOf(notifyValue), floatOf(fixedNextValue));

      notify = false;
      unsigned long sinceLastSend = millis() - lastValueMillis;
      lastValue = fixedNextValue;

      // notify changed value

      if (deviceConnected && sinceLastSend > minIntervalToSend ) {
#ifdef LOG_NOTIFY
        Serial.print(millis() - lastValueMillis);
        Serial.print(" NotifyValue=");
        Serial.print(floatOf(notifyValue));
        Serial.print(" fixedNextValue=");
        Serial.println(floatOf(fixedNextValue));
#endif
        if (notifyValue != fixedNextValue) { // Don't send if no change
          notifyValue = fixedNextValue; 
          pResistanceCharacteristic->setValue((uint8_t*)&notifyValue, sizeof(value_t));
          pResistanceCharacteristic->notify();
          lastValueMillis = millis();
          delay(3); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
        }
      }

    }
  }

  // Tare button handling
  button1.tick();

  // Check the tilt
  // We only care about a change in tilt
  if (tilt()) {
    Serial.print("____ Tilt ____");
    Serial.println(millis());
    //digitalWrite(BUILTIN_LED, LOW);
  }
  
  // Sleep if we've been quiet for too long
  //if (millis() - lastMillis > sleepTimeoutMillis) {
    //!!!! Start by just using tilt millis for sleep
  if (millis() - lastTiltTime > sleepTimeoutMillis) {
    const uint32_t LIGHT_SLEEP_TIMEOUT_USECS = 2 * 1000000;
    esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_TIMEOUT_USECS);
//!!!!    Serial.println("Light sleep");
    digitalWrite(BUILTIN_LED, HIGH);
    // esp_light_sleep_start();
//!!!    Serial.println("Wakeup");
    digitalWrite(BUILTIN_LED, LOW);
    lastTiltTime = millis();
    // esp_deep_sleep_start();
    lastMillis = millis();
  }

  // TODO Come up with a good value for this
  //delay(500);
  
}
