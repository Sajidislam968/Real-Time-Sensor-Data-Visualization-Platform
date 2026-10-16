# ESP32 DHT22 MQTT Publisher

This Arduino IDE sketch reads a DHT22 temperature/humidity sensor connected to
an ESP32 and publishes retained MQTT values for the desktop dashboard.

## Hardware wiring

| DHT22 | ESP32 |
| --- | --- |
| VCC / `+` | 3.3V |
| DATA / `S` | GPIO 4 |
| GND / `-` | GND |

Use a pull-up resistor between DATA and VCC if required by your DHT22 module.

## Arduino IDE setup

1. Install the Espressif **ESP32** board platform in Arduino IDE's Boards
   Manager.
2. Install these libraries in Library Manager:
   - **DHT sensor library** by Adafruit
   - **Adafruit Unified Sensor**
   - **PubSubClient** by Nick O'Leary
3. Open `ESP32_DHT22_MQTT.ino` and select your ESP32 board and serial port.
4. Set `WIFI_SSID` and `WIFI_PASSWORD` to your Wi-Fi credentials.
5. Set `MQTT_BROKER` to the LAN IPv4 address of the computer running Mosquitto.
   The ESP32 and broker must be reachable on the same network; `127.0.0.1`
   will not work as the ESP32's broker address.
6. If the broker requires authentication, set `MQTT_USERNAME` and
   `MQTT_PASSWORD`; leave both empty for a broker configured without login.
7. Upload the sketch and open Serial Monitor at **115200 baud**.

The broker's TCP port defaults to `1883`. Ensure the broker listens on the
network interface and that its firewall allows the ESP32 to connect. The ESP32
uses 2.4 GHz Wi-Fi. Do not expose an anonymous broker to the public internet.

## MQTT messages

The sketch samples every two seconds and publishes these retained values:

| Topic | Payload |
| --- | --- |
| `sensors/room1/temperature` | Temperature in °C, e.g. `24.5` |
| `sensors/room1/humidity` | Relative humidity in %, e.g. `52.0` |
| `sensors/room1/status` | `online` while connected; retained `offline` Last Will on unexpected disconnect |

These topics match the desktop dashboard. Temperature thresholds used by the
dashboard are project-specific visualization rules, not safety limits.
