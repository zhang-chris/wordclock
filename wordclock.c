// Wordclock firmware

#define ESP32_RTOS // for ota capability
#define ESP32

#include <FastLED.h>
#include <WiFi.h>
#include <ezTime.h>
#include <credentials.h>
#include "OTATelnetStream.h"

const int PIN_LED_DATA = 15;
const int PIN_LED_CLOCK = 32;
const int PIN_LIGHT = 33;
const int PIN_MOTION = 27;

// NTP clock server
Timezone localTimezone;
const char* LOCAL_TIMEZONE_LOCATION = "America/Los_Angeles";

// Led strips
const int NUM_COLS = 11;
const int NUM_ROWS = 10;
const int NUM_MINUTES = 4; // LEDs for fine minute granularity
const int NUM_LEDS = (NUM_COLS * NUM_ROWS) + NUM_MINUTES;
const boolean SNAKE = true; // snake LEDs for cleaner wiring; odd numbered rows are reversed
const int START_POS = 0;

CRGB leds[NUM_LEDS];
boolean leds_buffer[NUM_LEDS];

// Brightness and motion
boolean readManualOverrideBrightness = false;
int manualOverrideBrightness = -1;
const int LIGHT_BUFFER_SIZE = 8;
int lightBuffer[LIGHT_BUFFER_SIZE];
int lightBufferIndex = 0;
const int FADE_STEPS = 20;
const int MS_IN_S = 1000;
const int MIN_BRIGHTNESS = 10;
const int MAX_BRIGHTNESS = 70;
const boolean LOG_BRIGHTNESS = false;

const boolean ENABLE_MOTION_SENSOR = true;
boolean lastMotion;
const long NO_MOTION_THRESHOLD_DAY_MS = 30 * 60 * 1000; // 30 minutes
const long NO_MOTION_THRESHOLD_NIGHT_MS = 5 * 60 * 1000; // 5 minutes
unsigned long lastMotionDetectedMs;
boolean logLedSleep = true;

const boolean DISPLAY_IT_IS = true;

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
  { 6,  4,  strlen("two") },
  { 5,  6,  strlen("three") },
  { 6,  0,  strlen("four") },
  { 6,  7,  strlen("five") },
  { 5,  3,  strlen("six") },
  { 8,  0,  strlen("seven") },
  { 7,  0,  strlen("eight") },
  { 4,  7,  strlen("nine") },
  { 9,  0,  strlen("ten") },
  { 7,  5,  strlen("eleven") },
  { 8,  5,  strlen("twelve") }
};

// special ordering because of wiring
const int w_minutes[NUM_MINUTES + 1][3] = {
  { -1,  -1,  -1 }, // filler element so minute matches index position
  { 10,  3,  1 },
  { 10,  2,  1 },
  { 10,  0,  1 },
  { 10,  1,  1 }
};

void serialMenu() {
  if (Serial.peek() == 10) { // ignore new line
    Serial.read();
  }
  if (Serial.available() > 0) {
    if (readManualOverrideBrightness) {
      if (Serial.available()) {
        int val = Serial.parseInt();
        if (val < -1 || val > 255) {
          TelnetStream.println("[ERROR] Brightness must be between -1 and 255");
        } else {
          TelnetStream.print("Brightness set to ");
          TelnetStream.println(val, DEC);
          manualOverrideBrightness = val;
        }
        readManualOverrideBrightness = false;
        printMenu();
      }
    } else {
      int in = Serial.read();
      if (in == 49) { // ascii 1 = byte 49
        TelnetStream.println("You entered [1]");
        TelnetStream.println("  Enter brightness (0-255):");
        readManualOverrideBrightness = true;
      } else if (in == 50) {
        TelnetStream.println("You entered [2]");
        TelnetStream.print("  Brightness: ");
        TelnetStream.println(manualOverrideBrightness, DEC);
        printMenu();
      } else if (in == 51) {
        TelnetStream.println("You entered [3]");
        TelnetStream.println("  Beginning simulation.");
        
        simulateClock();
        printMenu();
      } else if (in == 10) {

      } else {
        TelnetStream.print("[ERROR] Invalid input: ");
        TelnetStream.println(in);
        printMenu();
      }
    }
  }
}

void showTime() {
  showTime(hour(), minute());
}

void showTime(int hour, int minute) {
  int hourToDisplay = hour;
  
  // DEBUG
  TelnetStream.print("[DEBUG] ");
  TelnetStream.print(hour, DEC);
  TelnetStream.print(':');
  TelnetStream.println(minute, DEC);
  
  // "IT IS"
  if (DISPLAY_IT_IS) {
    displayWord(w_it);
    displayWord(w_is);
  }
  
  // Minutes
  if (minute >= 0 && minute <= 4) {
    displayWord(w_oclock);
  } else {
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
  			TelnetStream.print("[ERROR] Invalid floorMinute: ");
        TelnetStream.println(floorMinute, DEC);
  	}
 
    if (minute <= 34) {
      displayWord(w_past);
    } else {
      displayWord(w_to);
      hourToDisplay++;
    }
  } 
  
  // Hours
  if (hourToDisplay == 0) {
    hourToDisplay = 12;
  } else if (hourToDisplay > 12) {
    hourToDisplay -= 12;
  }
  displayWord(w_hours[hourToDisplay]);

  // Fine minute granularity
  int fineMinute = minute % 5;
  if (fineMinute > 0) {
    for (int i = 1; i <= fineMinute; i++) {
      displayWord(w_minutes[i]);
    }
  }

  updateDisplayAndClearBuffer();
}

void displayWord(const int word[3]){
  int row = word[0];
  int col = word[1];
  int length = word[2];

  for (int i = 0; i < length; i++) {
    int ledNum = convertFrom2DTo1D(row, col + i);
    leds_buffer[ledNum] = true;
  }
}

int convertFrom2DTo1D(int row, int col) {
  if (SNAKE && (row % 2 == 1)) {
    return (row * NUM_COLS) + (NUM_COLS - 1 - col) + START_POS;
  }
  return (row * NUM_COLS) + col + START_POS;
}

void updateDisplayAndClearBuffer() {
  for (int i = 0; i < NUM_LEDS; i++) {
    if (leds_buffer[i] == true) {
      leds[i] = CRGB::White;
    } else {
      leds[i] = CRGB::Black;
    }

    // reset buffer
    leds_buffer[i] = false;
  }

  FastLED.show();
}

void readLight() {
  int lightValue = analogRead(PIN_LIGHT);

  lightBuffer[lightBufferIndex] = lightValue;
  lightBufferIndex = (lightBufferIndex + 1) % LIGHT_BUFFER_SIZE;
}

void setBrightness() {
  int brightness = calculateBrightness();
  
  long noMotionThreshold = (hour() <= 8) ? NO_MOTION_THRESHOLD_NIGHT_MS : NO_MOTION_THRESHOLD_DAY_MS;

  if (ENABLE_MOTION_SENSOR && (millis() - lastMotionDetectedMs > noMotionThreshold)) {
    if (logLedSleep) {
      TelnetStream.println("Sleeping LEDs.");
      logLedSleep = false;
    }
    
    brightness = 0;
  } else {
    logLedSleep = true;
  }


  smoothToBrightness(brightness);
}

void smoothToBrightness(int brightness) {
  int currentBrightness = FastLED.getBrightness();

  if (currentBrightness == brightness) {
    return;
  }

  int difference = brightness - currentBrightness;
  int delta = difference / FADE_STEPS;

  if (delta == 0) {
    delta = (difference < 0) ? -1 : 1;
  }

  while (currentBrightness != brightness) {
    currentBrightness += delta;

    if (currentBrightness < MIN_BRIGHTNESS) {
      currentBrightness = MIN_BRIGHTNESS;
    }
    if (currentBrightness > MAX_BRIGHTNESS) {
      currentBrightness = MAX_BRIGHTNESS;
    }
    
    FastLED.setBrightness(currentBrightness);
    FastLED.show();

    if (currentBrightness == MIN_BRIGHTNESS || currentBrightness == MAX_BRIGHTNESS) {
      break;
    }

    delay(MS_IN_S / FADE_STEPS);
  }
}

// TODO: empirical testing required to determine proper brightness function
int calculateBrightness() {
  
  double averageLight = getAverageLight();
  int brightness = constrain(map(averageLight, 0, 3000, MIN_BRIGHTNESS, MAX_BRIGHTNESS), MIN_BRIGHTNESS, MAX_BRIGHTNESS);

  if (LOG_BRIGHTNESS) {
    TelnetStream.print("Average LDR: ");
    TelnetStream.println(averageLight, DEC);
    TelnetStream.print("Brightness: ");
    TelnetStream.println(brightness, DEC);
    TelnetStream.println("");
  }

  return brightness;
}

double getAverageLight() {
  double sum = 0;
  for (int i = 0; i < LIGHT_BUFFER_SIZE; i++) {
    sum += lightBuffer[i];
  }
  return sum / LIGHT_BUFFER_SIZE;
}

void checkMotion() {
  boolean motion = digitalRead(PIN_MOTION);
  if (motion != lastMotion) {
    TelnetStream.println("Motion change detected");
    lastMotion = motion;
    lastMotionDetectedMs = millis();
  }
}

// iterate through all possible times
void simulateClock() {
  for (int i = 0; i < 12; i++) {
    for (int j = 0; j < 60; j++) {
      showTime(i, j);
      delay(500);
    }
  }
}

void printMenu() {
  TelnetStream.println("");
  TelnetStream.println("Menu");
  TelnetStream.println("----");
  TelnetStream.println("  1. Set brightness override");
  TelnetStream.println("  2. Read brightness override");
  TelnetStream.println("  3. Simulate for testing");
  TelnetStream.println("");
}

void setup() {
  Serial.begin(9600);
  delay(500);

  TelnetStream.println("[INFO] Wordclock is booting...");

  TelnetStream.println("[INFO] OTA");
  setupOTA("wordclock", mySSID, myPASSWORD);
  delay(1000);
  
  TelnetStream.println("[INFO] LEDs");
  FastLED.addLeds<SK9822, PIN_LED_DATA, PIN_LED_CLOCK, BGR>(leds, NUM_LEDS);
  FastLED.setBrightness(MAX_BRIGHTNESS);
  set_max_power_in_volts_and_milliamps(5, 500); 

  TelnetStream.println("[INFO] Wifi");
  TelnetStream.printf("Connecting to %s ", mySSID);
  WiFi.begin(mySSID, myPASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      TelnetStream.print(".");
  }
  TelnetStream.println(" CONNECTED");

  TelnetStream.println("[INFO] Time");
  waitForSync();
  TelnetStream.println("  UTC: " + UTC.dateTime());
	localTimezone.setLocation(LOCAL_TIMEZONE_LOCATION);
  localTimezone.setDefault();
	TelnetStream.println("  Local time: " + localTimezone.dateTime());
  setInterval(60 * 15); // sync every 15 minutes
  
  // TelnetStream.println("[INFO] Tasks");
  // loadTasks();

  TelnetStream.println("[INFO] Motion sensor");
  pinMode(PIN_MOTION, INPUT);
  lastMotionDetectedMs = millis();

  TelnetStream.println("[INFO] Wordclock done booting. Hello World!");
  printMenu();
}

void loop() {
  // while (true) {
  //   simulateClock();
  // }

  // Show time
  EVERY_N_SECONDS(1) {
    showTime();
  }

  // ezTime updates
  EVERY_N_SECONDS(30) {
    events();
  }

  // Toggle motion detector
  EVERY_N_SECONDS(1) {
    checkMotion();
  }

  // Read light sensor
  EVERY_N_MILLISECONDS(250) {
    readLight();
  }

  // Adjust brightness
  EVERY_N_SECONDS(2) {
    setBrightness();
  }
}