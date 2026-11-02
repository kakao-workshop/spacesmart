#include "DashboardLink.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>

#include "AppLogic.h"
namespace DashboardLink {
namespace {
struct Packet {
  char text[96];
};
QueueHandle_t queue = nullptr;
std::atomic<bool> online{false}, authenticatedWriter{false};
BLECharacteristic* tx=nullptr;
char outgoing[240]={}; size_t outLength=0,outOffset=0; uint32_t lastChunk=0;
Logic::LineBuffer input;
class Receiver : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    authenticatedWriter.store(true);
    const String data = characteristic->getValue();
    for (size_t i = 0; i < data.length(); ++i) {
      if (data[i] == '\0') continue;
      if (input.push(data[i]) == Logic::LineBuffer::Result::Ready) {
        Packet p = {};
        snprintf(p.text, sizeof(p.text), "%s", input.data());
        xQueueSend(queue, &p, 0);
      }
    }
  }
} receiver;
class Connections : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { online.store(true); }
  void onDisconnect(BLEServer*) override {
    online.store(false); authenticatedWriter.store(false);
    input = Logic::LineBuffer{};
    xQueueReset(queue);
  }
} connections;
}  // namespace
bool begin() {
  queue = xQueueCreate(8, sizeof(Packet));
  if (!queue) return false;
  BLEDevice::init("SpaceSmart");
  BLEDevice::setMTU(128);
  static BLESecurity security;
  security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  security.setCapability(ESP_IO_CAP_NONE);
  security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(&connections);
  server->advertiseOnDisconnect(true);
  BLEService* service = server->createService(SERVICE_UUID);
  BLECharacteristic* rx = service->createCharacteristic(
      RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_ENC);
  rx->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
  rx->setCallbacks(&receiver);
  tx=service->createCharacteristic(TX_UUID,BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_READ_ENC | BLECharacteristic::PROPERTY_NOTIFY);
  tx->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
  auto* cccd=new BLE2902(); cccd->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED); tx->addDescriptor(cccd);
  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();
  return true;
}
bool canSend() { return online.load()&&authenticatedWriter.load()&&outOffset==outLength; }
bool send(const char* line) {
  if(!canSend()||strlen(line)>=sizeof(outgoing))return false;
  strcpy(outgoing,line);outLength=strlen(line);outOffset=0;return true;
}
void tick(uint32_t now) {
  if(!online.load()||!authenticatedWriter.load()){outLength=outOffset=0;return;}
  if(outOffset<outLength&&uint32_t(now-lastChunk)>=12){
    lastChunk=now; size_t n=std::min(size_t(20),outLength-outOffset);
    tx->setValue((uint8_t*)outgoing+outOffset,n);tx->notify();outOffset+=n;
  }
}
bool connected() { return online.load(); }
bool receive(char* destination, size_t capacity) {
  Packet p;
  if (!queue || xQueueReceive(queue, &p, 0) != pdTRUE) return false;
  snprintf(destination, capacity, "%s", p.text);
  return true;
}
}  // namespace DashboardLink
