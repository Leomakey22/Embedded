#define BLYNK_TEMPLATE_ID "TMPL6GhsNAJbH"
#define BLYNK_TEMPLATE_NAME "Smart Kitchen"
#define BLYNK_AUTH_TOKEN "2kpq_mYeXnwrxzMd0-2xk487wgRhkHdY"

/* Includes ---------------------------------------------------------------- */
#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <LEEE-project-1_inferencing.h> 
#include <DHT.h>

// -------------------- WI-FI SETTINGS --------------------
char ssid[] = "LEO";    
char pass[] = "12345678";   

// -------------------- PINS CONFIGURATION --------------------
#define MQ2_PIN      32  
#define MQ135_PIN    33  
#define FLAME_PIN    25  
#define DHT_PIN      4   
#define DHT_TYPE     DHT11 
#define BUZZER_PIN   14  
#define LED_PIN      2   // RED LED (Alarm)
#define LED_GREEN_PIN 18 // GREEN LED (Normal/Cooking) - Connect sa GPIO 18

DHT dht(DHT_PIN, DHT_TYPE);

// -------------------- THRESHOLDS --------------------
float TEMP_THRESHOLD = 50.0; 
int GAS_THRESHOLD = 2000;    

float features[4]; 
bool alarmSent = false; 
bool isConnected = false; 

void setup()
{
    Serial.begin(115200);
    dht.begin();

    pinMode(FLAME_PIN, INPUT);
    pinMode(MQ2_PIN, INPUT);
    pinMode(MQ135_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    
    // Setup LEDs
    pinMode(LED_PIN, OUTPUT);       // Red
    pinMode(LED_GREEN_PIN, OUTPUT); // Green

    Serial.println("System Starting...");
    
    // WiFi Connection Logic
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    WiFi.begin(ssid, pass);
    
    int timer = 0;
    while (WiFi.status() != WL_CONNECTED && timer < 20) { 
        delay(500);
        Serial.print(".");
        timer++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi Connected!");
        Blynk.config(BLYNK_AUTH_TOKEN); 
        isConnected = true;
    } else {
        Serial.println("\n❌ WiFi FAILED! (Running Offline Mode)");
        isConnected = false;
    }
    
    delay(1000);
}

void loop()
{
    // Check WiFi Connection
    if (isConnected && WiFi.status() == WL_CONNECTED) {
        Blynk.run();
    } else {
        if (WiFi.status() != WL_CONNECTED) {
             Serial.println("Reconnecting to WiFi...");
             WiFi.begin(ssid, pass);
             delay(2000); 
        }
    }

    // 1. READ REAL SENSORS
    int gas = analogRead(MQ2_PIN);
    int smoke = analogRead(MQ135_PIN);
    float temp = dht.readTemperature();
    
    if (isnan(temp)) temp = 0; 

    int rawFlame = digitalRead(FLAME_PIN);
    int finalFlame = (rawFlame == LOW) ? 1 : 0; 

    // Print Data
    Serial.print("Gas: "); Serial.print(gas);
    Serial.print(" | Smoke: "); Serial.print(smoke);
    Serial.print(" | Temp: "); Serial.print(temp);
    Serial.print(" | Flame: "); Serial.println(finalFlame);

    // 2. SEND DATA TO BLYNK
    if (isConnected) {
        Blynk.virtualWrite(V0, temp);   
        Blynk.virtualWrite(V1, smoke);  
        Blynk.virtualWrite(V2, gas);    
    }

    // 3. AI LOGIC PREPARATION
    features[0] = gas;
    features[1] = smoke;
    features[2] = temp;
    features[3] = finalFlame;

    if (sizeof(features) / sizeof(float) != EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        return; 
    }

    // 4. RUN AI CLASSIFIER
    ei_impulse_result_t result = { 0 };
    signal_t features_signal;
    features_signal.total_length = sizeof(features) / sizeof(float);
    features_signal.get_data = &raw_feature_get_data;

    run_classifier(&features_signal, &result, false);

    String ai_prediction = "";
    float max_score = 0.0;
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (result.classification[ix].value > max_score) {
            max_score = result.classification[ix].value;
            ai_prediction = String(result.classification[ix].label);
        }
    }

    // 5. DECISION LOGIC (UPDATED LED BEHAVIOR)
    
    bool ai_sees_danger = (ai_prediction.startsWith("fire") || ai_prediction.startsWith("cooking"));
    bool isHighTemp = (temp > TEMP_THRESHOLD);
    bool isGasLeak = (gas > GAS_THRESHOLD);

    // --- MANUAL FILTER (Para dili magpataka og Cooking Mode kung ubos ang smoke) ---
    if (smoke < 300 && gas < 300 && finalFlame == 0) {
        ai_sees_danger = false; 
    }

    // --- PRIORITY 1: FIRE ALARM (DANGER) ---
    if (finalFlame == 1 || (ai_sees_danger && isHighTemp)) {
         Serial.println("🚨 STATUS: FIRE ALARM!");
         
         digitalWrite(LED_PIN, HIGH);       // RED ON
         digitalWrite(LED_GREEN_PIN, LOW);  // GREEN OFF (Mapalong kay Emergency)
         digitalWrite(BUZZER_PIN, HIGH); 
         
         if (isConnected) {
             Blynk.virtualWrite(V3, "🚨 FIRE! 🚨");
             if (alarmSent == false) {
                 Serial.println(">>> SENDING NOTIFICATION... <<<");
                 Blynk.logEvent("fire_alert", "WARNING: Fire Detected!"); 
                 alarmSent = true; 
             }
         }
    }
    
    // --- PRIORITY 2: GAS LEAK (DANGER) ---
    else if (isGasLeak) {
         Serial.println("⚠️ STATUS: GAS LEAK!");
         
         digitalWrite(LED_PIN, HIGH);       // RED ON
         digitalWrite(LED_GREEN_PIN, LOW);  // GREEN OFF (Mapalong kay Emergency)
         digitalWrite(BUZZER_PIN, HIGH); 
         
         if (isConnected) {
             Blynk.virtualWrite(V3, "⚠️ GAS LEAK ⚠️");
         }
    }

    // --- PRIORITY 3: COOKING MODE (SAFE) ---
    else if (ai_sees_danger && !isHighTemp) {
        Serial.println("🍳 STATUS: COOKING MODE");
        
        digitalWrite(LED_PIN, LOW);         // RED OFF (Safe ra magluto)
        digitalWrite(LED_GREEN_PIN, HIGH);  // GREEN ON (Imong Request: Siga gihapon Green)
        digitalWrite(BUZZER_PIN, LOW); 
        
        if (isConnected) {
            Blynk.virtualWrite(V3, "Cooking Mode");
            if (alarmSent == true) {
               alarmSent = false; 
            }
        }
    } 
    
    // --- PRIORITY 4: NORMAL (SAFE) ---
    else {
        Serial.println("✅ STATUS: NORMAL");
        
        digitalWrite(LED_PIN, LOW);         // RED OFF
        digitalWrite(LED_GREEN_PIN, HIGH);  // GREEN ON
        digitalWrite(BUZZER_PIN, LOW);
        
        if (isConnected) {
            Blynk.virtualWrite(V3, "Normal");
            if (alarmSent == true) {
                alarmSent = false; 
            }
        }
    }

    delay(1000); 
}

// Helper function
int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, features + offset, length * sizeof(float));
    return 0;
}