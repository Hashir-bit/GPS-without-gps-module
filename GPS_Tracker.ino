#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <math.h>

// ================================================================
// WIFI SETTINGS
// ================================================================

const char* ssid = "iPhone";
const char* password = "atkaresam123";

const char* GOOGLE_API_KEY = "AIzaSyB9WQW_b6lyWzUGraREahHUOR12Cdmvui8";

// Dashboard Server Telemetry Endpoint
// IMPORTANT: Replace "YOUR_LAPTOP_IP" below with the IP address of the laptop running dashboard_server.py!
const char* DASHBOARD_SERVER_URL = "http://YOUR_LAPTOP_IP:8000/api/telemetry";

// ================================================================
// MPU6050
// ================================================================

#define MPU_ADDR 0x68

#define REG_ACCEL_XOUT_H 0x3B
#define REG_PWR_MGMT_1   0x6B
#define REG_GYRO_CONFIG  0x1B
#define REG_ACCEL_CONFIG 0x1C
#define REG_CONFIG       0x1A
#define REG_WHO_AM_I     0x75

// ±8G
const float ACCEL_SCALE = 4096.0;

// ±500 deg/s
const float GYRO_SCALE = 65.5;

// ================================================================
// STEP SETTINGS
// ================================================================

// High threshold detects the step peak
float STEP_THRESHOLD_HIGH = 11.5;

// Low threshold arms the next step
float STEP_THRESHOLD_LOW = 10.0;

// Average step length
float STEP_LENGTH_M = 0.70;

// Minimum time between steps
const unsigned long STEP_MIN_INTERVAL_MS = 400;

// Prevents double counting
bool stepPeakDetected = false;

// ================================================================
// POSITION VARIABLES
// ================================================================

double originLat = 0.0;
double originLng = 0.0;

double posX = 0.0;
double posY = 0.0;

double heading = 0.0;

bool haveFix = false;

int stepCount = 0;

// ================================================================
// TIMERS
// ================================================================

unsigned long lastStepTime = 0;
unsigned long lastImuTime = 0;

// Forward declaration
void sendTelemetryToDashboard(double lat, double lng, int steps, float headDeg, float offX, float offY, float accel);

// ================================================================
// FUNCTION: WRITE MPU REGISTER
// ================================================================

bool writeRegister(
    uint8_t reg,
    uint8_t value
)
{
    Wire.beginTransmission(MPU_ADDR);

    Wire.write(reg);
    Wire.write(value);

    uint8_t error = Wire.endTransmission();

    return error == 0;
}

// ================================================================
// FUNCTION: READ MPU REGISTERS
// ================================================================

bool readRegisters(
    uint8_t startReg,
    uint8_t* buffer,
    uint8_t length
)
{
    Wire.beginTransmission(MPU_ADDR);

    Wire.write(startReg);

    if (Wire.endTransmission(false) != 0)
    {
        return false;
    }

    uint8_t received =
        Wire.requestFrom(
            MPU_ADDR,
            length
        );

    if (received != length)
    {
        return false;
    }

    for (uint8_t i = 0; i < length; i++)
    {
        buffer[i] = Wire.read();
    }

    return true;
}

// ================================================================
// FUNCTION: READ SINGLE REGISTER
// ================================================================

uint8_t readRegister(uint8_t reg)
{
    uint8_t value = 0;

    if (!readRegisters(reg, &value, 1))
    {
        return 0xFF;
    }

    return value;
}

// ================================================================
// FUNCTION: INITIALIZE MPU6050
// ================================================================

bool initializeMPU()
{
    uint8_t whoAmI =
        readRegister(REG_WHO_AM_I);

    if (whoAmI == 0xFF)
    {
        return false;
    }

    // Wake MPU6050
    if (!writeRegister(
            REG_PWR_MGMT_1,
            0x00
        ))
    {
        return false;
    }

    delay(100);

    // Accelerometer ±8G
    if (!writeRegister(
            REG_ACCEL_CONFIG,
            0x10
        ))
    {
        return false;
    }

    // Gyroscope ±500 deg/s
    if (!writeRegister(
            REG_GYRO_CONFIG,
            0x08
        ))
    {
        return false;
    }

    // Digital low pass filter
    if (!writeRegister(
            REG_CONFIG,
            0x04
        ))
    {
        return false;
    }

    delay(100);

    return true;
}

// ================================================================
// FUNCTION: READ MPU6050 DATA
// ================================================================

bool readMPU(
    float &ax,
    float &ay,
    float &az,
    float &gx,
    float &gy,
    float &gz
)
{
    uint8_t data[14];

    if (!readRegisters(
            REG_ACCEL_XOUT_H,
            data,
            14
        ))
    {
        return false;
    }

    int16_t rawAx =
        ((int16_t)data[0] << 8) |
        data[1];

    int16_t rawAy =
        ((int16_t)data[2] << 8) |
        data[3];

    int16_t rawAz =
        ((int16_t)data[4] << 8) |
        data[5];

    int16_t rawGx =
        ((int16_t)data[8] << 8) |
        data[9];

    int16_t rawGy =
        ((int16_t)data[10] << 8) |
        data[11];

    int16_t rawGz =
        ((int16_t)data[12] << 8) |
        data[13];

    // Convert acceleration to m/s²

    float axG =
        rawAx / ACCEL_SCALE;

    float ayG =
        rawAy / ACCEL_SCALE;

    float azG =
        rawAz / ACCEL_SCALE;

    ax = axG * 9.80665;
    ay = ayG * 9.80665;
    az = azG * 9.80665;

    // Convert gyro to deg/s

    float gxDeg =
        rawGx / GYRO_SCALE;

    float gyDeg =
        rawGy / GYRO_SCALE;

    float gzDeg =
        rawGz / GYRO_SCALE;

    // Convert deg/s to rad/s

    gx =
        gxDeg * PI / 180.0;

    gy =
        gyDeg * PI / 180.0;

    gz =
        gzDeg * PI / 180.0;

    return true;
}

// ================================================================
// FUNCTION: CONVERT LOCAL X/Y TO LAT/LNG
// ================================================================

void getCurrentLatLng(
    double &lat,
    double &lng
)
{
    const double metersPerDegLat =
        111320.0;

    double metersPerDegLng =
        111320.0 *
        cos(
            originLat *
            PI /
            180.0
        );

    lat =
        originLat +
        (
            posY /
            metersPerDegLat
        );

    if (fabs(metersPerDegLng) > 0.000001)
    {
        lng =
            originLng +
            (
                posX /
                metersPerDegLng
            );
    }
    else
    {
        lng = originLng;
    }
}

// ================================================================
// FUNCTION: SEND TELEMETRY TO DASHBOARD
// ================================================================

void sendTelemetryToDashboard(
    double lat,
    double lng,
    int steps,
    float headDeg,
    float offX,
    float offY,
    float accel
)
{
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    http.begin(DASHBOARD_SERVER_URL);
    http.addHeader("Content-Type", "application/json");

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif

    doc["lat"] = lat;
    doc["lng"] = lng;
    doc["accuracy"] = 12.0;
    doc["step_count"] = steps;
    doc["heading"] = headDeg;
    doc["offset_x"] = offX;
    doc["offset_y"] = offY;
    doc["accel"] = accel;

    String requestBody;
    serializeJson(doc, requestBody);

    http.POST(requestBody);
    http.end();
}

// ================================================================
// FUNCTION: WIFI LOCATION
// ================================================================

bool getWifiFix()
{
    int n =
        WiFi.scanNetworks(
            false,
            true
        );

    if (n <= 0)
    {
        return false;
    }

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
    JsonArray accessPoints = doc["wifiAccessPoints"].to<JsonArray>();
#else
    DynamicJsonDocument doc(4096);
    JsonArray accessPoints = doc.createNestedArray("wifiAccessPoints");
#endif

    int count =
        min(n, 10);

    for (
        int i = 0;
        i < count;
        i++
    )
    {
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonObject ap = accessPoints.add<JsonObject>();
#else
        JsonObject ap = accessPoints.createNestedObject();
#endif

        ap["macAddress"] =
            WiFi.BSSIDstr(i);

        ap["signalStrength"] =
            WiFi.RSSI(i);
    }

    String requestBody;

    serializeJson(
        doc,
        requestBody
    );

    // ============================================================
    // GOOGLE GEOLOCATION API
    // ============================================================

    String url =
        String(
            "https://www.googleapis.com/geolocation/v1/geolocate?key="
        ) +
        GOOGLE_API_KEY;

    WiFiClientSecure client;

    client.setInsecure();

    HTTPClient http;

    if (!http.begin(
            client,
            url
        ))
    {
        WiFi.scanDelete();

        return false;
    }

    http.addHeader(
        "Content-Type",
        "application/json"
    );

    int httpCode =
        http.POST(
            requestBody
        );

    if (httpCode != 200)
    {
        http.end();

        WiFi.scanDelete();

        return false;
    }

    String response =
        http.getString();

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument responseDoc;
#else
    DynamicJsonDocument responseDoc(2048);
#endif

    DeserializationError error =
        deserializeJson(
            responseDoc,
            response
        );

    if (error)
    {
        http.end();

        WiFi.scanDelete();

        return false;
    }

    double newLat =
        responseDoc["location"]["lat"];

    double newLng =
        responseDoc["location"]["lng"];

    http.end();

    WiFi.scanDelete();

    if (
        newLat == 0.0 &&
        newLng == 0.0
    )
    {
        return false;
    }

    originLat = newLat;
    originLng = newLng;

    posX = 0.0;
    posY = 0.0;

    haveFix = true;

    // Send updated location to dashboard
    sendTelemetryToDashboard(
        originLat,
        originLng,
        stepCount,
        heading * 180.0 / PI,
        posX,
        posY,
        9.81
    );

    return true;
}

// ================================================================
// FUNCTION: PROCESS ONE STEP
// ================================================================

void processStep(
    float accelerationMagnitude
)
{
    // Increment step counter
    stepCount++;

    // Add step to dead-reckoned offset
    posX +=
        STEP_LENGTH_M *
        cos(heading);

    posY +=
        STEP_LENGTH_M *
        sin(heading);

    // Current estimated position
    double currentLat = 0.0;
    double currentLng = 0.0;

    if (haveFix)
    {
        getCurrentLatLng(
            currentLat,
            currentLng
        );
    }

    // Send telemetry to Dashboard
    sendTelemetryToDashboard(
        currentLat,
        currentLng,
        stepCount,
        heading * 180.0 / PI,
        posX,
        posY,
        accelerationMagnitude
    );

    // Wifi Location update after this step
    if (
        WiFi.status() ==
        WL_CONNECTED
    )
    {
        getWifiFix();
    }
}

// ================================================================
// FUNCTION: UPDATE IMU
// ================================================================

void updateIMU()
{
    float ax;
    float ay;
    float az;

    float gx;
    float gy;
    float gz;

    if (!readMPU(
            ax,
            ay,
            az,
            gx,
            gy,
            gz
        ))
    {
        return;
    }

    unsigned long now =
        millis();

    if (lastImuTime == 0)
    {
        lastImuTime = now;
        return;
    }

    float dt =
        (
            now -
            lastImuTime
        ) /
        1000.0;

    lastImuTime = now;

    if (dt > 0.1)
    {
        dt = 0.1;
    }

    // Heading
    heading +=
        gz *
        dt;

    while (
        heading >=
        2.0 * PI
    )
    {
        heading -=
            2.0 * PI;
    }

    while (
        heading < 0
    )
    {
        heading +=
            2.0 * PI;
    }

    // Total acceleration
    float accelerationMagnitude =
        sqrt(
            ax * ax +
            ay * ay +
            az * az
        );

    // Step peak detection
    if (!stepPeakDetected)
    {
        if (
            accelerationMagnitude >
            STEP_THRESHOLD_HIGH
        )
        {
            if (
                now -
                lastStepTime >
                STEP_MIN_INTERVAL_MS
            )
            {
                stepPeakDetected = true;

                lastStepTime = now;

                processStep(
                    accelerationMagnitude
                );
            }
        }
    }
    else
    {
        if (
            accelerationMagnitude <
            STEP_THRESHOLD_LOW
        )
        {
            stepPeakDetected = false;
        }
    }
}

// ================================================================
// SETUP
// ================================================================

void setup()
{
    Serial.begin(
        115200
    );

    delay(1000);

    // ONLY print the Dashboard URL to Serial Monitor
    Serial.println();
    Serial.print("DASHBOARD URL: ");
    Serial.println(DASHBOARD_SERVER_URL);

    // I2C
    Wire.begin(
        21,
        22
    );

    Wire.setClock(
        400000
    );

    // MPU6050
    if (!initializeMPU())
    {
        while (true)
        {
            delay(1000);
        }
    }

    // WIFI
    WiFi.mode(
        WIFI_STA
    );

    WiFi.begin(
        ssid,
        password
    );

    int attempts = 0;

    while (
        WiFi.status() !=
        WL_CONNECTED &&
        attempts < 40
    )
    {
        delay(500);

        attempts++;
    }

    if (
        WiFi.status() ==
        WL_CONNECTED
    )
    {
        getWifiFix();
    }

    lastImuTime =
        millis();

    lastStepTime = 0;
}

// ================================================================
// MAIN LOOP
// ================================================================

void loop()
{
    updateIMU();
    delay(10);
}
