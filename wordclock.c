// Wordclock firmware

#define ESP32_RTOS // for ota capability

#include <FastLED.h>
#include <WiFi.h>
#include <ezTime.h>
#include <credentials.h>
#include "OTATelnetStream.h"

// TODO: correct pin numbers
const int pinLedData = 15;
const int pinLedClock = 32;
const int pinLDR = 33;
const int pinPIR = 27;

// NTP clock server
Timezone localTimezone;
const char* localTimezoneLocation = "America/Los_Angeles";

// Led strips
const int NUM_COLS = 11;
const int NUM_ROWS = 10;
const int NUM_MINUTES = 4; // LEDs for fine minute granularity
const int NUM_LEDS = (NUM_COLS * NUM_ROWS) + NUM_MINUTES;

CRGB leds[NUM_LEDS];
boolean ledsBuffer[NUM_LEDS];

// Tasks
const int wait = 10;
const int noTasks = 2;
typedef struct {
   unsigned long previous;
   int interval;
   void (*function)();
} Task;
Task tasks[noTasks];

// Brightness and motion
boolean readManualOverrideBrightness = false;
int manualOverrideBrightness = -1;

const boolean enableMotionSensor = true;
const int noMotionThresholdMs = 30 * 60 * 1000; // 30 minutes
unsigned long lastMotionDetectedMs;
boolean logLedSleep = true;

const boolean displayItIs = true;

// Words
// Format: { line index, start position index, length }

const int w_it[3] =        { 0,  0,  strlen("it") };
const int w_is[3] =        { 0,  3,  strlen("is") };
const int w_five[3] =      { 2,  7,  strlen("five") };
const int w_ten[3] =       { 3,  0,  strlen("ten") };
const int w_quarter[3] =   { 1,  3,  strlen("quarter") };
const int w_twenty[3] =    { 2,  0,  strlen("twenty") };
const int w_half[3] =      { 3,  4,  strlen("half") };
const int w_to[3] =        { 3,  8,  strlen("to") };
const int w_past[3] =      { 4,  1,  strlen("past") };
const int w_oclock[3] =    { 9,  5,  strlen("oclock") };

const int NUM_HOURS = 12;
const int w_hours[NUM_HOURS + 1][3] = {
  { -1,  -1,  -1 }, // filler element so hour matches index position
  { 5,  0,  strlen("one") },
  { 6,  8,  strlen("two") },
  { 5,  6,  strlen("three") },
  { 6,  0,  strlen("four") },
  { 6,  4,  strlen("five") },
  { 5,  3,  strlen("six") },
  { 8,  0,  strlen("seven") },
  { 7,  0,  strlen("eight") },
  { 4,  7,  strlen("nine") },
  { 9,  0,  strlen("ten") },
  { 7,  5,  strlen("eleven") },
  { 8,  5,  strlen("twelve") }
};

const int w_minutes[NUM_MINUTES + 1][3] = {
  { -1,  -1,  -1 }, // filler element so minute matches index position
  { 10,  0,  1 },
  { 10,  1,  1 },
  { 10,  2,  1 },
  { 10,  3,  1 }
};

void loadTasks() {
  
  // Listen for input on the serial interface
  tasks[0].previous = 0;
  tasks[0].interval = 100;
  tasks[0].function = serialMenu;
  
  // Show time
  tasks[1].previous = 0;
  tasks[1].interval = 1000;
  tasks[1].function = showTime;
}

void serialMenu() {
  if (Serial.peek() == 10) { // ignore new line
    Serial.read();
  }
  if (Serial.available() > 0) {
    if (readManualOverrideBrightness) {
      if (Serial.available()) {
        int val = Serial.parseInt();
        if (val < -1 || val > 255) {
          Serial.println("[ERROR] Brightness must be between -1 and 255");
        } else {
          Serial.print("Brightness set to ");
          Serial.println(val, DEC);
          manualOverrideBrightness = val;
        }
        readManualOverrideBrightness = false;
        printMenu();
      }
    } else {
      int in = Serial.read();
      if (in == 49) { // ascii 1 = byte 49
        Serial.println("You entered [1]");
        Serial.println("  Enter brightness (0-255):");
        readManualOverrideBrightness = true;
      } else if (in == 50) {
        Serial.println("You entered [2]");
        Serial.print("  Brightness: ");
        Serial.println(manualOverrideBrightness, DEC);
        printMenu();
      } else if (in == 51) {
        Serial.println("You entered [3]");
        Serial.println("  Beginning simulation.");
        
        simulateClock();
        printMenu();
      } else if (in == 10) {

      } else {
        Serial.print("[ERROR] Invalid input: ");
        Serial.println(in);
        printMenu();
      }
    }
  }
}

void showTime() {
  showTime(now());
}

void showTime(time_t now) {
  // Get the time
  int hour = localTimezone.hour(now);
  int hourToDisplay = hour;
  int minute = localTimezone.minute(now);
  
  // DEBUG
  Serial.print("[DEBUG] ");
  Serial.print(hour, DEC);
  Serial.print(':');
  Serial.println(minute, DEC);
  
  // "IT IS"
  if (displayItIs) {
    displayWord(w_it);
    displayWord(w_is);
  }
  
  // Minutes
  if (minute == 0) {
    displayWord(w_oclock);
  }
  else {
  	int floorMinute = (minute / 5) * 5;

  	switch (floorMinute) {
  		case 0:
  			break;
		  case 5:
  			displayWord(w_five);
        break;
  		case 10:
  			displayWord(w_ten);
        break;
		  case 15:
  			displayWord(w_quarter);
        break;
  		case 20:
  			displayWord(w_twenty);
        break;
		  case 25:
  			displayWord(w_twenty);
  			displayWord(w_five);
        break;
  		case 30:
  			displayWord(w_half);
        break;
		  case 35:
  			displayWord(w_twenty);
  			displayWord(w_five);
        break;
  		case 40:
  			displayWord(w_twenty);
        break;
		  case 45:
  			displayWord(w_quarter);
        break;
  		case 50:
  			displayWord(w_ten);
        break;
		  case 55:
  			displayWord(w_five);
        break;
  		default:
  			Serial.print("[ERROR] Invalid floorMinute: ");
        Serial.println(floorMinute, DEC);
  	}
 
    if (minute <= 30) {
      displayWord(w_past);
    } else {
      displayWord(w_to);
      hourToDisplay++;
    }
  } 
  
  // Hours
  if (hourToDisplay < 12) {
    displayWord(w_hours[hourToDisplay]);
  } else {
    displayWord(w_hours[hourToDisplay - 12]);
  }

  // Fine minute granularity
  int floorMinute = minute % 5;
  if (floorMinute > 0) {
    displayWord(w_minutes[floorMinute]);
  }

  updateDisplayAndClearBuffer();
}

void displayWord(const int word[3]){
  int row = word[0];
  int col = word[1];
  int length = word[3];

  for (int i = 0; i < length; i++) {
    int ledNum = convertFrom2DTo1D(row, col + i);
    ledsBuffer[ledNum] = true;
  }
}

int convertFrom2DTo1D(int row, int col) {
  return row * NUM_COLS + col;
}

void updateDisplayAndClearBuffer() {
  for (int i = 0; i < NUM_LEDS; i++) {
    if (ledsBuffer[i] == true) {
      leds[i] = CRGB::White;
    } else {
      leds[i] = CRGB::Black;
    }

    // reset buffer
    ledsBuffer[i] = false;
  }

  setBrightness();
  FastLED.show();
}

void setBrightness() {
  int lightValue = analogRead(pinLDR);
  Serial.print("LDR value is: ");
  Serial.println(lightValue, DEC);

  int brightness = lightValueToBrightness(lightValue);
  Serial.print("Calculated brightness value is: ");
  Serial.println(brightness, DEC);
  
  if (millis() - lastMotionDetectedMs > noMotionThresholdMs) {
    if (logLedSleep) {
      Serial.println("Motion detected");
      logLedSleep = false;
    }
    brightness = 0;
  } else {
    logLedSleep = true;
  }
  
  if (manualOverrideBrightness >= 0 && manualOverrideBrightness <= 255) {
    brightness = manualOverrideBrightness;
    Serial.print("Overriding brightness with: ");
    Serial.println(brightness, DEC);
  }
  
  FastLED.setBrightness(brightness);
}

// TODO: empirical testing required to determine proper brightness function
int lightValueToBrightness(int lightValue) {
  int brightness = lightValue;
  return 0;
}

void IRAM_ATTR detectsMovement() {
  Serial.println("Motion detected");
  lastMotionDetectedMs = millis();
}

// iterate through all possible times
// total duration = 144s with 200ms delay
void simulateClock() {
  time_t simulatedTime = 0;

  for (int i = 0; i < (12 * 60); i++) {
    showTime(simulatedTime);

    // 60*12/5 = 144 steps. 0.5s per step = .2s delay
    simulatedTime += 60; // increment 1 minute
    delay(200);
  }
}

void printMenu() {
  Serial.println("");
  Serial.println("Menu");
  Serial.println("----");
  Serial.println("  1. Set brightness override");
  Serial.println("  2. Read brightness override");
  Serial.println("  3. Simulate for testing");
  Serial.println("");
}

void setup() {
  Serial.begin(9600);
  delay(500);

  Serial.println("[INFO] Wordclock is booting...");

  Serial.println("[INFO] OTA");
  setupOTA("wordclock", mySSID, myPASSWORD);
  delay(5000);
  
  Serial.println("[INFO] LEDs");
  FastLED.addLeds<SK9822, pinLedData, pinLedClock, BGR>(leds, NUM_LEDS);

  Serial.println("[INFO] Wifi");
  Serial.printf("Connecting to %s ", mySSID);
  WiFi.begin(mySSID, myPASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println(" CONNECTED");

  Serial.println("[INFO] Time");
  waitForSync();
  Serial.println("  UTC: " + UTC.dateTime());
	localTimezone.setLocation(localTimezoneLocation);
  localTimezone.setDefault();
	Serial.println("  Local time: " + localTimezone.dateTime());
  setInterval(60 * 15); // sync every 15 minutes
  
  Serial.println("[INFO] Tasks");
  loadTasks();

  if (enableMotionSensor) {
    Serial.println("[INFO] Motion sensor");
    pinMode(pinPIR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pinPIR), detectsMovement, RISING);
    lastMotionDetectedMs = millis();
  }
  
  Serial.println("[INFO] Wordclock done booting. Hello World!");
  printMenu();
}

void loop() {
  unsigned long time = millis();
  for (int i = 0; i < noTasks; i++) {
    Task task = tasks[i];
    if (time - task.previous > task.interval) {
      tasks[i].previous = time;
      task.function();
    }
  }  

  // ezTime updates
  events();

  delay(wait);
}