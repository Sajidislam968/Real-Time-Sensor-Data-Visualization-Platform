cmake_minimum_required(VERSION 3.16)

project(RealTimeSensorVisualization LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(GLUT REQUIRED)

find_path(PAHO_MQTT_INCLUDE_DIR MQTTClient.h)
find_library(PAHO_MQTT_C_LIBRARY NAMES paho-mqtt3c)

if(NOT PAHO_MQTT_INCLUDE_DIR OR NOT PAHO_MQTT_C_LIBRARY)
    message(FATAL_ERROR
        "Eclipse Paho MQTT C is required. Install the development package "
        "(for example, libpaho-mqtt-dev on Debian/Ubuntu)."
    )
endif()

add_executable(SensorDashboard
    SensorDashboard/main.cpp
)

target_include_directories(SensorDashboard PRIVATE
    "${PAHO_MQTT_INCLUDE_DIR}"
)

target_link_libraries(SensorDashboard PRIVATE
    OpenGL::GL
    OpenGL::GLU
    GLUT::GLUT
    "${PAHO_MQTT_C_LIBRARY}"
)

add_executable(SensorVisualizationEvaluation
    SensorVisualizationEvaluation/main.cpp
)

target_link_libraries(SensorVisualizationEvaluation PRIVATE
    OpenGL::GL
    OpenGL::GLU
    GLUT::GLUT
)

set_target_properties(
    SensorDashboard
    SensorVisualizationEvaluation
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
