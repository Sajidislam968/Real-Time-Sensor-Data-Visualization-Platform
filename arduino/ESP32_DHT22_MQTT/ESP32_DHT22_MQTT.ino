#include <DHT.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <math.h>
#include <stdio.h>

constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t DHT_TYPE = DHT22;

constexpr uint16_t MQTT_PORT = 1883;
constexpr unsigned long SENSOR_INTERVAL_MS = 2000;
constexpr unsigned long MQTT_RETRY_INTERVAL_MS = 5000;

const char* const WIFI_SSID = "YOUR_WIFI_NAME";
const char* const WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* const MQTT_BROKER = "192.168.1.100";
const char* const MQTT_USERNAME = "";
const char* const MQTT_PASSWORD = "";

const char* const TEMPERATURE_TOPIC =
    "sensors/room1/temperature";
const char* const HUMIDITY_TOPIC =
    "sensors/room1/humidity";
const char* const STATUS_TOPIC =
    "sensors/room1/status";

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long previousSensorRead = 0;
unsigned long previousMqttAttempt = 0;

void connectToWiFi()
{
    Serial.println();
    Serial.print("Connecting to Wi-Fi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print('.');
    }

    Serial.println();
    Serial.println("Wi-Fi connected.");
    Serial.print("ESP32 IP address: ");
    Serial.println(WiFi.localIP());
}

bool connectToMqtt()
{
    Serial.print("Connecting to MQTT broker ");
    Serial.print(MQTT_BROKER);
    Serial.print(':');
    Serial.println(MQTT_PORT);

    String clientId = "ESP32-DHT22-";
    clientId += String(
        static_cast<uint32_t>(ESP.getEfuseMac()),
        HEX
    );

    const bool connected =
        MQTT_USERNAME[0] == '\0'
        ? mqttClient.connect(
            clientId.c_str(),
            STATUS_TOPIC,
            0,
            true,
            "offline"
        )
        : mqttClient.connect(
            clientId.c_str(),
            MQTT_USERNAME,
            MQTT_PASSWORD,
            STATUS_TOPIC,
            0,
            true,
            "offline"
        );

    if (!connected)
    {
        Serial.print("MQTT connection failed. State: ");
        Serial.println(mqttClient.state());
        return false;
    }

    Serial.println("MQTT connected.");

    if (!mqttClient.publish(STATUS_TOPIC, "online", true))
    {
        Serial.println("ERROR: Could not publish online status.");
    }

    return true;
}

void publishSensorData()
{
    const float humidity = dht.readHumidity();
    const float temperatureC = dht.readTemperature();

    if (isnan(temperatureC) || isnan(humidity))
    {
        Serial.println("ERROR: Failed to read DHT22.");
        return;
    }

    char temperatureText[16];
    char humidityText[16];

    snprintf(
        temperatureText,
        sizeof(temperatureText),
        "%.1f",
        temperatureC
    );
    snprintf(
        humidityText,
        sizeof(humidityText),
        "%.1f",
        humidity
    );

    Serial.print("Temperature: ");
    Serial.print(temperatureText);
    Serial.print(" C | Humidity: ");
    Serial.print(humidityText);
    Serial.println(" %");

    const bool temperaturePublished = mqttClient.publish(
        TEMPERATURE_TOPIC,
        temperatureText,
        true
    );
    const bool humidityPublished = mqttClient.publish(
        HUMIDITY_TOPIC,
        humidityText,
        true
    );

    if (temperaturePublished && humidityPublished)
    {
        Serial.println("MQTT data published successfully.");
    }
    else
    {
        Serial.println("ERROR: MQTT sensor-data publishing failed.");
    }
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    dht.begin();
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setKeepAlive(30);

    connectToWiFi();
    connectToMqtt();
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Wi-Fi disconnected; reconnecting.");
        connectToWiFi();
    }

    const unsigned long currentTime = millis();

    if (!mqttClient.connected())
    {
        if (
            currentTime - previousMqttAttempt >=
            MQTT_RETRY_INTERVAL_MS
        )
        {
            previousMqttAttempt = currentTime;
            connectToMqtt();
        }
    }
    else
    {
        mqttClient.loop();
    }

    if (
        mqttClient.connected() &&
        currentTime - previousSensorRead >= SENSOR_INTERVAL_MS
    )
    {
        previousSensorRead = currentTime;
        publishSensorData();
    }
}
