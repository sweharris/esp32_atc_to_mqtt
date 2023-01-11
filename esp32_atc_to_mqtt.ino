// This code is for an ESP32.  It's designed to received BLE Beacons from
// Xiami Mijia thermometers flashed with the PVVX firmware, set to
// custom broadcasts
//
// Since the ESP32 has two cores we split the workload; the "default" core
// runs loop() that keeps the MQTT service connected and (if configured)
// allows for network code updates.
// The second core is dedicated to processing the BLE beacon traffic
// and posting the JSON to the MQTT server.
//
// Since the ESP32 only has one radio it can't do BT and WiFi at the
// same time.  We set BT scanning to use 99% of the time.  Older esp32
// firmware may not work so well; this was tested with 2.0.6, which has
// better radio sharing.  Seems to work for me!
//
// (c) 2023 Stephen Harris

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <map>
#include <time.h>
#include "network_conn.h"

#define _mqttBase    "atc/"

// For MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// For bluetooth scanning
int scanTime = 0; // Scan forever
BLEScan *pBLEScan;

// For mapping MAC addresses to names
std::map<String, String> mac_names;

void map_setup()
{
#include "mac_to_name.h"
}

// Take an integer, divide by 100, return to <n> decimal places
String dp(char *place,int32_t val)
{
  char buf[10];
  sprintf(buf,place,val/100.0);
  return buf;
}

class ScanCallBack : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice dev)
  {
    // Now we can do our BT processing
    String mac=dev.getAddress().toString().c_str();
    mac.replace(":","");

    // We only want to look at data from our ATC devices
    // Basically payload length must be 19 (17 bytes from the ATC plus
    // the length string plus the type
    // Then the type must be 22
    // Then the next two bytes must be 0x1A 0x18 (the PVVX signature)
    uint8_t* payloadPtr = dev.getPayload();

    if (dev.getPayloadLength() != 19 ||
        *(payloadPtr+1) != 22 ||
        *(payloadPtr+2) != 0x1A ||
        *(payloadPtr+3) != 0x18)
    {
      // Serial.print("x");
      return;
    }

    // The data structure we have in PayLoadData is
    //     0: length (should be 18)
    //     1: type (should be 22)
    //   2-3: PVVX signature (should be 1A 18)
    //   4-9: MAC Address (low byte first)
    // 10-11: int16 tempC (low byte first)
    // 12-13: uint16 humidity (low byte first)
    // 14-15: uint16 Battery mV (low byte first)
    //    16: Battery percentage
    //    17: Broadcast count
    //    18: Flags

    Serial.println("====Entering====");

    String topic = _mqttBase + mac;
    Serial.println("Topic: " + topic);

    time_t now = time(nullptr);
    String tm = ctime(&now);
    tm.trim();

    // Hopefully this will also handle negative values!
    int16_t tempC=*(payloadPtr+10) + *(payloadPtr+11)*256;
    int16_t tempF=(tempC*9/5)+3200;
    uint16_t humid=*(payloadPtr+12) + *(payloadPtr+13)*256;
    uint16_t battmV=*(payloadPtr+14) + *(payloadPtr+15)*256;
    uint8_t battpct=*(payloadPtr+16);

    // Kludge up some JSON
    String JSON = String("{") \
                + "\"rx_by\":\"" + WiFi.localIP().toString() + "\"," \
                + "\"time\":" + String(now) +"," \
                + "\"date\":\"" + tm + "\"," \
                + "\"mac\":\"" + mac + "\"," \
                + "\"name\":\"" + mac_names[mac] + "\"," \
                + "\"tempC\":" + dp("%5.1f",tempC) + "," \
                + "\"tempF\":" + dp("%5.1f",tempF) + "," \
                + "\"humidity\":" + dp("%3.0f",humid) + "," \
                + "\"battMV\":" + String(battmV) + "," \
                + "\"battpct\":" + String(battpct) + \
                + "}";
    Serial.println(JSON);

    if (client.connected())
    {
      // OK, we know we're connected.  Send it
      client.publish(topic.c_str(),JSON.c_str(),true);
      Serial.println("Sent");
    }
    else
    {
      Serial.println("Dropped because not connected");
    }
  }
};

// We're gonna do netuploads and mqtt keep alives in a separate thread
TaskHandle_t Task1;  // For BLE

void setup()
{
  Serial.begin(115200);
  map_setup();

  Serial.println();
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);     // Connect to the network
  Serial.print("Connecting to ");
  Serial.print(ssid);
  Serial.println(" ...");

  int i = 0;
  while (WiFi.status() != WL_CONNECTED) // Wait for the Wi-Fi to connect
  {
    delay(1000);
    Serial.print(++i); Serial.print(' ');
    // Sometimes the ESP32 just fails to connect!  Reboot if unsuccesful
    if (i > 10 )
    {
      Serial.print("Give up; reboot!");
      ESP.restart();
    }
  }

  Serial.println('\n');
  Serial.println("Connection established!");
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP());
  Serial.print("Hostname:\t");
  Serial.println(WiFi.getHostname());

  // Get the current time.  Initial log lines may be wrong 'cos
  // it's not instant.  Timezone handling... eh, let's do it all in UTC
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  delay(1000);

  // Now we're on the network, setup the MQTT client
  client.setServer(mqttServer, mqttPort);

#if NETWORK_UPDATE
   __setup_updater();
#endif

  Serial.print("Main tasking running on core ");
  Serial.print(xPortGetCoreID());
  Serial.print(" with priority ");
  Serial.println(uxTaskPriorityGet(NULL));

  Serial.println();
  Serial.println("Scanning...");

  // create new scan and make it call the scan handler.  The "true"
  // means it'll repeat even if we've seen a beacon from a device
  // before
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new ScanCallBack(),true);
  pBLEScan->setActiveScan(false); //active scan uses more power so turn off
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99); // Bluetooth uses the radio most of the time

  // We will run the BLE scanner in the other core.  I set the
  // priority to be higher than the current task, but I don't know
  // if that really makes a difference.
  xTaskCreatePinnedToCore(
                    BLEscanaloop,               /* Task function. */
                    "BLEscanaloop",             /* name of task. */
                    10000,                      /* Stack size of task */
                    NULL,                       /* parameter of the task */
                    uxTaskPriorityGet(NULL)+1,  /* priority of the task */
                    &Task1,                     /* Task handle */
                    1-xPortGetCoreID());        /* pin task to other core */
}

// This thread is to handle the BLE scanning.  It's in the other core
void BLEscanaloop( void * pvParameters )
{
  Serial.print("Scanning tasking running on core ");
  Serial.print(xPortGetCoreID());
  Serial.print(" with priority ");
  Serial.println(uxTaskPriorityGet(NULL));

  while (1)
  {
    BLEScanResults foundDevices = pBLEScan->start(scanTime, false);

    // That should(!) never return; we don't care, anyway.  But just in case
    // delete results fromBLEScan buffer to release memory
    pBLEScan->clearResults();
    delay(2000);
  }
}

// This loop now just ensures MQTT is kept alive.  Connect if not
// connected.
void loop()
{
  // Try to reconnect to MQTT, in case we disconnected
  if (!client.connected())
  {
    // Generate a random ID each time
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);

    Serial.println("Connecting to MQTT Server " + String(mqttServer));
    if (client.connect(clientId.c_str()))
    {
      String msg="MQTT connected at ";
      time_t now = time(nullptr);
      msg += ctime(&now);
      msg.trim();
      msg += ".  I am " + WiFi.localIP().toString();
      Serial.println(msg);
    }
    else
    {
      Serial.println("failed with state " + client.state());
    }
  }

  // Keep MQTT alive
  client.loop();

#ifdef NETWORK_UPDATE
  __netupdateServer.handleClient();
#endif
}
