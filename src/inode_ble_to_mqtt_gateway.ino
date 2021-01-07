#include <MQTT.h>
#include "Particle.h"
#include "config.h"

// SYSTEM_MODE(MANUAL);

SerialLogHandler logHandler(LOG_LEVEL_TRACE);

const size_t SCAN_RESULT_MAX = 30;
BleScanResult scanResults[SCAN_RESULT_MAX];
LEDStatus ledOverride(RGB_COLOR_WHITE, LED_PATTERN_SOLID, LED_SPEED_NORMAL, LED_PRIORITY_IMPORTANT);

auto raw = 0;

void callback(char* topic, byte* payload, unsigned int length);
MQTT client(MQTT_ADDRESS, MQTT_PORT, callback);

void callback(char* topic, byte* payload, unsigned int length) {
    char p[length + 1];
    memcpy(p, payload, length);
    p[length] = NULL;
    if (!strcmp(p, "RED"))
      RGB.color(255, 0, 0);
    delay(1000);
}

void setup() {
    (void)logHandler;
    Log.info("starting...");
    Particle.publish("starting...");
    Particle.variable("raw", &raw, INT);

    BLE.on();
    BLE.setPPCP(12, 12, 0, 200);
 
    client.connect("mqtt_client");
    if (client.isConnected()) {
        client.publish("outTopic/message","hello world");
        client.subscribe("inTopic/message");
    }
 
    // if (waitForNot(Particle.connected, 10000)) {
    //     Log.info("not connected");
    // }
}

void array_to_string(byte array[], unsigned int len, char buffer[]) {
    for (unsigned int i = 0; i < len; i++) {
        byte nib1 = (array[i] >> 4) & 0x0F;
        byte nib2 = (array[i] >> 0) & 0x0F;
        buffer[i*2+0] = nib1  < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
        buffer[i*2+1] = nib2  < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
    }
    buffer[len*2] = '\0';
}

void parseiNodeMeter(unsigned char *data, String mac) {
  // 00 00 A7 09 00 00 0A C0 A4 00 80 00 00
    
  struct iNodeMeter {
    unsigned short rawAvg;
    unsigned int rawSum;
    unsigned short constant : 14;
    unsigned short unit : 2;
    unsigned char lightLevel : 4;
    unsigned char batteryLevel : 4;
    unsigned short weekDayTotal : 12;
    unsigned short weekDay : 4;
  } __attribute__((packed));

  iNodeMeter *meter = (iNodeMeter*)data;

  auto unitDefault = 1;
  auto unitMultiplier = 1;
  auto unitAvgName = "cnt";
  auto unitSumName = "cnt";

  switch (meter->unit) {
    case 0:
      unitDefault = 100;
      unitMultiplier = 1000;
      unitAvgName = "W";
      unitSumName = "Wh";
      break;
      
    case 1:
      unitDefault = 1000;
      unitMultiplier = 1000;
      unitAvgName = "dm3";
      unitSumName = "dm3";
      break;
  }

  auto constant = meter->constant;
  if (constant <= 0) {
    constant = unitDefault;
  }

  auto batteryLevel = 10 * ((meter->batteryLevel < 11 ? meter->batteryLevel : 11) - 1);
  auto batteryVoltage = batteryLevel * 1200 / 100 + 1800;
  auto lightLevel = meter->lightLevel * 100 / 15;
  auto avg = 60 * unitMultiplier * (unsigned int)meter->rawAvg / constant;
  auto sum = unitMultiplier * (unsigned int)meter->rawSum / constant;
    
  // char str[32] = "";
  // array_to_string(data, 13, str);
  // Particle.publish(str);
    
  if (client.isConnected()) {
      client.publish("/inode/" + mac + "/total/raw", String(meter->rawSum));
      client.publish("/inode/" + mac + "/total/cnt", String(sum));
      client.publish("/inode/" + mac + "/avg/raw", String(meter->rawAvg));
      client.publish("/inode/" + mac + "/avg/cnt", String(avg));
      client.publish("/inode/" + mac + "/battery/level", String(batteryLevel));
      client.publish("/inode/" + mac + "/battery/mV", String(batteryVoltage));
      client.publish("/inode/" + mac + "/light/level", String(lightLevel));
      client.publish("/inode/" + mac + "/device/constant", String(meter->constant));
      client.publish("/inode/" + mac + "/device/unit", String(meter->unit));
  }
  Log.info("Raw value: " + String(meter->rawSum));
}

bool isiNodeDevice(BleScanResult scanResult) {
  auto address = scanResult.address;
  if (address[0] == 0x00 && address[1] == 0x0b && address[2] == 0x57) {
    return true;
  }
  if (address[0] == 0xd0 && address[1] == 0xf0 && address[2] == 0x18) {
    return true;
  }
  if (address[5] == 0xd0 && address[4] == 0xf0 && address[3] == 0x18) {
    return true;
  }
  return false;
}

void loop() {
    if (client.isConnected())
        client.loop();
    
    RGB.color(0, 0, 255);
    BLE.setScanTimeout(100);
    int count = BLE.scan(scanResults, SCAN_RESULT_MAX);
  
    // Log.info("Found BLE devices: " + String(count));
    for (int i = 0; i < count; i++) {
        String mac = String::format("%02X", scanResults[i].address[5])
            + String::format("%02X",scanResults[i].address[4])
            + String::format("%02X",scanResults[i].address[3])
            + String::format("%02X",scanResults[i].address[2])
            + String::format("%02X",scanResults[i].address[1])
            + String::format("%02X",scanResults[i].address[0]);
        mac = mac.toLowerCase();
        String device = mac + " (rssi: " + (String)scanResults[i].rssi + ")";

        // uint8_t buf_name[BLE_MAX_ADV_DATA_LEN];
        // scanResults[i].advertisingData.get(BleAdvertisingDataType::COMPLETE_LOCAL_NAME , buf_name, BLE_MAX_ADV_DATA_LEN);
        // String name = (char*)buf_name;
        // Log.info("Name: " + name);
        // Log.info("Name: " + scanResults[i].advertisingData.deviceName());

        if(isiNodeDevice(scanResults[i]) == true) {
            Log.info(">>> Found Inode device: " + device + " <<<");
            RGB.color(0, 255, 0);

            if (client.isConnected()) {
              client.publish("/inode/" + mac + "/device/rssi", String(scanResults[i].rssi));
            }

            uint8_t buf[13];
            scanResults[i].advertisingData.get(BleAdvertisingDataType::MANUFACTURER_SPECIFIC_DATA , buf, BLE_MAX_ADV_DATA_LEN);
            Particle.publish("device", device);  
        
            if (buf[0] != 0x90 && buf[0] != 0xa0) {
                // Particle.publish("not inode data");
            } else {
                switch(buf[1]) {
                    case 0x82:
                        uint8_t buf_2[11];
                        memcpy(buf_2, &buf[2], sizeof(buf) - 2);
                        parseiNodeMeter(buf_2, mac);
                    }
            }
        } else {
            RGB.color(255, 0, 0);
            Log.info("Device with MAC: " + device + " is not an Inode hardware");
        }
    }  
    delay(100);
    RGB.color(0, 0, 0);
}
