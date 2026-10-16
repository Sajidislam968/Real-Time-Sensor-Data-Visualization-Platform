#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <GL/freeglut.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

// ==================================================
// Experimental configuration
// ==================================================

constexpr float COLD_THRESHOLD = 20.0f;
constexpr float HIGH_THRESHOLD = 35.0f;

constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 700;

constexpr int SCENARIOS_PER_MODE = 6;
constexpr int VISUALIZATION_MODE_COUNT = 4;
constexpr int TOTAL_TRIALS =
    SCENARIOS_PER_MODE * VISUALIZATION_MODE_COUNT;

// ==================================================
// Enumerations
// ==================================================

enum class VisualizationMode
{
    NumericOnly = 0,
    StatusColor = 1,
    TrendGraph = 2,
    FullDashboard = 3
};

enum class TemperatureState
{
    Unselected,
    Cold,
    Normal,
    High
};

enum class TrendDirection
{
    Unselected,
    Rising,
    Stable,
    Falling
};

enum class RecommendedAction
{
    Unselected,
    NoAction,
    Monitor,
    Intervene
};

enum class AppPhase
{
    ParticipantSetup,
    Instructions,
    WaitingToStart,
    Decision,
    Confidence,
    Finished
};

// ==================================================
// Scenario and trial structures
// ==================================================

struct Scenario
{
    int id;
    float temperature;
    float humidity;

    TemperatureState trueState;
    TrendDirection trueTrend;
    RecommendedAction trueAction;
};

struct Trial
{
    int trialNumber;
    int blockNumber;

    VisualizationMode mode;
    Scenario scenario;
};

// ==================================================
// Application state
// ==================================================

AppPhase appPhase =
    AppPhase::ParticipantSetup;

std::string participantId;

std::vector<Trial> trialPlan;
std::size_t currentTrialIndex = 0;

std::vector<float> currentHistory;

TemperatureState selectedState =
    TemperatureState::Unselected;

TrendDirection selectedTrend =
    TrendDirection::Unselected;

RecommendedAction selectedAction =
    RecommendedAction::Unselected;

int selectedConfidence = 0;

long long currentResponseTimeMs = 0;

std::chrono::steady_clock::time_point decisionStartTime;

std::ofstream evaluationFile;
std::string evaluationFilePath;

std::string statusMessage =
    "Enter a participant ID and press ENTER.";

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
        glutBitmapCharacter(
            font,
            *character
        );
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
            getTextWidth(
                text,
                font
            )
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

float clampFloat(
    float value,
    float minimum,
    float maximum
)
{
    if (value < minimum)
    {
        return minimum;
    }

    if (value > maximum)
    {
        return maximum;
    }

    return value;
}

// ==================================================
// Text conversion helpers
// ==================================================

const char* getModeName(
    VisualizationMode mode
)
{
    switch (mode)
    {
        case VisualizationMode::NumericOnly:
            return "A - NUMERIC ONLY";

        case VisualizationMode::StatusColor:
            return "B - STATUS COLOR";

        case VisualizationMode::TrendGraph:
            return "C - TREND GRAPH";

        case VisualizationMode::FullDashboard:
            return "D - FULL DASHBOARD";
    }

    return "UNKNOWN";
}

const char* getStateName(
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
            return "--";
    }
}

const char* getTrendName(
    TrendDirection trend
)
{
    switch (trend)
    {
        case TrendDirection::Rising:
            return "RISING";

        case TrendDirection::Stable:
            return "STABLE";

        case TrendDirection::Falling:
            return "FALLING";

        default:
            return "--";
    }
}

const char* getActionName(
    RecommendedAction action
)
{
    switch (action)
    {
        case RecommendedAction::NoAction:
            return "NO ACTION";

        case RecommendedAction::Monitor:
            return "MONITOR";

        case RecommendedAction::Intervene:
            return "INTERVENE";

        default:
            return "--";
    }
}

// ==================================================
// Color helpers
// ==================================================

void setTemperatureColor(
    float temperature
)
{
    if (temperature < COLD_THRESHOLD)
    {
        glColor3f(
            0.15f,
            0.45f,
            1.0f
        );
    }
    else if (temperature < 25.0f)
    {
        glColor3f(
            0.15f,
            0.85f,
            0.35f
        );
    }
    else if (temperature < 30.0f)
    {
        glColor3f(
            1.0f,
            0.85f,
            0.15f
        );
    }
    else if (temperature < HIGH_THRESHOLD)
    {
        glColor3f(
            1.0f,
            0.45f,
            0.10f
        );
    }
    else
    {
        glColor3f(
            1.0f,
            0.10f,
            0.10f
        );
    }
}

void setStatePanelColor(
    TemperatureState state
)
{
    switch (state)
    {
        case TemperatureState::Cold:
            glColor3f(
                0.10f,
                0.35f,
                0.85f
            );
            break;

        case TemperatureState::Normal:
            glColor3f(
                0.12f,
                0.50f,
                0.25f
            );
            break;

        case TemperatureState::High:
            glColor3f(
                0.85f,
                0.06f,
                0.06f
            );
            break;

        default:
            glColor3f(
                0.30f,
                0.32f,
                0.38f
            );
            break;
    }
}

// ==================================================
// Time and CSV functions
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

bool fileContainsData(
    const std::string& path
)
{
    std::ifstream file(
        path,
        std::ios::binary |
        std::ios::ate
    );

    if (!file.is_open())
    {
        return false;
    }

    return file.tellg() > 0;
}

bool initializeEvaluationFile()
{
    evaluationFilePath = (
        std::filesystem::path(
            getExecutableDirectory()
        ) /
        "evaluation_results.csv"
    ).string();

    const bool alreadyContainsData =
        fileContainsData(
            evaluationFilePath
        );

    evaluationFile.open(
        evaluationFilePath,
        std::ios::out |
        std::ios::app
    );

    if (!evaluationFile.is_open())
    {
        statusMessage =
            "ERROR: Could not open evaluation_results.csv";

        return false;
    }

    if (!alreadyContainsData)
    {
        evaluationFile
            << "participant_id,"
            << "trial_number,"
            << "block_number,"
            << "visualization_mode,"
            << "scenario_id,"
            << "true_state,"
            << "selected_state,"
            << "true_trend,"
            << "selected_trend,"
            << "true_action,"
            << "selected_action,"
            << "state_correct,"
            << "trend_correct,"
            << "action_correct,"
            << "response_time_ms,"
            << "confidence,"
            << "timestamp\n";

        evaluationFile.flush();
    }

    return true;
}

void saveCurrentTrial()
{
    if (
        !evaluationFile.is_open() ||
        currentTrialIndex >=
        trialPlan.size()
    )
    {
        return;
    }

    const Trial& trial =
        trialPlan[currentTrialIndex];

    const int stateCorrect =
        selectedState ==
        trial.scenario.trueState
        ? 1
        : 0;

    const int trendCorrect =
        selectedTrend ==
        trial.scenario.trueTrend
        ? 1
        : 0;

    const int actionCorrect =
        selectedAction ==
        trial.scenario.trueAction
        ? 1
        : 0;

    evaluationFile
        << participantId
        << ','
        << trial.trialNumber
        << ','
        << trial.blockNumber
        << ','
        << getModeName(
            trial.mode
        )
        << ','
        << trial.scenario.id
        << ','
        << getStateName(
            trial.scenario.trueState
        )
        << ','
        << getStateName(
            selectedState
        )
        << ','
        << getTrendName(
            trial.scenario.trueTrend
        )
        << ','
        << getTrendName(
            selectedTrend
        )
        << ','
        << getActionName(
            trial.scenario.trueAction
        )
        << ','
        << getActionName(
            selectedAction
        )
        << ','
        << stateCorrect
        << ','
        << trendCorrect
        << ','
        << actionCorrect
        << ','
        << currentResponseTimeMs
        << ','
        << selectedConfidence
        << ','
        << createTimestamp()
        << '\n';

    evaluationFile.flush();
}

// ==================================================
// Trial-plan generation
// ==================================================

int extractParticipantNumber(
    const std::string& id
)
{
    int number = 0;
    bool foundDigit = false;

    for (char character : id)
    {
        if (
            std::isdigit(
                static_cast<unsigned char>(
                    character
                )
            )
        )
        {
            foundDigit = true;

            number =
                number * 10 +
                (
                    character -
                    '0'
                );
        }
    }

    if (!foundDigit || number <= 0)
    {
        unsigned int hash = 0;

        for (char character : id)
        {
            hash =
                hash * 31u +
                static_cast<unsigned char>(
                    character
                );
        }

        number =
            static_cast<int>(
                hash % 1000u
            ) + 1;
    }

    return number;
}

std::array<Scenario, SCENARIOS_PER_MODE>
createScenarioSet()
{
    return
    {{
        {
            1,
            18.0f,
            68.0f,
            TemperatureState::Cold,
            TrendDirection::Stable,
            RecommendedAction::Intervene
        },
        {
            2,
            24.0f,
            58.0f,
            TemperatureState::Normal,
            TrendDirection::Stable,
            RecommendedAction::NoAction
        },
        {
            3,
            32.0f,
            61.0f,
            TemperatureState::Normal,
            TrendDirection::Rising,
            RecommendedAction::Monitor
        },
        {
            4,
            36.5f,
            55.0f,
            TemperatureState::High,
            TrendDirection::Rising,
            RecommendedAction::Intervene
        },
        {
            5,
            39.0f,
            52.0f,
            TemperatureState::High,
            TrendDirection::Falling,
            RecommendedAction::Intervene
        },
        {
            6,
            19.5f,
            72.0f,
            TemperatureState::Cold,
            TrendDirection::Falling,
            RecommendedAction::Intervene
        }
    }};
}

void buildTrialPlan()
{
    trialPlan.clear();

    const int participantNumber =
        extractParticipantNumber(
            participantId
        );

    std::array<VisualizationMode, VISUALIZATION_MODE_COUNT>
        modeOrder =
    {{
        VisualizationMode::NumericOnly,
        VisualizationMode::StatusColor,
        VisualizationMode::TrendGraph,
        VisualizationMode::FullDashboard
    }};

    const int rotation =
        (
            participantNumber -
            1
        ) %
        VISUALIZATION_MODE_COUNT;

    std::rotate(
        modeOrder.begin(),
        modeOrder.begin() + rotation,
        modeOrder.end()
    );

    const auto scenarioSet =
        createScenarioSet();

    int trialNumber = 1;

    for (
        int blockIndex = 0;
        blockIndex <
        VISUALIZATION_MODE_COUNT;
        ++blockIndex
    )
    {
        std::array<int, SCENARIOS_PER_MODE>
            scenarioIndices =
        {{
            0, 1, 2, 3, 4, 5
        }};

        std::mt19937 randomGenerator(
            static_cast<unsigned int>(
                participantNumber *
                100 +
                blockIndex *
                17
            )
        );

        std::shuffle(
            scenarioIndices.begin(),
            scenarioIndices.end(),
            randomGenerator
        );

        for (
            int scenarioPosition = 0;
            scenarioPosition <
            SCENARIOS_PER_MODE;
            ++scenarioPosition
        )
        {
            const int scenarioIndex =
                scenarioIndices[
                    scenarioPosition
                ];

            Trial trial;

            trial.trialNumber =
                trialNumber;

            trial.blockNumber =
                blockIndex + 1;

            trial.mode =
                modeOrder[
                    blockIndex
                ];

            trial.scenario =
                scenarioSet[
                    scenarioIndex
                ];

            trialPlan.push_back(
                trial
            );

            ++trialNumber;
        }
    }

    currentTrialIndex = 0;
}

// ==================================================
// Scenario-history generation
// ==================================================

std::vector<float> generateHistory(
    const Scenario& scenario
)
{
    constexpr int pointCount = 30;

    std::vector<float> history;
    history.reserve(pointCount);

    if (
        scenario.trueTrend ==
        TrendDirection::Stable
    )
    {
        for (
            int index = 0;
            index < pointCount;
            ++index
        )
        {
            const float smallVariation =
                0.18f *
                static_cast<float>(
                    std::sin(
                        index * 0.65
                    )
                );

            history.push_back(
                scenario.temperature +
                smallVariation
            );
        }
    }
    else if (
        scenario.trueTrend ==
        TrendDirection::Rising
    )
    {
        const float startValue =
            scenario.temperature -
            7.0f;

        for (
            int index = 0;
            index < pointCount;
            ++index
        )
        {
            const float ratio =
                static_cast<float>(
                    index
                ) /
                static_cast<float>(
                    pointCount - 1
                );

            const float variation =
                0.12f *
                static_cast<float>(
                    std::sin(
                        index * 0.55
                    )
                );

            history.push_back(
                startValue +
                ratio *
                (
                    scenario.temperature -
                    startValue
                ) +
                variation
            );
        }
    }
    else
    {
        const float startValue =
            scenario.temperature +
            7.0f;

        for (
            int index = 0;
            index < pointCount;
            ++index
        )
        {
            const float ratio =
                static_cast<float>(
                    index
                ) /
                static_cast<float>(
                    pointCount - 1
                );

            const float variation =
                0.12f *
                static_cast<float>(
                    std::sin(
                        index * 0.55
                    )
                );

            history.push_back(
                startValue +
                ratio *
                (
                    scenario.temperature -
                    startValue
                ) +
                variation
            );
        }
    }

    if (!history.empty())
    {
        history.back() =
            scenario.temperature;
    }

    return history;
}

// ==================================================
// Trial flow
// ==================================================

void resetSelections()
{
    selectedState =
        TemperatureState::Unselected;

    selectedTrend =
        TrendDirection::Unselected;

    selectedAction =
        RecommendedAction::Unselected;

    selectedConfidence = 0;

    currentResponseTimeMs = 0;
}

bool decisionIsComplete()
{
    return
        selectedState !=
            TemperatureState::Unselected &&
        selectedTrend !=
            TrendDirection::Unselected &&
        selectedAction !=
            RecommendedAction::Unselected;
}

void startCurrentTrial()
{
    if (
        currentTrialIndex >=
        trialPlan.size()
    )
    {
        appPhase =
            AppPhase::Finished;

        return;
    }

    resetSelections();

    currentHistory =
        generateHistory(
            trialPlan[
                currentTrialIndex
            ].scenario
        );

    decisionStartTime =
        std::chrono::steady_clock::now();

    appPhase =
        AppPhase::Decision;

    statusMessage =
        "Answer all three questions, then press ENTER.";
}

void completeDecision()
{
    if (!decisionIsComplete())
    {
        statusMessage =
            "Select state, trend, and action before pressing ENTER.";

        return;
    }

    const auto decisionEndTime =
        std::chrono::steady_clock::now();

    currentResponseTimeMs =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(
            decisionEndTime -
            decisionStartTime
        ).count();

    appPhase =
        AppPhase::Confidence;

    statusMessage =
        "Select confidence from 1 to 5, then press ENTER.";
}

void completeConfidenceAndSave()
{
    if (
        selectedConfidence < 1 ||
        selectedConfidence > 5
    )
    {
        statusMessage =
            "Select a confidence value from 1 to 5.";

        return;
    }

    saveCurrentTrial();

    ++currentTrialIndex;

    if (
        currentTrialIndex >=
        trialPlan.size()
    )
    {
        appPhase =
            AppPhase::Finished;

        statusMessage =
            "Evaluation complete. Results were saved.";
    }
    else
    {
        appPhase =
            AppPhase::WaitingToStart;

        statusMessage =
            "Trial saved. Press SPACE for the next trial.";
    }
}

// ==================================================
// Visualization components
// ==================================================

void drawTemperatureCard(
    float left,
    float bottom,
    float right,
    float top,
    const Scenario& scenario
)
{
    glColor3f(
        0.10f,
        0.13f,
        0.18f
    );

    drawFilledRectangle(
        left,
        bottom,
        right,
        top
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    glLineWidth(1.5f);

    drawRectangleOutline(
        left,
        bottom,
        right,
        top
    );

    // Calculate positions from the card dimensions so the title,
    // temperature and humidity never overlap in compact layouts.
    const float centerX =
        (left + right) / 2.0f;

    const float centerY =
        (bottom + top) / 2.0f;

    const float titleY =
        top - 38.0f;

    const float temperatureY =
        centerY + 5.0f;

    const float humidityY =
        centerY - 40.0f;

    glColor3f(
        0.82f,
        0.86f,
        0.92f
    );

    drawCenteredText(
        centerX,
        titleY,
        "CURRENT SENSOR VALUES",
        GLUT_BITMAP_HELVETICA_18
    );

    char temperatureText[64];

    std::snprintf(
        temperatureText,
        sizeof(temperatureText),
        "%.1f deg C",
        scenario.temperature
    );

    setTemperatureColor(
        scenario.temperature
    );

    drawCenteredText(
        centerX,
        temperatureY,
        temperatureText,
        GLUT_BITMAP_TIMES_ROMAN_24
    );

    char humidityText[64];

    std::snprintf(
        humidityText,
        sizeof(humidityText),
        "Humidity: %.1f %%",
        scenario.humidity
    );

    glColor3f(
        0.25f,
        0.75f,
        1.0f
    );

    drawCenteredText(
        centerX,
        humidityY,
        humidityText,
        GLUT_BITMAP_HELVETICA_18
    );
}

void drawStatusBlock(
    float left,
    float bottom,
    float right,
    float top,
    TemperatureState state
)
{
    setStatePanelColor(
        state
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

    char stateText[64];

    std::snprintf(
        stateText,
        sizeof(stateText),
        "STATUS: %s",
        getStateName(
            state
        )
    );

    drawCenteredText(
        (left + right) / 2.0f,
        bottom +
        (
            top - bottom
        ) / 2.0f -
        5.0f,
        stateText,
        GLUT_BITMAP_HELVETICA_18
    );
}

void drawTrendGraphComponent(
    float left,
    float bottom,
    float right,
    float top
)
{
    constexpr float graphMinimum = 15.0f;
    constexpr float graphMaximum = 45.0f;

    glColor3f(
        0.10f,
        0.13f,
        0.18f
    );

    drawFilledRectangle(
        left,
        bottom,
        right,
        top
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    glLineWidth(1.5f);

    drawRectangleOutline(
        left,
        bottom,
        right,
        top
    );

    glColor3f(
        0.75f,
        0.78f,
        0.84f
    );

    drawText(
        left + 15.0f,
        top - 23.0f,
        "TEMPERATURE TREND",
        GLUT_BITMAP_HELVETICA_12
    );

    const float coldY =
        bottom +
        (
            COLD_THRESHOLD -
            graphMinimum
        ) /
        (
            graphMaximum -
            graphMinimum
        ) *
        (
            top -
            bottom
        );

    glColor3f(
        0.15f,
        0.45f,
        1.0f
    );

    glLineWidth(1.0f);

    drawHorizontalLine(
        left,
        right,
        coldY
    );

    drawText(
        right - 66.0f,
        coldY + 4.0f,
        "20 C",
        GLUT_BITMAP_HELVETICA_10
    );

    const float highY =
        bottom +
        (
            HIGH_THRESHOLD -
            graphMinimum
        ) /
        (
            graphMaximum -
            graphMinimum
        ) *
        (
            top -
            bottom
        );

    glColor3f(
        0.80f,
        0.15f,
        0.15f
    );

    drawHorizontalLine(
        left,
        right,
        highY
    );

    drawText(
        right - 66.0f,
        highY + 4.0f,
        "35 C",
        GLUT_BITMAP_HELVETICA_10
    );

    if (
        currentHistory.size() <
        2
    )
    {
        return;
    }

    glColor3f(
        0.15f,
        0.85f,
        1.0f
    );

    glLineWidth(3.0f);

    glBegin(GL_LINE_STRIP);

    for (
        std::size_t index = 0;
        index <
        currentHistory.size();
        ++index
    )
    {
        const float xRatio =
            static_cast<float>(
                index
            ) /
            static_cast<float>(
                currentHistory.size() -
                1
            );

        const float x =
            left +
            xRatio *
            (
                right -
                left
            );

        const float value =
            clampFloat(
                currentHistory[
                    index
                ],
                graphMinimum,
                graphMaximum
            );

        const float yRatio =
            (
                value -
                graphMinimum
            ) /
            (
                graphMaximum -
                graphMinimum
            );

        const float y =
            bottom +
            yRatio *
            (
                top -
                bottom
            );

        glVertex2f(
            x,
            y
        );
    }

    glEnd();
}

void drawThermometerComponent(
    float left,
    float bottom,
    float right,
    float top,
    float value
)
{
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

    const float clampedValue =
        clampFloat(
            value,
            0.0f,
            50.0f
        );

    const float fillTop =
        bottom +
        (
            clampedValue /
            50.0f
        ) *
        (
            top -
            bottom
        );

    setTemperatureColor(
        value
    );

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

    drawText(
        right + 15.0f,
        bottom - 3.0f,
        "0",
        GLUT_BITMAP_HELVETICA_18
    );

    drawText(
        right + 15.0f,
        bottom +
        (
            top - bottom
        ) / 2.0f -
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

void drawHeatMapComponent(
    float left,
    float bottom,
    float right,
    float top
)
{
    glColor3f(
        0.10f,
        0.13f,
        0.18f
    );

    drawFilledRectangle(
        left,
        bottom,
        right,
        top
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    glLineWidth(1.5f);

    drawRectangleOutline(
        left,
        bottom,
        right,
        top
    );

    glColor3f(
        0.82f,
        0.86f,
        0.92f
    );

    drawCenteredText(
        (left + right) / 2.0f,
        top - 25.0f,
        "RECENT TEMPERATURE MAP",
        GLUT_BITMAP_HELVETICA_12
    );

    constexpr int columns = 10;
    constexpr int rows = 3;

    const float gridLeft =
        left + 22.0f;

    const float gridTop =
        top - 48.0f;

    const float cellGap = 3.0f;

    const float cellWidth =
        (
            right -
            left -
            44.0f -
            (
                columns -
                1
            ) *
            cellGap
        ) /
        columns;

    const float cellHeight =
        (
            top -
            bottom -
            75.0f -
            (
                rows -
                1
            ) *
            cellGap
        ) /
        rows;

    for (
        int index = 0;
        index <
        columns * rows;
        ++index
    )
    {
        const int row =
            index /
            columns;

        const int column =
            index %
            columns;

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

        if (
            index <
            static_cast<int>(
                currentHistory.size()
            )
        )
        {
            setTemperatureColor(
                currentHistory[
                    index
                ]
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
    }
}

// ==================================================
// Visualization-mode displays
// ==================================================

void drawNumericOnlyMode(
    const Scenario& scenario
)
{
    drawTemperatureCard(
        180.0f,
        205.0f,
        890.0f,
        540.0f,
        scenario
    );
}

void drawStatusColorMode(
    const Scenario& scenario
)
{
    drawTemperatureCard(
        180.0f,
        330.0f,
        890.0f,
        560.0f,
        scenario
    );

    drawStatusBlock(
        260.0f,
        170.0f,
        810.0f,
        285.0f,
        scenario.trueState
    );
}

void drawTrendMode(
    const Scenario& scenario
)
{
    drawTemperatureCard(
        180.0f,
        410.0f,
        890.0f,
        580.0f,
        scenario
    );

    drawStatusBlock(
        180.0f,
        315.0f,
        890.0f,
        385.0f,
        scenario.trueState
    );

    drawTrendGraphComponent(
        180.0f,
        125.0f,
        890.0f,
        285.0f
    );
}

void drawFullDashboardMode(
    const Scenario& scenario
)
{
    drawTemperatureCard(
        250.0f,
        430.0f,
        890.0f,
        580.0f,
        scenario
    );

    drawThermometerComponent(
        105.0f,
        140.0f,
        170.0f,
        545.0f,
        scenario.temperature
    );

    drawTrendGraphComponent(
        250.0f,
        250.0f,
        890.0f,
        405.0f
    );

    drawStatusBlock(
        250.0f,
        130.0f,
        600.0f,
        220.0f,
        scenario.trueState
    );

    drawHeatMapComponent(
        620.0f,
        130.0f,
        890.0f,
        220.0f
    );
}

// ==================================================
// Response panel
// ==================================================

void drawSelectionLine(
    float x,
    float y,
    const char* label,
    const char* value,
    bool selected
)
{
    glColor3f(
        0.70f,
        0.74f,
        0.82f
    );

    drawText(
        x,
        y,
        label,
        GLUT_BITMAP_HELVETICA_12
    );

    if (selected)
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
            0.55f,
            0.58f,
            0.65f
        );
    }

    drawText(
        x + 92.0f,
        y,
        value,
        GLUT_BITMAP_HELVETICA_12
    );
}

void drawResponsePanel()
{
    constexpr float left = 930.0f;
    constexpr float bottom = 70.0f;
    constexpr float right = 1245.0f;
    constexpr float top = 580.0f;

    glColor3f(
        0.10f,
        0.13f,
        0.18f
    );

    drawFilledRectangle(
        left,
        bottom,
        right,
        top
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    glLineWidth(1.5f);

    drawRectangleOutline(
        left,
        bottom,
        right,
        top
    );

    glColor3f(
        0.90f,
        0.95f,
        1.0f
    );

    drawCenteredText(
        (left + right) / 2.0f,
        545.0f,
        "YOUR DECISION",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.32f,
        0.36f,
        0.43f
    );

    drawHorizontalLine(
        left + 20.0f,
        right - 20.0f,
        525.0f
    );

    glColor3f(
        0.80f,
        0.83f,
        0.90f
    );

    drawText(
        955.0f,
        495.0f,
        "1. Current state",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    drawText(
        970.0f,
        465.0f,
        "C = COLD",
        GLUT_BITMAP_HELVETICA_12
    );

    drawText(
        970.0f,
        442.0f,
        "N = NORMAL",
        GLUT_BITMAP_HELVETICA_12
    );

    drawText(
        970.0f,
        419.0f,
        "H = HIGH",
        GLUT_BITMAP_HELVETICA_12
    );

    glColor3f(
        0.80f,
        0.83f,
        0.90f
    );

    drawText(
        955.0f,
        380.0f,
        "2. Trend",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    drawText(
        970.0f,
        350.0f,
        "R = RISING",
        GLUT_BITMAP_HELVETICA_12
    );

    drawText(
        970.0f,
        327.0f,
        "S = STABLE",
        GLUT_BITMAP_HELVETICA_12
    );

    drawText(
        970.0f,
        304.0f,
        "F = FALLING",
        GLUT_BITMAP_HELVETICA_12
    );

    glColor3f(
        0.80f,
        0.83f,
        0.90f
    );

    drawText(
        955.0f,
        265.0f,
        "3. Action",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    drawText(
        970.0f,
        235.0f,
        "0 = NO ACTION",
        GLUT_BITMAP_HELVETICA_12
    );

    drawText(
        970.0f,
        212.0f,
        "M = MONITOR",
        GLUT_BITMAP_HELVETICA_12
    );

    drawText(
        970.0f,
        189.0f,
        "I = INTERVENE",
        GLUT_BITMAP_HELVETICA_12
    );

    // Dedicated summary area prevents the selected answers from
    // touching or falling below the panel border.
    glColor3f(
        0.075f,
        0.090f,
        0.120f
    );

    drawFilledRectangle(
        left + 14.0f,
        bottom + 10.0f,
        right - 14.0f,
        164.0f
    );

    glColor3f(
        0.32f,
        0.36f,
        0.43f
    );

    drawHorizontalLine(
        left + 20.0f,
        right - 20.0f,
        172.0f
    );

    drawSelectionLine(
        955.0f,
        143.0f,
        "State:",
        getStateName(
            selectedState
        ),
        selectedState !=
            TemperatureState::Unselected
    );

    drawSelectionLine(
        955.0f,
        119.0f,
        "Trend:",
        getTrendName(
            selectedTrend
        ),
        selectedTrend !=
            TrendDirection::Unselected
    );

    drawSelectionLine(
        955.0f,
        95.0f,
        "Action:",
        getActionName(
            selectedAction
        ),
        selectedAction !=
            RecommendedAction::Unselected
    );
}

// ==================================================
// Screens
// ==================================================

void drawParticipantSetupScreen()
{
    glColor3f(
        0.90f,
        0.95f,
        1.0f
    );

    drawCenteredText(
        640.0f,
        610.0f,
        "SENSOR VISUALIZATION EVALUATION",
        GLUT_BITMAP_TIMES_ROMAN_24
    );

    glColor3f(
        0.10f,
        0.13f,
        0.18f
    );

    drawFilledRectangle(
        300.0f,
        220.0f,
        980.0f,
        520.0f
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    glLineWidth(1.5f);

    drawRectangleOutline(
        300.0f,
        220.0f,
        980.0f,
        520.0f
    );

    glColor3f(
        0.82f,
        0.86f,
        0.92f
    );

    drawCenteredText(
        640.0f,
        465.0f,
        "Enter an anonymous participant ID",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.20f,
        0.23f,
        0.29f
    );

    drawFilledRectangle(
        430.0f,
        340.0f,
        850.0f,
        405.0f
    );

    glColor3f(
        0.80f,
        0.84f,
        0.92f
    );

    glLineWidth(2.0f);

    drawRectangleOutline(
        430.0f,
        340.0f,
        850.0f,
        405.0f
    );

    const std::string displayId =
        participantId.empty()
        ? "Example: P01"
        : participantId;

    if (participantId.empty())
    {
        glColor3f(
            0.45f,
            0.48f,
            0.55f
        );
    }
    else
    {
        glColor3f(
            0.20f,
            0.90f,
            0.40f
        );
    }

    drawCenteredText(
        640.0f,
        365.0f,
        displayId.c_str(),
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    drawCenteredText(
        640.0f,
        285.0f,
        "Use letters and numbers only. Press ENTER to continue.",
        GLUT_BITMAP_HELVETICA_12
    );
}

void drawInstructionsScreen()
{
    glColor3f(
        0.90f,
        0.95f,
        1.0f
    );

    drawCenteredText(
        640.0f,
        630.0f,
        "EVALUATION INSTRUCTIONS",
        GLUT_BITMAP_TIMES_ROMAN_24
    );

    glColor3f(
        0.10f,
        0.13f,
        0.18f
    );

    drawFilledRectangle(
        180.0f,
        120.0f,
        1100.0f,
        570.0f
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    glLineWidth(1.5f);

    drawRectangleOutline(
        180.0f,
        120.0f,
        1100.0f,
        570.0f
    );

    glColor3f(
        0.82f,
        0.86f,
        0.92f
    );

    drawText(
        225.0f,
        520.0f,
        "You will complete 24 trials using four visualization methods.",
        GLUT_BITMAP_HELVETICA_18
    );

    drawText(
        225.0f,
        480.0f,
        "For every trial, identify the state, trend, and recommended action.",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.25f,
        0.75f,
        1.0f
    );

    drawText(
        225.0f,
        420.0f,
        "Temperature rules",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.72f,
        0.76f,
        0.84f
    );

    drawText(
        250.0f,
        385.0f,
        "COLD: below 20 C",
        GLUT_BITMAP_HELVETICA_18
    );

    drawText(
        250.0f,
        350.0f,
        "NORMAL: 20 C to 34.9 C",
        GLUT_BITMAP_HELVETICA_18
    );

    drawText(
        250.0f,
        315.0f,
        "HIGH: 35 C and above",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        1.0f,
        0.65f,
        0.25f
    );

    drawText(
        650.0f,
        420.0f,
        "Action rules",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.72f,
        0.76f,
        0.84f
    );

    drawText(
        675.0f,
        385.0f,
        "COLD or HIGH: INTERVENE",
        GLUT_BITMAP_HELVETICA_18
    );

    drawText(
        675.0f,
        350.0f,
        "NORMAL and rising near 35 C: MONITOR",
        GLUT_BITMAP_HELVETICA_18
    );

    drawText(
        675.0f,
        315.0f,
        "Other NORMAL cases: NO ACTION",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.20f,
        0.90f,
        0.40f
    );

    drawCenteredText(
        640.0f,
        210.0f,
        "Press SPACE to begin the first trial.",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.55f,
        0.60f,
        0.68f
    );

    drawCenteredText(
        640.0f,
        160.0f,
        "No correctness feedback will be shown during the experiment.",
        GLUT_BITMAP_HELVETICA_12
    );
}

void drawWaitingScreen()
{
    const Trial& nextTrial =
        trialPlan[
            currentTrialIndex
        ];

    glColor3f(
        0.90f,
        0.95f,
        1.0f
    );

    drawCenteredText(
        640.0f,
        620.0f,
        "READY FOR NEXT TRIAL",
        GLUT_BITMAP_TIMES_ROMAN_24
    );

    glColor3f(
        0.10f,
        0.13f,
        0.18f
    );

    drawFilledRectangle(
        320.0f,
        220.0f,
        960.0f,
        520.0f
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    drawRectangleOutline(
        320.0f,
        220.0f,
        960.0f,
        520.0f
    );

    char progressText[100];

    std::snprintf(
        progressText,
        sizeof(progressText),
        "Trial %u of %d",
        static_cast<unsigned int>(
            currentTrialIndex + 1
        ),
        TOTAL_TRIALS
    );

    glColor3f(
        0.82f,
        0.86f,
        0.92f
    );

    drawCenteredText(
        640.0f,
        450.0f,
        progressText,
        GLUT_BITMAP_HELVETICA_18
    );

    char modeText[120];

    std::snprintf(
        modeText,
        sizeof(modeText),
        "Next visualization: %s",
        getModeName(
            nextTrial.mode
        )
    );

    glColor3f(
        0.25f,
        0.75f,
        1.0f
    );

    drawCenteredText(
        640.0f,
        385.0f,
        modeText,
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.20f,
        0.90f,
        0.40f
    );

    drawCenteredText(
        640.0f,
        300.0f,
        "Press SPACE when the participant is ready.",
        GLUT_BITMAP_HELVETICA_18
    );
}

void drawTrialScreen()
{
    if (
        currentTrialIndex >=
        trialPlan.size()
    )
    {
        return;
    }

    const Trial& trial =
        trialPlan[
            currentTrialIndex
        ];

    glColor3f(
        0.90f,
        0.95f,
        1.0f
    );

    drawCenteredText(
        640.0f,
        655.0f,
        "SENSOR VISUALIZATION DECISION TASK",
        GLUT_BITMAP_TIMES_ROMAN_24
    );

    char progressText[140];

    std::snprintf(
        progressText,
        sizeof(progressText),
        "Participant: %s | Trial %d of %d | %s",
        participantId.c_str(),
        trial.trialNumber,
        TOTAL_TRIALS,
        getModeName(
            trial.mode
        )
    );

    glColor3f(
        0.55f,
        0.60f,
        0.68f
    );

    drawCenteredText(
        640.0f,
        620.0f,
        progressText,
        GLUT_BITMAP_HELVETICA_12
    );

    switch (trial.mode)
    {
        case VisualizationMode::NumericOnly:
            drawNumericOnlyMode(
                trial.scenario
            );
            break;

        case VisualizationMode::StatusColor:
            drawStatusColorMode(
                trial.scenario
            );
            break;

        case VisualizationMode::TrendGraph:
            drawTrendMode(
                trial.scenario
            );
            break;

        case VisualizationMode::FullDashboard:
            drawFullDashboardMode(
                trial.scenario
            );
            break;
    }

    drawResponsePanel();

    glColor3f(
        0.75f,
        0.78f,
        0.84f
    );

    drawCenteredText(
        640.0f,
        48.0f,
        statusMessage.c_str(),
        GLUT_BITMAP_HELVETICA_12
    );

    if (
        appPhase ==
        AppPhase::Confidence
    )
    {
        glColor3f(
            0.05f,
            0.06f,
            0.09f
        );

        drawFilledRectangle(
            330.0f,
            215.0f,
            880.0f,
            455.0f
        );

        glColor3f(
            0.75f,
            0.80f,
            0.90f
        );

        glLineWidth(2.0f);

        drawRectangleOutline(
            330.0f,
            215.0f,
            880.0f,
            455.0f
        );

        glColor3f(
            0.90f,
            0.95f,
            1.0f
        );

        drawCenteredText(
            605.0f,
            400.0f,
            "HOW CONFIDENT ARE YOU?",
            GLUT_BITMAP_HELVETICA_18
        );

        glColor3f(
            0.68f,
            0.72f,
            0.80f
        );

        drawCenteredText(
            605.0f,
            350.0f,
            "1 = Not confident     5 = Very confident",
            GLUT_BITMAP_HELVETICA_18
        );

        char confidenceText[64];

        const std::string confidenceValue =
            selectedConfidence == 0
            ? "--"
            : std::to_string(
                selectedConfidence
            );

        std::snprintf(
            confidenceText,
            sizeof(confidenceText),
            "Selected: %s",
            confidenceValue.c_str()
        );

        if (selectedConfidence > 0)
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
                0.55f,
                0.58f,
                0.65f
            );
        }

        drawCenteredText(
            605.0f,
            300.0f,
            confidenceText,
            GLUT_BITMAP_HELVETICA_18
        );

        glColor3f(
            0.75f,
            0.78f,
            0.84f
        );

        drawCenteredText(
            605.0f,
            250.0f,
            "Press ENTER to save this trial.",
            GLUT_BITMAP_HELVETICA_12
        );
    }
}

void drawFinishedScreen()
{
    glColor3f(
        0.90f,
        0.95f,
        1.0f
    );

    drawCenteredText(
        640.0f,
        610.0f,
        "EVALUATION COMPLETE",
        GLUT_BITMAP_TIMES_ROMAN_24
    );

    glColor3f(
        0.10f,
        0.13f,
        0.18f
    );

    drawFilledRectangle(
        260.0f,
        210.0f,
        1020.0f,
        520.0f
    );

    glColor3f(
        0.65f,
        0.70f,
        0.78f
    );

    drawRectangleOutline(
        260.0f,
        210.0f,
        1020.0f,
        520.0f
    );

    glColor3f(
        0.20f,
        0.90f,
        0.40f
    );

    drawCenteredText(
        640.0f,
        445.0f,
        "All 24 trials were saved successfully.",
        GLUT_BITMAP_HELVETICA_18
    );

    glColor3f(
        0.72f,
        0.76f,
        0.84f
    );

    drawCenteredText(
        640.0f,
        380.0f,
        "Results file:",
        GLUT_BITMAP_HELVETICA_18
    );

    drawCenteredText(
        640.0f,
        340.0f,
        evaluationFilePath.c_str(),
        GLUT_BITMAP_HELVETICA_12
    );

    drawCenteredText(
        640.0f,
        275.0f,
        "Press ESC to close the program.",
        GLUT_BITMAP_HELVETICA_18
    );
}

// ==================================================
// Main display
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

    switch (appPhase)
    {
        case AppPhase::ParticipantSetup:
            drawParticipantSetupScreen();
            break;

        case AppPhase::Instructions:
            drawInstructionsScreen();
            break;

        case AppPhase::WaitingToStart:
            drawWaitingScreen();
            break;

        case AppPhase::Decision:
        case AppPhase::Confidence:
            drawTrialScreen();
            break;

        case AppPhase::Finished:
            drawFinishedScreen();
            break;
    }

    glutSwapBuffers();
}

// ==================================================
// Keyboard input
// ==================================================

void handleParticipantSetupInput(
    unsigned char key
)
{
    if (key == 13)
    {
        if (participantId.empty())
        {
            statusMessage =
                "Participant ID cannot be empty.";

            return;
        }

        if (!initializeEvaluationFile())
        {
            return;
        }

        buildTrialPlan();

        appPhase =
            AppPhase::Instructions;

        statusMessage =
            "Press SPACE to begin.";

        return;
    }

    if (key == 8)
    {
        if (!participantId.empty())
        {
            participantId.pop_back();
        }

        return;
    }

    if (
        participantId.size() >= 12
    )
    {
        return;
    }

    if (
        std::isalnum(
            static_cast<unsigned char>(
                key
            )
        ) ||
        key == '-' ||
        key == '_'
    )
    {
        participantId.push_back(
            static_cast<char>(
                std::toupper(
                    static_cast<unsigned char>(
                        key
                    )
                )
            )
        );
    }
}

void handleDecisionInput(
    unsigned char key
)
{
    const unsigned char upperKey =
        static_cast<unsigned char>(
            std::toupper(
                static_cast<unsigned char>(
                    key
                )
            )
        );

    if (upperKey == 'C')
    {
        selectedState =
            TemperatureState::Cold;
    }
    else if (upperKey == 'N')
    {
        selectedState =
            TemperatureState::Normal;
    }
    else if (upperKey == 'H')
    {
        selectedState =
            TemperatureState::High;
    }
    else if (upperKey == 'R')
    {
        selectedTrend =
            TrendDirection::Rising;
    }
    else if (upperKey == 'S')
    {
        selectedTrend =
            TrendDirection::Stable;
    }
    else if (upperKey == 'F')
    {
        selectedTrend =
            TrendDirection::Falling;
    }
    else if (upperKey == '0')
    {
        selectedAction =
            RecommendedAction::NoAction;
    }
    else if (upperKey == 'M')
    {
        selectedAction =
            RecommendedAction::Monitor;
    }
    else if (upperKey == 'I')
    {
        selectedAction =
            RecommendedAction::Intervene;
    }
    else if (key == 13)
    {
        completeDecision();
    }
}

void handleConfidenceInput(
    unsigned char key
)
{
    if (
        key >= '1' &&
        key <= '5'
    )
    {
        selectedConfidence =
            key - '0';

        return;
    }

    if (key == 13)
    {
        completeConfidenceAndSave();
    }
}

void keyboard(
    unsigned char key,
    int x,
    int y
)
{
    if (key == 27)
    {
        glutLeaveMainLoop();
        return;
    }

    switch (appPhase)
    {
        case AppPhase::ParticipantSetup:
            handleParticipantSetupInput(
                key
            );
            break;

        case AppPhase::Instructions:
            if (key == ' ')
            {
                appPhase =
                    AppPhase::WaitingToStart;

                statusMessage =
                    "Press SPACE when ready.";
            }
            break;

        case AppPhase::WaitingToStart:
            if (key == ' ')
            {
                startCurrentTrial();
            }
            break;

        case AppPhase::Decision:
            handleDecisionInput(
                key
            );
            break;

        case AppPhase::Confidence:
            handleConfidenceInput(
                key
            );
            break;

        case AppPhase::Finished:
            break;
    }

    glutPostRedisplay();
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
        static_cast<double>(
            WINDOW_WIDTH
        ),
        0.0,
        static_cast<double>(
            WINDOW_HEIGHT
        )
    );

    glMatrixMode(
        GL_MODELVIEW
    );

    glLoadIdentity();
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
        WINDOW_WIDTH,
        WINDOW_HEIGHT
    );

    glutInitWindowPosition(
        25,
        25
    );

    glutCreateWindow(
        "Sensor Visualization Research Evaluation"
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

    glutMainLoop();

    if (evaluationFile.is_open())
    {
        evaluationFile.flush();
        evaluationFile.close();
    }

    return EXIT_SUCCESS;
}
