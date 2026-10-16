#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <GL/freeglut.h>
#include <MQTTClient.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <system_error>

// ==================================================
// MQTT configuration
// ==================================================

const char* const MQTT_CLIENT_ID =
    "SensorDashboard-FreeGLUT-01";

const char* const MQTT_SUBSCRIPTION =
    "sensors/room1/#";

const char* const TEMPERATURE_TOPIC =
    "sensors/room1/temperature";

const char* const HUMIDITY_TOPIC =
    "sensors/room1/humidity";

const char* const STATUS_TOPIC =
    "sensors/room1/status";

constexpr int MQTT_QOS = 0;
constexpr long MQTT_RECEIVE_TIMEOUT_MS = 0L;

// ==================================================
// Temperature thresholds and states
// ==================================================

constexpr float MIN_TEMPERATURE = 0.0f;
constexpr float MAX_TEMPERATURE = 50.0f;

constexpr float COLD_TEMPERATURE = 20.0f;
constexpr float ALERT_TEMPERATURE = 35.0f;

enum class TemperatureState
{
    Unknown,
    Cold,
    Normal,
    High
};

TemperatureState currentTemperatureState =
    TemperatureState::Unknown;

// ==================================================
// MQTT state
// ==================================================

MQTTClient mqttClient = nullptr;

bool mqttClientCreated = false;
bool mqttConnected = false;

std::string mqttStatusText = "STARTING";
std::string esp32StatusText = "UNKNOWN";

std::chrono::steady_clock::time_point nextReconnectAttempt =
    std::chrono::steady_clock::now();

std::chrono::steady_clock::time_point lastSensorMessageTime =
    std::chrono::steady_clock::now();

constexpr int SENSOR_TIMEOUT_SECONDS = 8;

// ==================================================
// Sensor state
// ==================================================

float temperature = 0.0f;
float humidity = 0.0f;

bool hasTemperature = false;
bool hasHumidity = false;

// ==================================================
// CSV logging state
// ==================================================

std::ofstream csvFile;
std::string csvFilePath;

bool csvLoggingReady = false;
std::string csvStatusText = "STARTING";

bool temperatureUpdatedSinceLastLog = false;
bool humidityUpdatedSinceLastLog = false;

unsigned long csvRowsWritten = 0;

// ==================================================
// Dashboard animation state
// ==================================================

bool alertVisible = true;
int alertCounter = 0;

// ==================================================
// Temperature history and heat map
// ==================================================

std::deque<float> temperatureHistory;

constexpr std::size_t MAX_HISTORY_POINTS = 80;
constexpr std::size_t HEATMAP_POINTS = 50;
constexpr int HEATMAP_COLUMNS = 10;
constexpr int HEATMAP_ROWS = 5;

// ==================================================
// Statistics and abnormal-event history
// ==================================================

bool statisticsInitialized = false;

float minimumRecordedTemperature = 0.0f;
float maximumRecordedTemperature = 0.0f;

double temperatureSum = 0.0;
unsigned long temperatureReadingCount = 0;

bool abnormalEventActive = false;

TemperatureState activeAbnormalState =
    TemperatureState::Unknown;

std::chrono::steady_clock::time_point currentEventStartTime =
    std::chrono::steady_clock::now();

unsigned long totalColdEvents = 0;
unsigned long totalHighEvents = 0;

std::string lastEventTimestamp = "None";

std::deque<std::string> eventHistory;
constexpr std::size_t MAX_EVENT_HISTORY_ITEMS = 3;

// ==================================================
// Utility drawing functions
// ==================================================

void drawText(
    float x,
    float y,
    const char* text,
    void* font
)
{
    glRasterPos2f(x, y);

    for (
        const char* character = text;
        *character != '\0';
        ++character
    )
    {
        glutBitmapCharacter(font, *character);
    }
}

int getTextWidth(
    const char* text,
    void* font
)
{
    int width = 0;

    for (
        const char* character = text;
        *character != '\0';
        ++character
    )
    {
        width += glutBitmapWidth(
            font,
            *character
        );
    }

    return width;
}

void drawCenteredText(
    float centerX,
    float y,
    const char* text,
    void* font
)
{
    const float textWidth =
        static_cast<float>(
            getTextWidth(text, font)
        );

    drawText(
        centerX - textWidth / 2.0f,
        y,
        text,
        font
    );
}

void drawFilledRectangle(
    float left,
    float bottom,
    float right,
    float top
)
{
    glBegin(GL_QUADS);

    glVertex2f(left, bottom);
    glVertex2f(right, bottom);
    glVertex2f(right, top);
    glVertex2f(left, top);

    glEnd();
}

void drawRectangleOutline(
    float left,
    float bottom,
    float right,
    float top
)
{
    glBegin(GL_LINE_LOOP);

    glVertex2f(left, bottom);
    glVertex2f(right, bottom);
    glVertex2f(right, top);
    glVertex2f(left, top);

    glEnd();
}

void drawHorizontalLine(
    float left,
    float right,
    float y
)
{
    glBegin(GL_LINES);

    glVertex2f(left, y);
    glVertex2f(right, y);

    glEnd();
}

// ==================================================
// Temperature state helpers
// ==================================================

TemperatureState getTemperatureState(float value)
{
    if (value < COLD_TEMPERATURE)
    {
        return TemperatureState::Cold;
    }

    if (value >= ALERT_TEMPERATURE)
    {
        return TemperatureState::High;
    }

    return TemperatureState::Normal;
}

const char* getTemperatureStateName(
    TemperatureState state
)
{
    switch (state)
    {
        case TemperatureState::Cold:
            return "COLD";

        case TemperatureState::Normal:
            return "NORMAL";

        case TemperatureState::High:
            return "HIGH";

        default:
            return "UNKNOWN";
    }
}

bool isAbnormalState(
    TemperatureState state
)
{
    return
        state == TemperatureState::Cold ||
        state == TemperatureState::High;
}

// ==================================================
// CSV and time functions
// ==================================================

std::string getExecutableDirectory()
{
#ifdef _WIN32
    char executablePath[MAX_PATH];

    const DWORD pathLength =
        GetModuleFileNameA(
            nullptr,
            executablePath,
            MAX_PATH
        );

    if (
        pathLength == 0 ||
        pathLength >= MAX_PATH
    )
    {
        return ".";
    }

    const std::string completePath(
        executablePath,
        pathLength
    );

    const std::size_t separatorPosition =
        completePath.find_last_of("\\/");

    if (
        separatorPosition ==
        std::string::npos
    )
    {
        return ".";
    }

    return completePath.substr(
        0,
        separatorPosition
    );
#else
    std::error_code error;
    const std::filesystem::path executablePath =
        std::filesystem::read_symlink(
            "/proc/self/exe",
            error
        );

    if (error)
    {
        std::cerr
            << "WARNING: Could not resolve the executable path: "
            << error.message()
            << ". Saving CSV in the current directory.\n";

        return ".";
    }

    return executablePath.parent_path().string();
#endif
}

const char* getMqttAddress()
{
    const char* configuredAddress =
        std::getenv("SENSOR_MQTT_ADDRESS");

    if (
        configuredAddress != nullptr &&
        configuredAddress[0] != '\0'
    )
    {
        return configuredAddress;
    }

    return "tcp://127.0.0.1:1883";
}

std::string createTimestamp()
{
    const auto currentTime =
        std::chrono::system_clock::now();

    const std::time_t currentTimeValue =
        std::chrono::system_clock::to_time_t(
            currentTime
        );

    const std::tm* localTime =
        std::localtime(
            &currentTimeValue
        );

    if (localTime == nullptr)
    {
        return "unknown-time";
    }

    char timestamp[32];

    std::strftime(
        timestamp,
        sizeof(timestamp),
        "%Y-%m-%d %H:%M:%S",
        localTime
    );

    return timestamp;
}

bool fileAlreadyContainsData(
    const std::string& path
)
{
    std::ifstream existingFile(
        path,
        std::ios::binary |
        std::ios::ate
    );

    if (!existingFile.is_open())
    {
        return false;
    }

    return existingFile.tellg() > 0;
}

bool initializeCsvLogging()
{
    csvFilePath = (
        std::filesystem::path(
            getExecutableDirectory()
        ) /
        "sensor_data.csv"
    ).string();

    const bool csvAlreadyHasData =
        fileAlreadyContainsData(
            csvFilePath
        );

    csvFile.open(
        csvFilePath,
        std::ios::out |
        std::ios::app
    );

    if (!csvFile.is_open())
    {
        csvLoggingReady = false;
        csvStatusText = "ERROR";

        std::cerr
            << "ERROR: Could not open CSV file:\n"
            << csvFilePath
            << '\n';

        return false;
    }

    if (!csvAlreadyHasData)
    {
        csvFile
            << "timestamp,"
            << "temperature_c,"
            << "humidity_percent,"
            << "alert_state\n";

        csvFile.flush();
    }

    csvLoggingReady = true;
    csvStatusText = "LOGGING";

    std::cout
        << "CSV logging enabled.\n"
        << "CSV file: "
        << csvFilePath
        << "\n\n";

    return true;
}

void tryLogSensorReading()
{
    if (!csvLoggingReady)
    {
        return;
    }

    if (
        !hasTemperature ||
        !hasHumidity
    )
    {
        return;
    }

    if (
        !temperatureUpdatedSinceLastLog ||
        !humidityUpdatedSinceLastLog
    )
    {
        return;
    }

    const TemperatureState state =
        getTemperatureState(
            temperature
        );

    csvFile
        << createTimestamp()
        << ','
        << std::fixed
        << std::setprecision(1)
        << temperature
        << ','
        << humidity
        << ','
        << getTemperatureStateName(state)
        << '\n';

    csvFile.flush();

    if (!csvFile.good())
    {
        csvLoggingReady = false;
        csvStatusText = "WRITE ERROR";

        std::cerr
            << "ERROR: Failed to write CSV data.\n";

        return;
    }

    temperatureUpdatedSinceLastLog = false;
    humidityUpdatedSinceLastLog = false;

    ++csvRowsWritten;

    std::cout
        << "CSV row saved: "
        << createTimestamp()
        << ", "
        << temperature
        << " C, "
        << humidity
        << " %, "
        << getTemperatureStateName(state)
        << '\n';
}

void cleanupCsvLogging()
{
    if (csvFile.is_open())
    {
        csvFile.flush();
        csvFile.close();
    }

    csvLoggingReady = false;
}

// ==================================================
// Temperature color mapping
// ==================================================

void setTemperatureColor(float value)
{
    if (value < 20.0f)
    {
        glColor3f(0.15f, 0.45f, 1.0f);
    }
    else if (value < 25.0f)
    {
        glColor3f(0.15f, 0.85f, 0.35f);
    }
    else if (value < 30.0f)
    {
        glColor3f(1.0f, 0.85f, 0.15f);
    }
    else if (value < ALERT_TEMPERATURE)
    {
        glColor3f(1.0f, 0.45f, 0.10f);
    }
    else
    {
        glColor3f(1.0f, 0.10f, 0.10f);
    }
}

void setStateColor(
    TemperatureState state
)
{
    switch (state)
    {
        case TemperatureState::Cold:
            glColor3f(0.10f, 0.35f, 0.85f);
            break;

        case TemperatureState::Normal:
            glColor3f(0.12f, 0.50f, 0.25f);
            break;

        case TemperatureState::High:
            glColor3f(0.90f, 0.05f, 0.05f);
            break;

        default:
            glColor3f(0.30f, 0.32f, 0.38f);
            break;
    }
}

// ==================================================
// Sensor connection status
// ==================================================

bool isSensorOnline()
{
    if (!hasTemperature)
    {
        return false;
    }

    const auto currentTime =
        std::chrono::steady_clock::now();

    const auto elapsedSeconds =
        std::chrono::duration_cast<
            std::chrono::seconds
        >(
            currentTime -
            lastSensorMessageTime
        ).count();

    return elapsedSeconds <=
           SENSOR_TIMEOUT_SECONDS;
}

// ==================================================
// Statistics and event-history functions
// ==================================================

void startAbnormalEvent(
    TemperatureState state,
    float value
)
{
    abnormalEventActive = true;
    activeAbnormalState = state;

    currentEventStartTime =
        std::chrono::steady_clock::now();

    lastEventTimestamp =
        createTimestamp();

    if (state == TemperatureState::Cold)
    {
        ++totalColdEvents;
    }
    else if (state == TemperatureState::High)
    {
        ++totalHighEvents;
    }

    char eventEntry[120];

    std::snprintf(
        eventEntry,
        sizeof(eventEntry),
        "%s | %s | %.1f C",
        lastEventTimestamp.c_str(),
        getTemperatureStateName(state),
        value
    );

    eventHistory.push_front(
        eventEntry
    );

    if (
        eventHistory.size() >
        MAX_EVENT_HISTORY_ITEMS
    )
    {
        eventHistory.pop_back();
    }

    std::cout
        << "ABNORMAL EVENT STARTED: "
        << eventEntry
        << '\n';
}

void updateStatisticsAndEventHistory(
    float value
)
{
    if (!statisticsInitialized)
    {
        minimumRecordedTemperature = value;
        maximumRecordedTemperature = value;
        statisticsInitialized = true;
    }
    else
    {
        minimumRecordedTemperature =
            std::min(
                minimumRecordedTemperature,
                value
            );

        maximumRecordedTemperature =
            std::max(
                maximumRecordedTemperature,
                value
            );
    }

    temperatureSum += value;
    ++temperatureReadingCount;

    const TemperatureState newState =
        getTemperatureState(value);

    if (newState != currentTemperatureState)
    {
        if (isAbnormalState(newState))
        {
            startAbnormalEvent(
                newState,
                value
            );
        }
        else
        {
            abnormalEventActive = false;

            activeAbnormalState =
                TemperatureState::Unknown;
        }

        currentTemperatureState =
            newState;
    }
}

double getAverageTemperature()
{
    if (temperatureReadingCount == 0)
    {
        return 0.0;
    }

    return temperatureSum /
           static_cast<double>(
               temperatureReadingCount
           );
}

long long getCurrentEventDurationSeconds()
{
    if (!abnormalEventActive)
    {
        return 0;
    }

    const auto currentTime =
        std::chrono::steady_clock::now();

    return std::chrono::duration_cast<
        std::chrono::seconds
    >(
        currentTime -
        currentEventStartTime
    ).count();
}

// ==================================================
// Temperature history
// ==================================================

void addTemperatureToHistory(float value)
{
    temperatureHistory.push_back(value);

    if (
        temperatureHistory.size() >
        MAX_HISTORY_POINTS
    )
    {
        temperatureHistory.pop_front();
    }
}

// ==================================================
// MQTT functions
// ==================================================

bool connectToMqtt()
{
    if (!mqttClientCreated)
    {
        return false;
    }

    if (MQTTClient_isConnected(mqttClient))
    {
        mqttConnected = true;
        mqttStatusText = "CONNECTED";

        return true;
    }

    MQTTClient_connectOptions connectOptions =
        MQTTClient_connectOptions_initializer;

    connectOptions.keepAliveInterval = 20;
    connectOptions.cleansession = 1;

    std::cout
        << "Connecting to MQTT broker: "
        << getMqttAddress()
        << '\n';

    const int connectResult =
        MQTTClient_connect(
            mqttClient,
            &connectOptions
        );

    if (
        connectResult !=
        MQTTCLIENT_SUCCESS
    )
    {
        mqttConnected = false;
        mqttStatusText = "DISCONNECTED";

        std::cerr
            << "MQTT connection failed. Error: "
            << connectResult
            << '\n';

        return false;
    }

    const int subscribeResult =
        MQTTClient_subscribe(
            mqttClient,
            MQTT_SUBSCRIPTION,
            MQTT_QOS
        );

    if (
        subscribeResult !=
        MQTTCLIENT_SUCCESS
    )
    {
        mqttConnected = false;
        mqttStatusText =
            "SUBSCRIBE ERROR";

        std::cerr
            << "MQTT subscription failed. Error: "
            << subscribeResult
            << '\n';

        MQTTClient_disconnect(
            mqttClient,
            1000
        );

        return false;
    }

    mqttConnected = true;
    mqttStatusText = "CONNECTED";

    std::cout
        << "MQTT connected successfully.\n"
        << "Subscribed to: "
        << MQTT_SUBSCRIPTION
        << "\n\n";

    return true;
}

bool initializeMqtt()
{
    const int createResult =
        MQTTClient_create(
            &mqttClient,
            getMqttAddress(),
            MQTT_CLIENT_ID,
            MQTTCLIENT_PERSISTENCE_NONE,
            nullptr
        );

    if (
        createResult !=
        MQTTCLIENT_SUCCESS
    )
    {
        mqttClientCreated = false;
        mqttConnected = false;
        mqttStatusText =
            "CREATE ERROR";

        std::cerr
            << "Could not create MQTT client. Error: "
            << createResult
            << '\n';

        return false;
    }

    mqttClientCreated = true;

    return connectToMqtt();
}

void processMqttMessage(
    const std::string& topic,
    const std::string& payload
)
{
    try
    {
        if (topic == TEMPERATURE_TOPIC)
        {
            const float receivedTemperature =
                std::stof(payload);

            if (
                std::isfinite(
                    receivedTemperature
                )
            )
            {
                temperature =
                    receivedTemperature;

                hasTemperature = true;

                temperatureUpdatedSinceLastLog =
                    true;

                lastSensorMessageTime =
                    std::chrono::steady_clock::now();

                addTemperatureToHistory(
                    temperature
                );

                updateStatisticsAndEventHistory(
                    temperature
                );

                std::cout
                    << "Temperature received: "
                    << temperature
                    << " C\n";

                tryLogSensorReading();
            }
        }
        else if (topic == HUMIDITY_TOPIC)
        {
            const float receivedHumidity =
                std::stof(payload);

            if (
                std::isfinite(
                    receivedHumidity
                ) &&
                receivedHumidity >= 0.0f &&
                receivedHumidity <= 100.0f
            )
            {
                humidity =
                    receivedHumidity;

                hasHumidity = true;

                humidityUpdatedSinceLastLog =
                    true;

                lastSensorMessageTime =
                    std::chrono::steady_clock::now();

                std::cout
                    << "Humidity received: "
                    << humidity
                    << " %\n";

                tryLogSensorReading();
            }
        }
        else if (topic == STATUS_TOPIC)
        {
            esp32StatusText = payload;

            lastSensorMessageTime =
                std::chrono::steady_clock::now();

            std::cout
                << "ESP32 status: "
                << payload
                << '\n';
        }
    }
    catch (const std::exception&)
    {
        std::cerr
            << "Invalid MQTT payload on topic "
            << topic
            << ": "
            << payload
            << '\n';
    }
}

void pollMqtt()
{
    if (!mqttClientCreated)
    {
        return;
    }

    if (
        !MQTTClient_isConnected(
            mqttClient
        )
    )
    {
        mqttConnected = false;
        mqttStatusText = "DISCONNECTED";

        const auto currentTime =
            std::chrono::steady_clock::now();

        if (
            currentTime >=
            nextReconnectAttempt
        )
        {
            connectToMqtt();

            nextReconnectAttempt =
                currentTime +
                std::chrono::seconds(5);
        }

        return;
    }

    mqttConnected = true;
    mqttStatusText = "CONNECTED";

    for (
        int messageIndex = 0;
        messageIndex < 10;
        ++messageIndex
    )
    {
        char* topicName = nullptr;
        int topicLength = 0;

        MQTTClient_message* message =
            nullptr;

        const int receiveResult =
            MQTTClient_receive(
                mqttClient,
                &topicName,
                &topicLength,
                &message,
                MQTT_RECEIVE_TIMEOUT_MS
            );

        if (
            receiveResult !=
            MQTTCLIENT_SUCCESS
        )
        {
            mqttConnected = false;
            mqttStatusText =
                "RECEIVE ERROR";

            std::cerr
                << "MQTT receive error: "
                << receiveResult
                << '\n';

            break;
        }

        if (message == nullptr)
        {
            break;
        }

        std::string topic;

        if (topicLength > 0)
        {
            topic.assign(
                topicName,
                static_cast<std::size_t>(
                    topicLength
                )
            );
        }
        else if (topicName != nullptr)
        {
            topic = topicName;
        }

        const std::string payload(
            static_cast<const char*>(
                message->payload
            ),
            static_cast<std::size_t>(
                message->payloadlen
            )
        );

        processMqttMessage(
            topic,
            payload
        );

        MQTTClient_freeMessage(
            &message
        );

        if (topicName != nullptr)
        {
            MQTTClient_free(
                topicName
            );
        }
    }
}

void cleanupMqtt()
{
    if (!mqttClientCreated)
    {
        return;
    }

    if (
        MQTTClient_isConnected(
            mqttClient
        )
    )
    {
        MQTTClient_disconnect(
            mqttClient,
            1000
        );
    }

    MQTTClient_destroy(
        &mqttClient
    );

    mqttClientCreated = false;
    mqttConnected = false;
}

// ==================================================
// Thermometer
// ==================================================

void drawThermometer(float value)
{
    constexpr float left = 42.0f;
    constexpr float bottom = 105.0f;
    constexpr float width = 72.0f;
    constexpr float height = 425.0f;

    const float right =
        left + width;

    const float top =
        bottom + height;

    const float displayedValue =
        hasTemperature
        ? value
        : MIN_TEMPERATURE;

    const float clampedTemperature =
        std::clamp(
            displayedValue,
            MIN_TEMPERATURE,
            MAX_TEMPERATURE
        );

    const float fillRatio =
        (
            clampedTemperature -
            MIN_TEMPERATURE
        ) /
        (
            MAX_TEMPERATURE -
            MIN_TEMPERATURE
        );

    const float fillTop =
        bottom +
        height * fillRatio;

    glColor3f(
        0.15f,
        0.18f,
        0.23f
    );

    drawFilledRectangle(
        left,
        bottom,
        right,
        top
    );

    if (hasTemperature)
    {
        setTemperatureColor(value);
    }
    else
    {
        glColor3f(
            0.35f,
            0.35f,
            0.40f
        );
    }

    drawFilledRectangle(
        left + 7.0f,
        bottom + 7.0f,
        right - 7.0f,
        fillTop
    );

    glColor3f(
        0.90f,
        0.90f,
        0.95f
    );

    glLineWidth(3.0f);

    drawRectangleOutline(
        left,
        bottom,
        right,
        top
    );

    glColor3f(
        0.80f,
        0.80f,
        0.85f
    );

    drawCenteredText(
        (left + right) / 2.0f,
        top + 25.0f,
        "THERMOMETER",
        GLUT_BITMAP_HELVETICA_12
    );

    drawText(
        right + 15.0f,
        bottom - 3.0f,
        "0",
        GLUT_BITMAP_HELVETICA_18
    );

    drawText(
        right + 15.0f,
        bottom +
        height * 0.5f -
        3.0f,
        "25",
        GLUT_BITMAP_HELVETICA_18
    );

    drawText(
        right + 15.0f,
        top - 3.0f,
        "50",
        GLUT_BITMAP_HELVETICA_18
    );
}

// ==================================================
// Trend graph
// ==================================================

void drawTrendGraph()
{
    constexpr float graphLeft =
        165.0f;

    constexpr float graphBottom =
        285.0f;

    constexpr float graphRight =
        900.0f;

    constexpr float graphTop =
        390.0f;

    constexpr float graphMinTemperature =
        15.0f;

    constexpr float graphMaxTemperature =
        45.0f;

    glColor3f(
        0.10f,
        0.13f,
        0.18f
    );

    drawFilledRectangle(
        graphLeft,
        graphBottom,
        graphRight,
        graphTop
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    glLineWidth(1.5f);

    drawRectangleOutline(
        graphLeft,
        graphBottom,
        graphRight,
        graphTop
    );

    glColor3f(
        0.75f,
        0.78f,
        0.84f
    );

    drawText(
        graphLeft + 15.0f,
        graphTop - 20.0f,
        "LIVE MQTT TEMPERATURE TREND",
        GLUT_BITMAP_HELVETICA_12
    );

    const float coldRatio =
        (
            COLD_TEMPERATURE -
            graphMinTemperature
        ) /
        (
            graphMaxTemperature -
            graphMinTemperature
        );

    const float coldY =
        graphBottom +
        coldRatio *
        (
            graphTop -
            graphBottom
        );

    glColor3f(
        0.15f,
        0.45f,
        1.0f
    );

    glLineWidth(1.0f);

    drawHorizontalLine(
        graphLeft,
        graphRight,
        coldY
    );

    drawText(
        graphRight - 75.0f,
        coldY + 4.0f,
        "20 C",
        GLUT_BITMAP_HELVETICA_10
    );

    const float alertRatio =
        (
            ALERT_TEMPERATURE -
            graphMinTemperature
        ) /
        (
            graphMaxTemperature -
            graphMinTemperature
        );

    const float alertY =
        graphBottom +
        alertRatio *
        (
            graphTop -
            graphBottom
        );

    glColor3f(
        0.75f,
        0.15f,
        0.15f
    );

    drawHorizontalLine(
        graphLeft,
        graphRight,
        alertY
    );

    drawText(
        graphRight - 75.0f,
        alertY + 4.0f,
        "35 C",
        GLUT_BITMAP_HELVETICA_10
    );

    if (
        temperatureHistory.size() <
        2
    )
    {
        glColor3f(
            0.65f,
            0.70f,
            0.78f
        );

        drawCenteredText(
            (graphLeft + graphRight) / 2.0f,
            graphBottom + 35.0f,
            "Waiting for temperature history...",
            GLUT_BITMAP_HELVETICA_12
        );

        return;
    }

    glColor3f(
        0.15f,
        0.85f,
        1.0f
    );

    glLineWidth(2.5f);

    glBegin(GL_LINE_STRIP);

    for (
        std::size_t index = 0;
        index <
        temperatureHistory.size();
        ++index
    )
    {
        const float xRatio =
            static_cast<float>(index) /
            static_cast<float>(
                MAX_HISTORY_POINTS - 1
            );

        const float x =
            graphLeft +
            xRatio *
            (
                graphRight -
                graphLeft
            );

        const float graphValue =
            std::clamp(
                temperatureHistory[index],
                graphMinTemperature,
                graphMaxTemperature
            );

        const float yRatio =
            (
                graphValue -
                graphMinTemperature
            ) /
            (
                graphMaxTemperature -
                graphMinTemperature
            );

        const float y =
            graphBottom +
            yRatio *
            (
                graphTop -
                graphBottom
            );

        glVertex2f(x, y);
    }

    glEnd();
}

// ==================================================
// Temporal heat map
// ==================================================

void drawTemporalHeatMap()
{
    constexpr float panelLeft =
        925.0f;

    constexpr float panelBottom =
        310.0f;

    constexpr float panelRight =
        1260.0f;

    constexpr float panelTop =
        585.0f;

    constexpr float gridLeft =
        949.0f;

    constexpr float gridTop =
        508.0f;

    constexpr float cellWidth =
        25.0f;

    constexpr float cellHeight =
        18.0f;

    constexpr float cellGap =
        3.0f;

    glColor3f(
        0.10f,
        0.13f,
        0.18f
    );

    drawFilledRectangle(
        panelLeft,
        panelBottom,
        panelRight,
        panelTop
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    glLineWidth(1.5f);

    drawRectangleOutline(
        panelLeft,
        panelBottom,
        panelRight,
        panelTop
    );

    glColor3f(
        0.90f,
        0.95f,
        1.0f
    );

    drawCenteredText(
        (panelLeft + panelRight) / 2.0f,
        555.0f,
        "TEMPORAL HEAT MAP",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    drawCenteredText(
        (panelLeft + panelRight) / 2.0f,
        533.0f,
        "Latest 50 temperature readings",
        GLUT_BITMAP_HELVETICA_12
    );

    const std::size_t availableCount =
        temperatureHistory.size() <
        HEATMAP_POINTS
        ? temperatureHistory.size()
        : HEATMAP_POINTS;

    const std::size_t startIndex =
        temperatureHistory.size() >
        HEATMAP_POINTS
        ? temperatureHistory.size() -
          HEATMAP_POINTS
        : 0;

    for (
        std::size_t cellIndex = 0;
        cellIndex < HEATMAP_POINTS;
        ++cellIndex
    )
    {
        const int row =
            static_cast<int>(
                cellIndex /
                HEATMAP_COLUMNS
            );

        const int column =
            static_cast<int>(
                cellIndex %
                HEATMAP_COLUMNS
            );

        const float cellLeft =
            gridLeft +
            column *
            (
                cellWidth +
                cellGap
            );

        const float cellTop =
            gridTop -
            row *
            (
                cellHeight +
                cellGap
            );

        const float cellRight =
            cellLeft +
            cellWidth;

        const float cellBottom =
            cellTop -
            cellHeight;

        if (cellIndex < availableCount)
        {
            const float heatMapTemperature =
                temperatureHistory[
                    startIndex +
                    cellIndex
                ];

            setTemperatureColor(
                heatMapTemperature
            );
        }
        else
        {
            glColor3f(
                0.20f,
                0.22f,
                0.27f
            );
        }

        drawFilledRectangle(
            cellLeft,
            cellBottom,
            cellRight,
            cellTop
        );

        glColor3f(
            0.35f,
            0.38f,
            0.44f
        );

        glLineWidth(1.0f);

        drawRectangleOutline(
            cellLeft,
            cellBottom,
            cellRight,
            cellTop
        );

        if (
            availableCount > 0 &&
            cellIndex ==
            availableCount - 1
        )
        {
            glColor3f(
                1.0f,
                1.0f,
                1.0f
            );

            glLineWidth(2.5f);

            drawRectangleOutline(
                cellLeft - 1.5f,
                cellBottom - 1.5f,
                cellRight + 1.5f,
                cellTop + 1.5f
            );
        }
    }

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    drawText(
        949.0f,
        389.0f,
        "Oldest",
        GLUT_BITMAP_HELVETICA_10
    );

    drawText(
        1190.0f,
        389.0f,
        "Newest",
        GLUT_BITMAP_HELVETICA_10
    );

    // Separator keeps the legend visually clear of the heat-map grid.
    glColor3f(
        0.32f,
        0.36f,
        0.43f
    );

    glLineWidth(1.0f);

    drawHorizontalLine(
        panelLeft + 20.0f,
        panelRight - 20.0f,
        380.0f
    );

    glColor3f(
        0.90f,
        0.95f,
        1.0f
    );

    drawCenteredText(
        (panelLeft + panelRight) / 2.0f,
        368.0f,
        "COLOR SCALE",
        GLUT_BITMAP_HELVETICA_12
    );

    const float legendTemperatures[5] =
    {
        15.0f,
        22.0f,
        27.0f,
        32.0f,
        37.0f
    };

    const char* legendLabels[5] =
    {
        "Cold",
        "Normal",
        "Warm",
        "Hot",
        "Alert"
    };

    for (
        int legendIndex = 0;
        legendIndex < 5;
        ++legendIndex
    )
    {
        const float legendLeft =
            952.0f +
            legendIndex *
            61.0f;

        const float legendBottom =
            337.0f;

        const float legendRight =
            legendLeft +
            28.0f;

        const float legendTop =
            357.0f;

        setTemperatureColor(
            legendTemperatures[
                legendIndex
            ]
        );

        drawFilledRectangle(
            legendLeft,
            legendBottom,
            legendRight,
            legendTop
        );

        glColor3f(
            0.75f,
            0.78f,
            0.84f
        );

        drawCenteredText(
            (legendLeft + legendRight) / 2.0f,
            322.0f,
            legendLabels[
                legendIndex
            ],
            GLUT_BITMAP_HELVETICA_10
        );
    }
}

// ==================================================
// Alert and statistics panel
// ==================================================

void drawAlertStatisticsPanel()
{
    constexpr float panelLeft =
        925.0f;

    constexpr float panelBottom =
        35.0f;

    constexpr float panelRight =
        1260.0f;

    constexpr float panelTop =
        295.0f;

    const float panelCenter =
        (panelLeft + panelRight) / 2.0f;

    glColor3f(
        0.10f,
        0.13f,
        0.18f
    );

    drawFilledRectangle(
        panelLeft,
        panelBottom,
        panelRight,
        panelTop
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    glLineWidth(1.5f);

    drawRectangleOutline(
        panelLeft,
        panelBottom,
        panelRight,
        panelTop
    );

    glColor3f(
        0.90f,
        0.95f,
        1.0f
    );

    drawCenteredText(
        panelCenter,
        268.0f,
        "ALERTS & STATISTICS",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.32f,
        0.36f,
        0.43f
    );

    glLineWidth(1.0f);

    drawHorizontalLine(
        panelLeft + 18.0f,
        panelRight - 18.0f,
        254.0f
    );

    char minimumText[64];
    char maximumText[64];
    char averageText[64];
    char readingCountText[64];
    char coldEventsText[64];
    char highEventsText[64];
    char currentStateText[64];
    char durationText[64];

    if (statisticsInitialized)
    {
        std::snprintf(
            minimumText,
            sizeof(minimumText),
            "Minimum: %.1f C",
            minimumRecordedTemperature
        );

        std::snprintf(
            maximumText,
            sizeof(maximumText),
            "Maximum: %.1f C",
            maximumRecordedTemperature
        );

        std::snprintf(
            averageText,
            sizeof(averageText),
            "Average: %.1f C",
            getAverageTemperature()
        );
    }
    else
    {
        std::snprintf(
            minimumText,
            sizeof(minimumText),
            "Minimum: --.- C"
        );

        std::snprintf(
            maximumText,
            sizeof(maximumText),
            "Maximum: --.- C"
        );

        std::snprintf(
            averageText,
            sizeof(averageText),
            "Average: --.- C"
        );
    }

    std::snprintf(
        readingCountText,
        sizeof(readingCountText),
        "Readings: %lu",
        temperatureReadingCount
    );

    std::snprintf(
        coldEventsText,
        sizeof(coldEventsText),
        "Cold events: %lu",
        totalColdEvents
    );

    std::snprintf(
        highEventsText,
        sizeof(highEventsText),
        "High events: %lu",
        totalHighEvents
    );

    std::snprintf(
        currentStateText,
        sizeof(currentStateText),
        "State: %s",
        getTemperatureStateName(
            currentTemperatureState
        )
    );

    if (abnormalEventActive)
    {
        std::snprintf(
            durationText,
            sizeof(durationText),
            "Duration: %lld sec",
            getCurrentEventDurationSeconds()
        );
    }
    else
    {
        std::snprintf(
            durationText,
            sizeof(durationText),
            "Duration: --"
        );
    }

    glColor3f(
        0.75f,
        0.78f,
        0.84f
    );

    drawText(
        945.0f,
        230.0f,
        minimumText,
        GLUT_BITMAP_HELVETICA_12
    );

    drawText(
        1095.0f,
        230.0f,
        maximumText,
        GLUT_BITMAP_HELVETICA_12
    );

    drawText(
        945.0f,
        207.0f,
        averageText,
        GLUT_BITMAP_HELVETICA_12
    );

    drawText(
        1095.0f,
        207.0f,
        readingCountText,
        GLUT_BITMAP_HELVETICA_12
    );

    glColor3f(
        0.35f,
        0.65f,
        1.0f
    );

    drawText(
        945.0f,
        184.0f,
        coldEventsText,
        GLUT_BITMAP_HELVETICA_12
    );

    glColor3f(
        1.0f,
        0.30f,
        0.25f
    );

    drawText(
        1095.0f,
        184.0f,
        highEventsText,
        GLUT_BITMAP_HELVETICA_12
    );

    setStateColor(
        currentTemperatureState
    );

    drawText(
        945.0f,
        161.0f,
        currentStateText,
        GLUT_BITMAP_HELVETICA_12
    );

    glColor3f(
        0.75f,
        0.78f,
        0.84f
    );

    drawText(
        1095.0f,
        161.0f,
        durationText,
        GLUT_BITMAP_HELVETICA_12
    );

    char lastEventText[128];

    std::snprintf(
        lastEventText,
        sizeof(lastEventText),
        "Last abnormal event: %s",
        lastEventTimestamp.c_str()
    );

    drawText(
        945.0f,
        136.0f,
        lastEventText,
        GLUT_BITMAP_HELVETICA_12
    );

    glColor3f(
        0.32f,
        0.36f,
        0.43f
    );

    drawHorizontalLine(
        panelLeft + 18.0f,
        panelRight - 18.0f,
        123.0f
    );

    glColor3f(
        0.90f,
        0.95f,
        1.0f
    );

    drawText(
        945.0f,
        103.0f,
        "Recent abnormal events",
        GLUT_BITMAP_HELVETICA_12
    );

    if (eventHistory.empty())
    {
        glColor3f(
            0.65f,
            0.70f,
            0.78f
        );

        drawText(
            955.0f,
            79.0f,
            "No cold or high events recorded",
            GLUT_BITMAP_HELVETICA_10
        );
    }
    else
    {
        for (
            std::size_t index = 0;
            index < eventHistory.size();
            ++index
        )
        {
            char historyLine[140];

            std::snprintf(
                historyLine,
                sizeof(historyLine),
                "%u) %s",
                static_cast<unsigned int>(
                    index + 1
                ),
                eventHistory[index].c_str()
            );

            glColor3f(
                0.85f,
                0.65f,
                0.30f
            );

            drawText(
                950.0f,
                80.0f -
                static_cast<float>(index) *
                16.0f,
                historyLine,
                GLUT_BITMAP_HELVETICA_10
            );
        }
    }
}

// ==================================================
// Main status panel
// ==================================================

void drawStatusPanel()
{
    constexpr float left = 245.0f;
    constexpr float bottom = 140.0f;
    constexpr float right = 820.0f;
    constexpr float top = 265.0f;

    if (!hasTemperature)
    {
        glColor3f(
            0.30f,
            0.32f,
            0.38f
        );

        drawFilledRectangle(
            left,
            bottom,
            right,
            top
        );

        glColor3f(
            1.0f,
            1.0f,
            1.0f
        );

        drawCenteredText(
            (left + right) / 2.0f,
            198.0f,
            "WAITING FOR SENSOR DATA",
            GLUT_BITMAP_HELVETICA_18
        );

        return;
    }

    if (!isSensorOnline())
    {
        glColor3f(
            0.75f,
            0.42f,
            0.05f
        );

        drawFilledRectangle(
            left,
            bottom,
            right,
            top
        );

        glColor3f(
            1.0f,
            1.0f,
            1.0f
        );

        drawCenteredText(
            (left + right) / 2.0f,
            210.0f,
            "SENSOR DATA IS STALE",
            GLUT_BITMAP_HELVETICA_18
        );

        drawCenteredText(
            (left + right) / 2.0f,
            173.0f,
            "CHECK ESP32 CONNECTION",
            GLUT_BITMAP_HELVETICA_18
        );

        return;
    }

    const TemperatureState state =
        getTemperatureState(
            temperature
        );

    if (state == TemperatureState::Cold)
    {
        setStateColor(state);

        drawFilledRectangle(
            left,
            bottom,
            right,
            top
        );

        glColor3f(
            1.0f,
            1.0f,
            1.0f
        );

        drawCenteredText(
            (left + right) / 2.0f,
            210.0f,
            "STATUS: COLD",
            GLUT_BITMAP_HELVETICA_18
        );

        drawCenteredText(
            (left + right) / 2.0f,
            173.0f,
            "TEMPERATURE IS TOO LOW",
            GLUT_BITMAP_HELVETICA_18
        );

        return;
    }

    if (state == TemperatureState::Normal)
    {
        setStateColor(state);

        drawFilledRectangle(
            left,
            bottom,
            right,
            top
        );

        glColor3f(
            1.0f,
            1.0f,
            1.0f
        );

        drawCenteredText(
            (left + right) / 2.0f,
            198.0f,
            "STATUS: NORMAL",
            GLUT_BITMAP_HELVETICA_18
        );

        return;
    }

    if (alertVisible)
    {
        glColor3f(
            0.90f,
            0.05f,
            0.05f
        );
    }
    else
    {
        glColor3f(
            0.35f,
            0.02f,
            0.02f
        );
    }

    drawFilledRectangle(
        left,
        bottom,
        right,
        top
    );

    glColor3f(
        1.0f,
        1.0f,
        1.0f
    );

    drawCenteredText(
        (left + right) / 2.0f,
        210.0f,
        "HIGH TEMPERATURE ALERT",
        GLUT_BITMAP_HELVETICA_18
    );

    drawCenteredText(
        (left + right) / 2.0f,
        173.0f,
        "ACTION REQUIRED",
        GLUT_BITMAP_HELVETICA_18
    );
}

// ==================================================
// Connection and CSV indicators
// ==================================================

void drawConnectionIndicators()
{
    char mqttText[80];

    std::snprintf(
        mqttText,
        sizeof(mqttText),
        "MQTT: %s",
        mqttStatusText.c_str()
    );

    if (mqttConnected)
    {
        glColor3f(
            0.20f,
            0.90f,
            0.40f
        );
    }
    else
    {
        glColor3f(
            1.0f,
            0.25f,
            0.20f
        );
    }

    drawCenteredText(
        315.0f,
        615.0f,
        mqttText,
        GLUT_BITMAP_HELVETICA_12
    );

    const bool sensorOnline =
        isSensorOnline();

    char sensorText[80];

    std::snprintf(
        sensorText,
        sizeof(sensorText),
        "SENSOR: %s",
        sensorOnline
        ? "ONLINE"
        : "OFFLINE"
    );

    if (sensorOnline)
    {
        glColor3f(
            0.20f,
            0.90f,
            0.40f
        );
    }
    else
    {
        glColor3f(
            1.0f,
            0.60f,
            0.15f
        );
    }

    drawCenteredText(
        565.0f,
        615.0f,
        sensorText,
        GLUT_BITMAP_HELVETICA_12
    );

    char csvText[100];

    std::snprintf(
        csvText,
        sizeof(csvText),
        "CSV: %s (%lu rows)",
        csvStatusText.c_str(),
        csvRowsWritten
    );

    if (csvLoggingReady)
    {
        glColor3f(
            0.20f,
            0.90f,
            0.40f
        );
    }
    else
    {
        glColor3f(
            1.0f,
            0.25f,
            0.20f
        );
    }

    drawCenteredText(
        815.0f,
        615.0f,
        csvText,
        GLUT_BITMAP_HELVETICA_12
    );
}

// ==================================================
// Main dashboard display
// ==================================================

void display()
{
    glClear(
        GL_COLOR_BUFFER_BIT
    );

    glMatrixMode(
        GL_MODELVIEW
    );

    glLoadIdentity();

    glColor3f(
        0.90f,
        0.95f,
        1.0f
    );

    drawCenteredText(
        640.0f,
        665.0f,
        "REAL-TIME SENSOR DASHBOARD",
        GLUT_BITMAP_TIMES_ROMAN_24
    );

    drawConnectionIndicators();

    // Main live-sensor panel
    constexpr float livePanelLeft = 165.0f;
    constexpr float livePanelBottom = 405.0f;
    constexpr float livePanelRight = 900.0f;
    constexpr float livePanelTop = 585.0f;

    glColor3f(
        0.10f,
        0.13f,
        0.18f
    );

    drawFilledRectangle(
        livePanelLeft,
        livePanelBottom,
        livePanelRight,
        livePanelTop
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    glLineWidth(1.2f);

    drawRectangleOutline(
        livePanelLeft,
        livePanelBottom,
        livePanelRight,
        livePanelTop
    );

    glColor3f(
        0.70f,
        0.75f,
        0.82f
    );

    drawCenteredText(
        (livePanelLeft + livePanelRight) / 2.0f,
        545.0f,
        "LIVE ESP32 DHT22 SENSOR DATA",
        GLUT_BITMAP_HELVETICA_18
    );

    char temperatureText[64];

    if (hasTemperature)
    {
        std::snprintf(
            temperatureText,
            sizeof(temperatureText),
            "Temperature: %.1f deg C",
            temperature
        );

        setTemperatureColor(
            temperature
        );
    }
    else
    {
        std::snprintf(
            temperatureText,
            sizeof(temperatureText),
            "Temperature: --.- deg C"
        );

        glColor3f(
            0.65f,
            0.70f,
            0.78f
        );
    }

    drawCenteredText(
        (livePanelLeft + livePanelRight) / 2.0f,
        492.0f,
        temperatureText,
        GLUT_BITMAP_TIMES_ROMAN_24
    );

    char humidityText[64];

    if (hasHumidity)
    {
        std::snprintf(
            humidityText,
            sizeof(humidityText),
            "Humidity: %.1f %%",
            humidity
        );
    }
    else
    {
        std::snprintf(
            humidityText,
            sizeof(humidityText),
            "Humidity: --.- %%"
        );
    }

    glColor3f(
        0.25f,
        0.75f,
        1.0f
    );

    drawCenteredText(
        (livePanelLeft + livePanelRight) / 2.0f,
        452.0f,
        humidityText,
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    drawCenteredText(
        (livePanelLeft + livePanelRight) / 2.0f,
        420.0f,
        "Source: ESP32 DHT22 via MQTT",
        GLUT_BITMAP_HELVETICA_18
    );

    drawThermometer(
        temperature
    );

    drawTrendGraph();
    drawStatusPanel();
    drawTemporalHeatMap();
    drawAlertStatisticsPanel();

    glColor3f(
        0.55f,
        0.60f,
        0.68f
    );

    const std::string footer =
        "CSV: sensor_data.csv | Broker: " +
        std::string(getMqttAddress()) +
        " | Cold < 20 C | High >= 35 C";

    drawCenteredText(
        532.0f,
        55.0f,
        footer.c_str(),
        GLUT_BITMAP_HELVETICA_18
    );

    glutSwapBuffers();
}

// ==================================================
// Window resizing
// ==================================================

void reshape(
    int width,
    int height
)
{
    if (height == 0)
    {
        height = 1;
    }

    glViewport(
        0,
        0,
        width,
        height
    );

    glMatrixMode(
        GL_PROJECTION
    );

    glLoadIdentity();

    gluOrtho2D(
        0.0,
        1280.0,
        0.0,
        700.0
    );

    glMatrixMode(
        GL_MODELVIEW
    );

    glLoadIdentity();
}

// ==================================================
// FreeGLUT timer
// ==================================================

void timer(int value)
{
    pollMqtt();

    ++alertCounter;

    if (alertCounter >= 5)
    {
        alertCounter = 0;
        alertVisible =
            !alertVisible;
    }

    glutPostRedisplay();

    glutTimerFunc(
        100,
        timer,
        0
    );
}

// ==================================================
// Keyboard
// ==================================================

void keyboard(
    unsigned char key,
    int x,
    int y
)
{
    if (key == 27)
    {
        glutLeaveMainLoop();
    }
}

// ==================================================
// Program entry
// ==================================================

int main(
    int argc,
    char** argv
)
{
    glutInit(
        &argc,
        argv
    );

    glutSetOption(
        GLUT_ACTION_ON_WINDOW_CLOSE,
        GLUT_ACTION_GLUTMAINLOOP_RETURNS
    );

    glutInitDisplayMode(
        GLUT_DOUBLE |
        GLUT_RGB
    );

    glutInitWindowSize(
        1280,
        680
    );

    glutInitWindowPosition(
        25,
        25
    );

    glutCreateWindow(
        "Real-Time Sensor Data Visualization Platform"
    );

    glClearColor(
        0.035f,
        0.045f,
        0.065f,
        1.0f
    );

    glutDisplayFunc(
        display
    );

    glutReshapeFunc(
        reshape
    );

    glutKeyboardFunc(
        keyboard
    );

    initializeCsvLogging();
    initializeMqtt();

    glutTimerFunc(
        100,
        timer,
        0
    );

    glutMainLoop();

    cleanupMqtt();
    cleanupCsvLogging();

    return EXIT_SUCCESS;
}
