#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <math.h>

// ================================================================
// WIFI & DASHBOARD SETTINGS
// ================================================================

const char* ssid = "iPhone";
const char* password = "atkaresam123";

const char* GOOGLE_API_KEY = "AIzaSyB9WQW_b6lyWzUGraREahHUOR12Cdmvui8";

// Dashboard Server Telemetry Endpoint
// IMPORTANT: Replace "YOUR_LAPTOP_IP" below with the IP address of the laptop running dashboard_server.py!
// Example: "http://192.168.1.15:8000/api/telemetry"
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

// Gyro Z offset calibration variable
float gzOffset = 0.0;

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
            (uint8_t)MPU_ADDR,
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
// FUNCTION: CALIBRATE GYRO Z-AXIS DRIFT
// ================================================================

void calibrateGyro() {
    Serial.println("Calibrating Gyroscope... Keep sensor stationary!");
    long sumGz = 0;
    int samples = 200;

    for (int i = 0; i < samples; i++) {
        uint8_t data[2];
        if (readRegisters(0x47, data, 2)) {
            int16_t rawGz = ((int16_t)data[0] << 8) | data[1];
            sumGz += rawGz;
        }
        delay(5);
    }

    float avgRawGz = (float)sumGz / samples;
    float gzDeg = avgRawGz / GYRO_SCALE;
    gzOffset = gzDeg * PI / 180.0;

    Serial.printf("Gyro Z offset calibrated: %.4f rad/s\n", gzOffset);
}

// ================================================================
// FUNCTION: INITIALIZE MPU6050
// ================================================================

bool initializeMPU()
{
    Serial.println();
    Serial.println("Checking MPU6050...");

    uint8_t whoAmI =
        readRegister(REG_WHO_AM_I);

    Serial.print("WHO_AM_I = 0x");

    if (whoAmI < 16)
    {
        Serial.print("0");
    }

    Serial.println(whoAmI, HEX);

    if (whoAmI == 0xFF)
    {
        Serial.println(
            "Could not communicate with MPU6050."
        );

        return false;
    }

    if (whoAmI != 0x68 && whoAmI != 0x70)
    {
        Serial.println(
            "Warning: unexpected WHO_AM_I value."
        );
    }

    // Wake MPU6050
    Serial.println("Waking MPU6050...");

    if (!writeRegister(
            REG_PWR_MGMT_1,
            0x00
        ))
    {
        Serial.println(
            "Failed to wake MPU6050."
        );

        return false;
    }

    delay(100);

    // Accelerometer ±8G
    if (!writeRegister(
            REG_ACCEL_CONFIG,
            0x10
        ))
    {
        Serial.println(
            "Accelerometer configuration failed."
        );

        return false;
    }

    // Gyroscope ±500 deg/s
    if (!writeRegister(
            REG_GYRO_CONFIG,
            0x08
        ))
    {
        Serial.println(
            "Gyroscope configuration failed."
        );

        return false;
    }

    // Digital low pass filter
    if (!writeRegister(
            REG_CONFIG,
            0x04
        ))
    {
        Serial.println(
            "Filter configuration failed."
        );

        return false;
    }

    delay(100);

    calibrateGyro();

    Serial.println();
    Serial.println(
        "MPU6050 initialized successfully!"
    );

    Serial.println(
        "Accelerometer : +/-8G"
    );

    Serial.println(
        "Gyroscope     : +/-500 deg/s"
    );

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

    int16_t rawAx = ((int16_t)data[0] << 8) | data[1];
    int16_t rawAy = ((int16_t)data[2] << 8) | data[3];
    int16_t rawAz = ((int16_t)data[4] << 8) | data[5];

    int16_t rawGx = ((int16_t)data[8] << 8) | data[9];
    int16_t rawGy = ((int16_t)data[10] << 8) | data[11];
    int16_t rawGz = ((int16_t)data[12] << 8) | data[13];

    // Convert acceleration to m/s²
    float axG = rawAx / ACCEL_SCALE;
    float ayG = rawAy / ACCEL_SCALE;
    float azG = rawAz / ACCEL_SCALE;

    ax = axG * 9.80665;
    ay = ayG * 9.80665;
    az = azG * 9.80665;

    // Convert gyro to deg/s and subtract calibrated offset
    float gxDeg = rawGx / GYRO_SCALE;
    float gyDeg = rawGy / GYRO_SCALE;
    float gzDeg = rawGz / GYRO_SCALE;

    gx = gxDeg * PI / 180.0;
    gy = gyDeg * PI / 180.0;
    gz = (gzDeg * PI / 180.0) - gzOffset;

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
    const double metersPerDegLat = 111320.0;

    double metersPerDegLng = 111320.0 * cos(originLat * PI / 180.0);

    lat = originLat + (posY / metersPerDegLat);

    if (fabs(metersPerDegLng) > 0.000001)
    {
        lng = originLng + (posX / metersPerDegLng);
    }
    else
    {
        lng = originLng;
    }
}

// ================================================================
// FUNCTION: WIFI LOCATION (Supports ArduinoJson v6 and v7)
// ================================================================

bool getWifiFix()
{
    Serial.println();
    Serial.println("Scanning Wi-Fi networks...");

    int n = WiFi.scanNetworks(false, true);

    if (n <= 0)
    {
        Serial.println("No Wi-Fi networks found.");
        return false;
    }

    Serial.printf("Wi-Fi networks found: %d\n", n);

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
    JsonArray accessPoints = doc["wifiAccessPoints"].to<JsonArray>();
#else
    DynamicJsonDocument doc(4096);
    JsonArray accessPoints = doc.createNestedArray("wifiAccessPoints");
#endif

    int count = min(n, 10);

    for (int i = 0; i < count; i++)
    {
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonObject ap = accessPoints.add<JsonObject>();
#else
        JsonObject ap = accessPoints.createNestedObject();
#endif
        ap["macAddress"] = WiFi.BSSIDstr(i);
        ap["signalStrength"] = WiFi.RSSI(i);

        Serial.printf("%d. %s   RSSI: %d dBm\n", i + 1, WiFi.BSSIDstr(i).c_str(), WiFi.RSSI(i));
    }

    String requestBody;
    serializeJson(doc, requestBody);

    String url = String("https://www.googleapis.com/geolocation/v1/geolocate?key=") + GOOGLE_API_KEY;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    if (!http.begin(client, url))
    {
        Serial.println("HTTPS connection failed.");
        WiFi.scanDelete();
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    Serial.println("Sending Wi-Fi data to Google...");

    int httpCode = http.POST(requestBody);

    if (httpCode != 200)
    {
        Serial.println();
        Serial.println("Google Geolocation request FAILED.");
        Serial.printf("HTTP code: %d\n", httpCode);

        String response = http.getString();
        Serial.println(response);

        http.end();
        WiFi.scanDelete();
        return false;
    }

    String response = http.getString();

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument responseDoc;
#else
    DynamicJsonDocument responseDoc(2048);
#endif

    DeserializationError error = deserializeJson(responseDoc, response);

    if (error)
    {
        Serial.print("JSON error: ");
        Serial.println(error.c_str());
        http.end();
        WiFi.scanDelete();
        return false;
    }

    double newLat = responseDoc["location"]["lat"];
    double newLng = responseDoc["location"]["lng"];
    double accuracy = responseDoc["accuracy"];

    http.end();
    WiFi.scanDelete();

    if (newLat == 0.0 && newLng == 0.0)
    {
        Serial.println("Invalid Wi-Fi location.");
        return false;
    }

    originLat = newLat;
    originLng = newLng;

    posX = 0.0;
    posY = 0.0;

    haveFix = true;

    Serial.println();
    Serial.println("=========================================");
    Serial.println("          WIFI POSITION UPDATED");
    Serial.println("=========================================");
    Serial.printf("Step Count    : %d\n", stepCount);
    Serial.printf("Latitude      : %.6f\n", originLat);
    Serial.printf("Longitude     : %.6f\n", originLng);

    if (accuracy > 0)
    {
        Serial.printf("Accuracy      : %.2f meters\n", accuracy);
    }
    Serial.println("=========================================");

    // Post updated WiFi location fix immediately to Dashboard
    sendTelemetryToDashboard(originLat, originLng, stepCount, heading * 180.0 / PI, posX, posY, 9.81);

    return true;
}

// ================================================================
// FUNCTION: SEND TELEMETRY JSON TO DASHBOARD SERVER
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

    int httpCode = http.POST(requestBody);
    if (httpCode == 200) {
        Serial.println("Telemetry posted to Dashboard successfully.");
    } else {
        Serial.printf("Dashboard POST status code: %d\n", httpCode);
    }
    http.end();
}

// ================================================================
// FUNCTION: PROCESS ONE STEP
// ================================================================

void processStep(float accelerationMagnitude)
{
    stepCount++;

    // Dead Reckoning Navigation Coordinates
    posX += STEP_LENGTH_M * sin(heading);
    posY += STEP_LENGTH_M * cos(heading);

    double currentLat = originLat;
    double currentLng = originLng;

    if (haveFix)
    {
        getCurrentLatLng(currentLat, currentLng);
    }

    Serial.println();
    Serial.println("*****************************************");
    Serial.printf("STEP %d DETECTED\n", stepCount);
    Serial.printf("Acceleration : %.2f m/s^2\n", accelerationMagnitude);
    Serial.printf("Heading      : %.2f degrees\n", heading * 180.0 / PI);
    Serial.printf("Step Length  : %.2f m\n", STEP_LENGTH_M);
    Serial.printf("Total Steps  : %d\n", stepCount);
    Serial.printf("Distance     : %.2f m\n", stepCount * STEP_LENGTH_M);
    Serial.printf("Offset X     : %.2f m\n", posX);
    Serial.printf("Offset Y     : %.2f m\n", posY);

    if (haveFix)
    {
        Serial.printf("Latitude     : %.6f\n", currentLat);
        Serial.printf("Longitude    : %.6f\n", currentLng);
    }
    else
    {
        Serial.println("Latitude     : No Wi-Fi fix");
        Serial.println("Longitude    : No Wi-Fi fix");
    }

    Serial.println("*****************************************");

    // Post step telemetry to Python Dashboard
    sendTelemetryToDashboard(
        currentLat,
        currentLng,
        stepCount,
        heading * 180.0 / PI,
        posX,
        posY,
        accelerationMagnitude
    );

    // WiFi Location recalibration check after this step
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println();
        Serial.printf("Updating Wi-Fi position after Step %d...\n", stepCount);
        getWifiFix();
    }
    else
    {
        Serial.println("Wi-Fi unavailable. Continuing with MPU6050 dead reckoning.");
    }
}

// ================================================================
// FUNCTION: UPDATE IMU
// ================================================================

void updateIMU()
{
    float ax, ay, az;
    float gx, gy, gz;

    if (!readMPU(ax, ay, az, gx, gy, gz))
    {
        Serial.println("MPU read error!");
        return;
    }

    unsigned long now = millis();

    if (lastImuTime == 0)
    {
        lastImuTime = now;
        return;
    }

    float dt = (now - lastImuTime) / 1000.0;
    lastImuTime = now;

    if (dt > 0.1)
    {
        dt = 0.1;
    }

    // Heading Integration
    heading += gz * dt;

    heading = fmod(heading, 2.0 * PI);
    if (heading < 0)
    {
        heading += 2.0 * PI;
    }

    // Total Acceleration Magnitude
    float accelerationMagnitude = sqrt(ax * ax + ay * ay + az * az);

    // Step Peak Detection
    if (!stepPeakDetected)
    {
        if (accelerationMagnitude > STEP_THRESHOLD_HIGH)
        {
            if (now - lastStepTime > STEP_MIN_INTERVAL_MS)
            {
                stepPeakDetected = true;
                lastStepTime = now;
                processStep(accelerationMagnitude);
            }
        }
    }
    else
    {
        if (accelerationMagnitude < STEP_THRESHOLD_LOW)
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
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=========================================");
    Serial.println("       GPS-LESS ESP32 TRACKER");
    Serial.println("=========================================");

    Serial.println("\nStarting I2C...");
    Wire.begin(21, 22);
    Wire.setClock(400000);

    if (!initializeMPU())
    {
        Serial.println("\nMPU6050 initialization failed.");
        while (true)
        {
            delay(1000);
        }
    }

    Serial.println("\nConnecting to Wi-Fi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("Wi-Fi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        Serial.println("\nObtaining initial Wi-Fi location...");
        getWifiFix();
    }
    else
    {
        Serial.println("Wi-Fi connection failed. MPU6050 step tracking will continue.");
    }

    lastImuTime = millis();
    lastStepTime = 0;

    Serial.println();
    Serial.println("=========================================");
    Serial.println("          SETUP COMPLETE");
    Serial.println("=========================================");
    Serial.print("Dashboard Telemetry Target: ");
    Serial.println(DASHBOARD_SERVER_URL);
    Serial.println("Start walking!");
    Serial.println();
}

// ================================================================
// MAIN LOOP
// ================================================================

void loop()
{
    updateIMU();
    delay(10);
}
