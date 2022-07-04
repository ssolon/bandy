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

BLEServer* pServer = NULL;
BLECharacteristic* pBatteryLevelCharacteristic = NULL;
BLECharacteristic* pCountCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;
uint8_t batteryLevel = 80;

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "(uint16_t)0x1826" //"9b3116d8-f7e0-11ec-b939-0242ac120002"
#define CHARACTERISTIC_UUID "a6351a0c-f7e0-11ec-b939-0242ac120002"

const BLEUUID batteryServiceUUID = BLEUUID((uint16_t) 0x180f);
const BLEUUID batteryLevelUUID = BLEUUID((uint16_t) 0x2a19);

const BLEUUID fitnessServiceUUID = BLEUUID((uint16_t)0x1826);
const BLEUUID countCharacteristicUUID = BLEUUID((uint16_t) 0x2aeb);

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      Serial.println("client disconnected");
      deviceConnected = false;
    }
};

const int threshold = 30;
const int LED_RED = 22;
const int LED_GREEN = 21;
const int LED_WAITING = LED_RED;
const int LED_CONNECTED = LED_GREEN;

const int BUZZER = 13;

void setState() {
  digitalWrite(LED_WAITING, deviceConnected ? LOW : HIGH);
  digitalWrite(LED_CONNECTED, deviceConnected ? HIGH : LOW);
  tone(BUZZER, deviceConnected ? 440 : 220, 250);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_WAITING, OUTPUT);
  pinMode(LED_CONNECTED, OUTPUT);

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

  // Create the BLE Service
  BLEService *pService = pServer->createService(fitnessServiceUUID);

  // Create a BLE Characteristic
  pCountCharacteristic = pService->createCharacteristic(
                      countCharacteristicUUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pCountCharacteristic->addDescriptor(new BLE2902());

  BLE2904* pCountDescriptor = new BLE2904();
  pCountDescriptor->setFormat(BLE2904::FORMAT_SINT32);
  pCountDescriptor->setUnit(0x2702);

  pCountCharacteristic->addDescriptor(pCountDescriptor);

  BLEDescriptor* pCountDescriptionDescriptor = new BLEDescriptor(BLEUUID((uint16_t)0x2901), 8);
  pCountDescriptionDescriptor->setValue("Count");
  pCountCharacteristic->addDescriptor(pCountDescriptionDescriptor);

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
}


void loop() {
    // notify changed value
    if (deviceConnected) {
        pCountCharacteristic->setValue((uint8_t*)&value, 4);
        pCountCharacteristic->notify();
        value++;

        delay(3); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
    }
    // disconnecting
    if (!deviceConnected && oldDeviceConnected) {
        setState();
        delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        Serial.println("Restart advertising");
        oldDeviceConnected = deviceConnected;
    }
    // connecting
    if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        Serial.println("Client connected");
        oldDeviceConnected = deviceConnected;
        setState();
    }
    //!!!! Test out touch
    digitalWrite(LED_BUILTIN, touchRead(T0) < threshold ? HIGH : LOW);
    delay(500);
}