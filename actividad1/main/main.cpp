#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- LCD CONFIGURATION ---
#define LCD_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS    2
#define I2C_SDA     21
#define I2C_SCL     22

LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);

// --- KEYPAD CONFIGURATION ---
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

byte rowPins[ROWS] = {32, 33, 25, 26};
byte colPins[COLS] = {27, 14, 13, 4};
const char NO_KEY = '\0';

// --- DHT22 CONFIGURATION ---
const byte DHTPIN = 5;

// --- BUZZER CONFIGURATION ---
#define BUZZER_PIN 18
const float TEMP_THRESHOLD = 32.0; // Celsius

// --- BUTTON CONFIGURATION ---
// NOTE: GPIO 34/35 are input-only and have NO internal pull-down.
//       Add a 10k resistor from each pin to GND.
//       If you use GPIO 16 / 17 instead, change pinMode() below to INPUT_PULLDOWN.
const byte BUTTON1_PIN = 34;
const byte BUTTON2_PIN = 35;
const unsigned long BUTTON_DEBOUNCE_MS = 30;

struct ButtonState {
    bool stable;                 // last debounced logical state (true = pressed)
    bool candidate;              // raw state being debounced
    unsigned long candidateSince;
    const char* name;
};

ButtonState btn1 = { false, false, 0, "BTN1" };
ButtonState btn2 = { false, false, 0, "BTN2" };

// Timing variables for non-blocking sensor reads
unsigned long lastDHTRead = 0;
const unsigned long DHT_READ_INTERVAL = 5000;

// LCD cached values
float lastShownTemp = NAN;
float lastShownHum  = NAN;
bool  lastAlertState = false;
bool  lastSensorOk   = true;

// ------------------------------------------------------------------
// Keypad
// ------------------------------------------------------------------
char scanKeypad() {
    for (byte row = 0; row < ROWS; ++row) {
        digitalWrite(rowPins[row], LOW);
        delayMicroseconds(3);

        for (byte col = 0; col < COLS; ++col) {
            if (digitalRead(colPins[col]) == LOW) {
                digitalWrite(rowPins[row], HIGH);
                return keys[row][col];
            }
        }
        digitalWrite(rowPins[row], HIGH);
    }
    return NO_KEY;
}

char getKey() {
    static char candidate = NO_KEY;
    static unsigned long candidateSince = 0;
    static bool reported = false;

    const char scanned = scanKeypad();
    const unsigned long now = millis();
    if (scanned != candidate) {
        candidate = scanned;
        candidateSince = now;
        reported = false;
    }

    if (candidate != NO_KEY && !reported && now - candidateSince >= 25) {
        reported = true;
        return candidate;
    }
    return NO_KEY;
}

// ------------------------------------------------------------------
// Buttons
// ------------------------------------------------------------------
// Returns true ONCE on a rising edge (button just pressed).
bool buttonPressed(ButtonState &b, byte pin) {
    const bool raw = (digitalRead(pin) == HIGH); // active HIGH
    const unsigned long now = millis();

    if (raw != b.candidate) {
        b.candidate = raw;
        b.candidateSince = now;
    }

    if (b.candidate != b.stable && (now - b.candidateSince) >= BUTTON_DEBOUNCE_MS) {
        b.stable = b.candidate;
        if (b.stable) {
            return true;  // edge: press detected
        }
    }
    return false;
}

// ------------------------------------------------------------------
// DHT22
// ------------------------------------------------------------------
bool waitForDhtLevel(uint8_t level, unsigned long timeoutUs) {
    const unsigned long start = micros();
    while (digitalRead(DHTPIN) != level) {
        if (micros() - start >= timeoutUs) return false;
    }
    return true;
}

bool readDht22(float &humidity, float &temperature) {
    uint8_t data[5] = {};

    pinMode(DHTPIN, OUTPUT);
    digitalWrite(DHTPIN, LOW);
    delay(2);
    digitalWrite(DHTPIN, HIGH);
    delayMicroseconds(30);
    pinMode(DHTPIN, INPUT_PULLUP);
    delayMicroseconds(40);

    if (!waitForDhtLevel(LOW, 120) || !waitForDhtLevel(HIGH, 120) ||
        !waitForDhtLevel(LOW, 120)) {
        return false;
    }

    for (byte bit = 0; bit < 40; ++bit) {
        if (!waitForDhtLevel(HIGH, 100)) return false;
        const unsigned long highStart = micros();
        if (!waitForDhtLevel(LOW, 120)) return false;

        data[bit / 8] <<= 1;
        if (micros() - highStart > 40) {
            data[bit / 8] |= 1;
        }
    }

    if (static_cast<uint8_t>(data[0] + data[1] + data[2] + data[3]) != data[4]) {
        return false;
    }

    const uint16_t humidityRaw = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    const uint16_t temperatureRaw =
        (static_cast<uint16_t>(data[2] & 0x7F) << 8) | data[3];

    humidity = humidityRaw / 10.0f;
    temperature = temperatureRaw / 10.0f;
    if (data[2] & 0x80) temperature = -temperature;

    return true;
}

// ------------------------------------------------------------------
// LCD helpers
// ------------------------------------------------------------------
void lcdShowStaticLabels() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.setCursor(9, 0);
    lcd.print("H:");
    lcd.setCursor(0, 1);
    lcd.print("Ready");
}

void lcdShowMeasurement(float t, float h, bool alert) {
    char tbuf[8];
    char hbuf[8];
    snprintf(tbuf, sizeof(tbuf), "%.1f", t);
    snprintf(hbuf, sizeof(hbuf), "%.1f", h);

    lcd.setCursor(2, 0);
    lcd.print("     ");
    lcd.setCursor(2, 0);
    lcd.print(tbuf);
    lcd.print((char)223);  // degree symbol
    lcd.print("C");

    lcd.setCursor(11, 0);
    lcd.print("    ");
    lcd.setCursor(11, 0);
    lcd.print(hbuf);
    lcd.print("%");

    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print(alert ? "!! OVERHEAT !!" : "Status: OK");
}

void lcdShowSensorError() {
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print("Sensor ERROR");
}

// Short transient message on row 1
void lcdShowEvent(const char* msg) {
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print(msg);
}

// ------------------------------------------------------------------
// setup / loop
// ------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Keypad + DHT22 + Buzzer + LCD + Buttons Test");

    // ---- I2C & LCD ----
    Wire.begin(I2C_SDA, I2C_SCL);
    lcd.init();
    lcd.backlight();
    lcdShowStaticLabels();

    // ---- Keypad ----
    for (byte row = 0; row < ROWS; ++row) {
        pinMode(rowPins[row], OUTPUT);
        digitalWrite(rowPins[row], HIGH);
    }
    for (byte col = 0; col < COLS; ++col) {
        pinMode(colPins[col], INPUT_PULLUP);
    }

    // ---- Buzzer ----
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // ---- Buttons ----
    // Active HIGH with external 10k pull-down.
    // If you switch to GPIO 16 / 17, use INPUT_PULLDOWN instead.
    pinMode(BUTTON1_PIN, INPUT);
    pinMode(BUTTON2_PIN, INPUT);
}

void loop() {
    // 1. Keypad
    char key = getKey();
    if (key) {
        Serial.print("Key Pressed: ");
        Serial.println(key);
        char msg[20];
        snprintf(msg, sizeof(msg), "Key: %c", key);
        lcdShowEvent(msg);
    }

    // 2. Buttons
    if (buttonPressed(btn1, BUTTON1_PIN)) {
        Serial.println("Button 1 pressed");
        lcdShowEvent("Button 1 pressed");
        // <-- add your BTN1 action here
    }
    if (buttonPressed(btn2, BUTTON2_PIN)) {
        Serial.println("Button 2 pressed");
        lcdShowEvent("Button 2 pressed");
        // <-- add your BTN2 action here
    }

    // 3. DHT22 + buzzer + LCD refresh
    const unsigned long currentMillis = millis();
    if (currentMillis - lastDHTRead >= DHT_READ_INTERVAL) {
        lastDHTRead = currentMillis;

        float h, t;
        if (!readDht22(h, t)) {
            Serial.println("Failed to read from DHT sensor!");
            digitalWrite(BUZZER_PIN, LOW);
            if (lastSensorOk) {
                lcdShowSensorError();
                lastSensorOk = false;
            }
        } else {
            Serial.print("Humidity: ");
            Serial.print(h);
            Serial.print(" %\tTemperature: ");
            Serial.print(t);
            Serial.println(" *C");

            const bool alert = (t > TEMP_THRESHOLD);
            digitalWrite(BUZZER_PIN, alert ? HIGH : LOW);
            // tone(BUZZER_PIN, 1000) / noTone(BUZZER_PIN) for a passive buzzer

            const bool changed =
                (lastSensorOk == false) ||
                (t != lastShownTemp) ||
                (h != lastShownHum) ||
                (alert != lastAlertState);

            if (changed) {
                lcdShowMeasurement(t, h, alert);
                lastShownTemp  = t;
                lastShownHum   = h;
                lastAlertState = alert;
                lastSensorOk   = true;
            }
        }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}

// ESP-IDF entry point
extern "C" void app_main(void) {
    initArduino();
    setup();
    while (1) loop();
}