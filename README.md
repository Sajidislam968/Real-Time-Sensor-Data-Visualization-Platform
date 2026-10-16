# Real-Time Sensor Data Visualization Platform

This project contains two desktop applications built with C++, OpenGL, and
FreeGLUT:

- **SensorDashboard** subscribes to MQTT sensor messages and displays live
  temperature/humidity, temperature state, trend history, a heat map, alerts,
  statistics, and CSV logging. It requires a running MQTT broker and a sensor
  publisher such as the ESP32/DHT22 firmware described in the project notes.
- **SensorVisualizationEvaluation** runs a controlled study comparing Numeric
  only, Status color, Trend graph, and Full dashboard displays. It records
  participants' choices, correctness, response times, and confidence to
  `evaluation_results.csv`.

The project report describes the sensor-to-dashboard pipeline as DHT22 →
ESP32 → Wi-Fi/MQTT → Mosquitto → Eclipse Paho C → OpenGL. Its evaluation
reports 21 participants and 504 trials; trend accuracy was 50.0%/53.2% for
the two modes without a graph and 92.1%/95.2% for the two graph modes. These
temperature thresholds and interpretations are project rules, not safety
standards.

## Build on Linux

On Debian/Ubuntu:

```sh
sudo apt update
sudo apt install build-essential cmake freeglut3-dev libglu1-mesa-dev libpaho-mqtt-dev mosquitto mosquitto-clients
cmake -S . -B build
cmake --build build -j
```

Run either application from the project root:

```sh
./build/bin/SensorVisualizationEvaluation
./build/bin/SensorDashboard
```

The applications need a graphical desktop session with OpenGL support. The
CSV output is written beside the executable.
Generated CSV files are ignored by Git; the evaluation output contains
participant-level responses and should only be shared with appropriate consent.

## Run the live dashboard

Install and start Mosquitto, connect the ESP32 and the Linux computer to the
same network, and configure the ESP32 publisher to send these topics:

| Topic | Payload |
| --- | --- |
| `sensors/room1/temperature` | Temperature in °C |
| `sensors/room1/humidity` | Relative humidity in % |
| `sensors/room1/status` | Node status |

The dashboard defaults to `tcp://127.0.0.1:1883`. Set
`SENSOR_MQTT_ADDRESS` to the broker address when the broker is on another
machine. For example:

```sh
SENSOR_MQTT_ADDRESS=tcp://192.168.1.20:1883 ./build/bin/SensorDashboard
```

Replace the example address with the broker's actual LAN address. Configure
the broker and its authentication/access controls for your network; do not
expose an anonymous MQTT listener to the public internet.

## Project files

- `SensorDashboard/main.cpp` — live MQTT/OpenGL dashboard.
- `SensorVisualizationEvaluation/main.cpp` — four-mode user-study application.
- `CMakeLists.txt` — Linux-friendly build for both applications.

The Code::Blocks project files in the original Windows project use
machine-specific Windows library paths. They are not used by the CMake build.
