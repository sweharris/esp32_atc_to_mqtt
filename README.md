# esp32_atc_to_mqtt

There are some cheap Xiaomi Mijia thermometers out there.
What's interesting about them is that they can send their
measurements by bluetooth (eg to a mobile)
What's even more fun is that custom firmware can be upload
to them!

I'm using the PVVX firmware

    https://github.com/pvvx/ATC_MiThermometer

This will broadcast the data as part of the ble announcements
so any bluetooth receiver can passively listen and receive the
data.  It allows you to have many of these devices and efficiently
collect data without having to connect to each one individually.

---

This program is for an ESP32 device and acts as a passive scanner
(receiver) and posts the data to an MQTT server as a JSON string, which
can then be received by tools such as Home Assistant.

Note the PVVX firmware can broadcast in different formats.  I have it
configured for "custom"

The MQTT topic will be "atc/" followed by the device mac address of
the sending thermometer (eg "atc/123456789abc")

The JSON format is

    {
      "rx_by": "10.20.30.40",
      "time": 1673443719,
      "date": "Wed Jan 11 13:28:39 2023",
      "mac": "123456789abc",
      "name": "Kitchen",
      "tempC": 20.9,
      "tempF": 69.6,
      "humidity": 39,
      "battMV": 2967,
      "battpct": 85
    }

Note: the name field may be `""` if name has not been configured.

The date field is the human representation of the time field, in UTC

Broadcasts received before the ESP time has sync'd may show in 1970, but
will display accurately once NTP sync has completed.

---

To build you need to create two files.  Without them the build will
fail

You will need to make a file `network_conn.h` with contents similar to

    const char* ssid       = "your-ssid";
    const char* password   = "your-wpa-passwd";
    const char* mqttServer = "your-MQTT-server";
    const int mqttPort     = 1883;

in order to provide details of your network and MQTT server.

Next you will need to create a file `mac_to_name.h`.  This is a simple
list of entries such as

    mac_names["a4c138b12345"]="Kitchen";
    mac_names["a4c138812346"]="Bedroom";
    mac_names["a4c138a12347"]="Bathroom";

The key to the array is the MAC address (without :).  If you create a
blank file then the `name` entries in the JSON will just be `""`.

---

Originally I wrote some [python](https://github.com/sweharris/atc_to_mqtt)
to receive the broadcasts and publish to MQTT, but my central receiver
doesn't properly reach to my whole house; one room was commonly only
updating every 300 or more seconds.

So I wrote this, instead.

The idea behind this is that you can place multiple of these ESP32 in
your area.  Broadcasts may be picked up multiple times as a result,
but the MQTT topic should contain accurate data.  It's not so efficient
but it allows for the Bluetooth sensors to be placed distributed; no
worries about being close to a central receiver.

---
# atc_monitor.sh

This is a "monitor" app that will report on what is seen on the MQTT
server.  It can be useful to see if the ESP32s are properly reporting
in, and that each sensor is still sending.

---

(c) Stephen Harris, Jan 2023
MIT licensed.  See LICENSE file
