#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "secrets.h"

// -- Forward declarations --
void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);


// LEDs
#define RED_LED D2
#define GREEN_LED D3
#define BEAT_LED D4

// Piezo
#define PIEZO_KICK_PIN A0
#define PIEZO_SNARE_PIN D6

// WebSocket
WebSocketsServer webSocket(81);

// Rhythm Config
struct RhythmLevel {
  int beatsPerMeasure;
 
  String name;
};

RhythmLevel levels[] = {
  {4, "4/4"},
  {3,  "3/4"},
  {6, "7/4"},
  
};


const int NUM_CYCLES = 5;
int bpm = 120;
int cueDuration = 200;
int beatInterval = 60000 / bpm;
RhythmLevel selectedLevel = {4,  "4/4"};
float targetAccuracy = 85.0;

// Session state
enum SessionState { WAITING_FOR_CONFIG, READY_TO_START, IN_PROGRESS };
SessionState sessionState = WAITING_FOR_CONFIG;

// Game state
int currentBeat = 0;
int totalBeats = 0;
int hitCount = 0;
bool cueActive = false;
bool kickTapped = false;
bool snareTapped = false;
unsigned long lastBeatTime = 0;
unsigned long cueStartTime = 0;
int currentLevelIndex = 0;


// Sensitivity
const int THRESHOLD = 100;
const int debounceDelay = 50;

void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_TEXT) {
    Serial.printf("📩 Received: %s\n", payload);

    StaticJsonDocument<200> doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (!err) {


      if (doc.containsKey("command")) {
        String command = doc["command"];
        if (command == "endSession") {
          Serial.println("Received endSession command from frontend.");
          endSession(); // Call your existing endSession function
          return;
        }
      }

      //print doc
      bpm = doc["bpm"] | bpm;
      String rhythmStr = doc["rhythm"] | selectedLevel.name;
      targetAccuracy = doc["accuracy"] | targetAccuracy;


      bool found = false;
      for (int i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
      Serial.printf("Checking level: %s\n", levels[i].name.c_str());
        if (levels[i].name == rhythmStr) {
          selectedLevel = levels[i];
          currentLevelIndex = i; // Save the index
          found = true;
          break;
        }
      }
      

      if (!found) {
        Serial.println("Unknown rhythm name. Using default.");
      }
      


      Serial.println("✅ Parsed WebSocket config:");
      Serial.print("BPM: "); Serial.println(bpm);
      Serial.print("Rhythm: "); Serial.println(selectedLevel.name);
      Serial.print("BeatsPerMeasure: "); Serial.println(selectedLevel.beatsPerMeasure);
   
      Serial.print("Target Accuracy: "); Serial.println(targetAccuracy);

      beatInterval = 60000 / bpm;

      sessionState = READY_TO_START;
      digitalWrite(RED_LED, LOW);
      digitalWrite(GREEN_LED, HIGH);

      Serial.println("✅ Config applied. Ready to start session.");
    } else {
      Serial.println("⚠️ JSON parsing failed.");
    }
  }
}


void connectToWiFi() {
  Serial.print("📡 Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries++ < 30) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected!");
    Serial.print("📶 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ Failed to connect.");
  }

  webSocket.begin();
  Serial.println("🌐 WebSocket server running");

  webSocket.onEvent(handleWebSocketEvent);
}

void startSession() {
  currentBeat = 0;
  hitCount = 0;
  totalBeats = selectedLevel.beatsPerMeasure * NUM_CYCLES;
  beatInterval = 60000 / bpm;
  cueActive = false;
  lastBeatTime = millis();

  Serial.println("🎬 Starting: " + selectedLevel.name);
  String msg = "start:" + selectedLevel.name + ",bpm=" + String(bpm) +
               ",beatsPerMeasure=" + String(selectedLevel.beatsPerMeasure) +
                ",totalBeats=" + String(totalBeats) +
               ",targetAccuracy=" + String(targetAccuracy);
  webSocket.broadcastTXT(msg);

  sessionState = IN_PROGRESS;
}

void endSession() {
  cueActive = false;
  digitalWrite(BEAT_LED, LOW);

  float accuracy = (hitCount * 100.0) / totalBeats;

  // Compose message like: end:hits=9,total=12,accuracy=75.00
  String msg = "end:hits=" + String(hitCount) +
               ",total=" + String(totalBeats) +
               ",accuracy=" + String(accuracy, 2);

  Serial.println("📤 " + msg);
  webSocket.broadcastTXT(msg);

  if (accuracy >= targetAccuracy && currentLevelIndex < (int)(sizeof(levels) / sizeof(levels[0])) - 1) {
    currentLevelIndex++;
    selectedLevel = levels[currentLevelIndex];  // update to next rhythm
    Serial.println("🎉 Level up!");
    webSocket.broadcastTXT("levelUp");
  } else {
    Serial.println("🔁 Retry level");
    webSocket.broadcastTXT("retry");
  }
  
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
  sessionState = WAITING_FOR_CONFIG;
}

void handleBeatCue(unsigned long now) {
  if (!cueActive && now - lastBeatTime >= beatInterval) {
    cueActive = true;
    cueStartTime = now;
    lastBeatTime = now;
    kickTapped = snareTapped = false;

    currentBeat++;
    digitalWrite(BEAT_LED, HIGH);
    String beatMsg = "beat:" + String(currentBeat) + "/" + String(totalBeats);
    webSocket.broadcastTXT(beatMsg);

    if (currentBeat >= totalBeats) {
      endSession();
    }
  }

  if (cueActive && now - cueStartTime >= cueDuration) {
    cueActive = false;
    digitalWrite(BEAT_LED, LOW);

    if (!kickTapped && !snareTapped) {
      Serial.println("❌ Miss");
      webSocket.broadcastTXT("miss");
    }
  }
}

void detectTaps() {
  int kick = analogRead(PIEZO_KICK_PIN);
  int snare = digitalRead(PIEZO_SNARE_PIN) == HIGH;

  if (kick > THRESHOLD && !kickTapped) {
    kickTapped = true;
    webSocket.broadcastTXT("kick");
    if (cueActive) {
      hitCount++;
      webSocket.broadcastTXT("hit");
    }
    delay(debounceDelay);
  }

  printf(" Snare: %d\n", snare); // Debugging output

  
  if (snare  && !snareTapped) {
    snareTapped = true;
    webSocket.broadcastTXT("snare");
    if (cueActive) {
      hitCount++;
      webSocket.broadcastTXT("hit");
    }
    delay(debounceDelay);
  }

}

void setup() {
  Serial.begin(115200);

  pinMode(BEAT_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(PIEZO_KICK_PIN, INPUT);
  pinMode(PIEZO_SNARE_PIN, INPUT);

  digitalWrite(RED_LED, HIGH);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(BEAT_LED, LOW);

  connectToWiFi();
}

void loop() {
  webSocket.loop();
  unsigned long now = millis();

  if (sessionState == READY_TO_START) {
    startSession();
  }

  if (sessionState == IN_PROGRESS) {
    handleBeatCue(now);
    detectTaps();
  }

  delay(10);
}
