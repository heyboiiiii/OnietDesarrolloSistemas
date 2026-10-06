#include <Arduino.h>
#include <Keypad.h>
#include <DHT.h>

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
byte colPins[COLS] = {27, 14, 4, 13}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// --- DHT22 CONFIGURATION ---
#define DHTPIN 5        
#define DHTTYPE DHT22   
DHT dht(DHTPIN, DHTTYPE);

// --- BUZZER CONFIGURATION ---
#define BUZZER_PIN 18
const float TEMP_THRESHOLD = 32.0; // Temperature threshold in Celsius

// Timing variables for non-blocking sensor reads
unsigned long lastDHTRead = 0;
const unsigned long DHT_READ_INTERVAL = 5000; // Read every 5 seconds

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Keypad + DHT22 + Buzzer Test");
    
    // Initialize DHT sensor
    dht.begin();
    
    // Initialize Buzzer pin
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is off at startup
}

void loop() {
    // 1. Handle Keypad Input (Non-blocking)
    char key = keypad.getKey();
    if (key) {
        Serial.print("Key Pressed: ");
        Serial.println(key);
    }

    // 2. Handle DHT22 Sensor Reading and Buzzer Logic (Non-blocking, timed)
    unsigned long currentMillis = millis();
    if (currentMillis - lastDHTRead >= DHT_READ_INTERVAL) {
        lastDHTRead = currentMillis;

        float h = dht.readHumidity();
        float t = dht.readTemperature(); // Read temperature as Celsius

        // Check if any reads failed
        if (isnan(h) || isnan(t)) {
            Serial.println("Failed to read from DHT sensor!");
            digitalWrite(BUZZER_PIN, LOW); // Turn off buzzer on sensor failure
        } else {
            Serial.print("Humidity: ");
            Serial.print(h);
            Serial.print(" %\t");
            Serial.print("Temperature: ");
            Serial.print(t);
            Serial.println(" *C");

            // --- Buzzer Logic ---
            if (t > TEMP_THRESHOLD) {
                Serial.println("ALERT: Temperature above 32C! Buzzer ON.");
                digitalWrite(BUZZER_PIN, HIGH); 
                
                // If using a PASSIVE buzzer, comment out the line above and uncomment the line below:
                // tone(BUZZER_PIN, 1000); // Play a 1kHz tone
            } else {
                digitalWrite(BUZZER_PIN, LOW); 
                
                // If using a PASSIVE buzzer, uncomment the line below:
                // noTone(BUZZER_PIN); 
            }
        }
    }

    // Small delay to prevent task starvation
    vTaskDelay(pdMS_TO_TICKS(10));
}

// ESP-IDF entry point
extern "C" void app_main(void) {
    setup();
    while (1) {
        loop();
    }
}