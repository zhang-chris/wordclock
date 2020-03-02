// Wordclock firmware
// ==================
// November 2013 - August 2014
// by Wouter Devinck

// Dependencies:
//  * Arduino libraries                - http://arduino.cc/
//  * Chronodot library (for DS3231)   - https://github.com/Stephanie-Maks/Arduino-Chronodot
//  * LedControl library (for MAX7219) - http://playground.arduino.cc/Main/LedControl

/* Hardware block diagram:

              +-----------------+                            
              | Real time clock |                            
              | Maxim DS3231    |                            
              +--------+--------+                            
                       |I2C                                  
         +-------------+-------------+                       
         |                           |   +------------------+
         |                           +---+ 8x8 LED matrix 1 |
+---+    |                           |   | Maxim MAX7219    |
|LDR+----+                           |   +---------+--------+
+---+    |      Microcontroller      |             |         
         |      Atmel ATMEGA328      |   +---------+--------+
+------+ |      (with Arduino        |   | 8x8 LED matrix 2 |
|Buzzer+-+       bootloader)         |   | Maxim MAX7219    |
+------+ |                           |   +---------+--------+
         |                           |             |         
         |                           |   +---------+--------+
         +-++----++---------++----++-+   | 8x8 LED matrix 3 |
           ||    ||         ||    ||     | Maxim MAX7219    |
    +------++-+  ||  +------++-+  ||     +---------+--------+
    | Azoteq  |  ||  | Azoteq  |  ||               |         
    | IQS127D |  ||  | IQS127D |  ||     +---------+--------+
    +---------+  ||  +---------+  ||     | 8x8 LED matrix 4 |
                 ||               ||     | Maxim MAX7219    |
          +------++-+      +------++-+   +------------------+
          | Azoteq  |      | Azoteq  |                       
          | IQS127D |      | IQS127D |                       
          +---------+      +---------+                       

(created using http://asciiflow.com/) */


// Includes
#include <FastLED.h>
#include "Chronodot.h"  // DS3231  - Real time clock    - https://github.com/Stephanie-Maks/Arduino-Chronodot
#include <Wire.h>
#include <EEPROM.h>

// Pins to led drivers
const int pinData = 14; // A0 (used as digital pin)
const int pinClock = 16; // A2 (used as digital pin)

// Other pins (light sensor)
const int pinLDR = 3; // A3 (used as analog pin)

// Led strips
#define NUM_COLS = 11;
#define NUM_ROWS = 11;
#define NUM_MINUTES = 4; // LEDs for fine minute granularity
#define NUM_LEDS = (NUM_COLS * NUM_ROWS) + NUM_MINUTES;

CRGB leds[NUM_LEDS];
boolean ledsBuffer[NUM_LEDS];
int brightness; // Between 0 and 255

// The real time clock chip (DS3231)
Chronodot RTC;

// Tasks
const int wait = 10;
const int noTasks = 2;
typedef struct Tasks {
   long unsigned int previous;
   int interval;
   void (*function)();
} Task;
Task tasks[noTasks];

// Serial menu options
boolean mustReadBrightness = false;

// Words
// Format: { line index, start position index, length }

const int w_it[3] =        { 0,  0,  strlen("it") };
const int w_is[3] =        { 0,  3,  strlen("is") };
const int w_five[3] =      { 2,  7,  strlen("five") };
const int w_ten[3] =       { 3,  5,  strlen("ten") };
const int w_quarter[3] =   { 1,  2,  strlen("quarter") };
const int w_twenty[3] =    { 2,  0,  strlen("twenty") };
const int w_half[3] =      { 3,  0,  strlen("half") };
const int w_to[3] =        { 3,  9,  strlen("to") };
const int w_past[3] =      { 4,  0,  strlen("past") };
const int w_oclock[3] =    { 9,  5,  strlen("oclock") };

const int NUM_HOURS = 12
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

void setup() {
  // Debug info
  Serial.begin(9600);
  Serial.println("[INFO] Wordclock is booting...");
  
  // Read settings from EEPROM
  Serial.println("[INFO] 1. Read settings");
  brightness = EEPROM.read(0);
  // FastLed.setBrightness(brightness);
  
  // Initiate the LED drivers
  FastLED.addLeds<WS2812, pinData, GRB>(leds, NUM_LEDS);

  // Initiate the Real Time Clock
  Serial.println("[INFO] 3. Real time clock");
  Wire.begin();
  RTC.begin();
  if (!RTC.isrunning()) {
    Serial.println("[WARNING] RTC is NOT running!");
    RTC.adjust(DateTime(__DATE__, __TIME__));
  }
  
  // Tasks
  Serial.println("[INFO] 5. Tasks");
  loadTasks();
  
  // Debug info
  Serial.println("[INFO] Wordclock done booting. Hello World!");
  printMenu();
}

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

void loop() {
  unsigned long time = millis();
  for(int i = 0; i < noTasks; i++) {
    Task task = tasks[i];
    if (time - task.previous > task.interval) {
      tasks[i].previous = time;
      task.function();
    }
  }  
  delay(wait);
}

void serialMenu() {
  if (Serial.available() > 0) {
    if (mustReadBrightness) {
      int val = Serial.parseInt();
      if(val < 0 || val > 15) {
        Serial.println("[ERROR] Brightness must be between 0 and 15");
      } else {
        Serial.print("Brightness set to ");
        Serial.println(val, DEC);
        setBrightness(val);
        brightness = val;
        EEPROM.write(0, val);
      }
      mustReadBrightness = false;
      printMenu();
    } else {
      int in = Serial.read();
      if (in == 49) {
        Serial.println("You entered [1]");
        Serial.println("  Enter brightness (0-15)");
        mustReadBrightness = true;
      } else if (in == 50) {
        Serial.println("You entered [2]");
        Serial.print("  Brightness: ");
        Serial.println(brightness, DEC);
        printMenu();
      } else {
        Serial.println("[ERROR] Whut?");
        printMenu();
      }
    }
  }
}

void showTime() {
  
  // Get the time
  DateTime now = RTC.now();  
  int hour = now.hour();
  int hourToDisplay = hour;
  int minute = now.minute();
  
  // DEBUG
  Serial.print("[DEBUG] ");
  Serial.print(hour, DEC);
  Serial.print(':');
  Serial.println(minute, DEC);
  
  // Show "IT IS"
  displayWord(w_it);
  displayWord(w_is);
  
  // Minutes
  if (minute == 0) {
    displayWord(w_oclock);
  }
  else {
  	int floorMinute = (minute / 5) * 5

  	switch (floorMinute) {
  		case 0:
  			break;
		case 5:
  			displayWord(w_five);
  		case 10:
  			displayWord(w_ten);
		case 15:
  			displayWord(w_quarter);
  		case 20:
  			displayWord(w_twenty);
		case 25:
  			displayWord(w_twenty);
  			displayWord(w_five);
  		case 30:
  			displayWord(w_half);
		case 35:
  			displayWord(w_twenty);
  			displayWord(w_five);
  		case 40:
  			displayWord(w_twenty]);
		case 45:
  			displayWord(w_quarter);
  		case 50:
  			displayWord(w_ten);
		case 55:
  			displayWord(w_five);
  		default:
  			Serial.print("Invalid floorMinute: ");
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
  displayWord(w_minutes[minute % 5]);

  // Update display
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

void updateDisplayAndClearBuffer(int brightness) {
  for (int i = 0, i < NUM_LEDS, i++) {
    if (ledsBuffer[i] == true) {
      leds[i] = CRGB::White;
    } else {
      leds[i] = CRGB::Black;
    }

    // reset buffer
    ledBuffer[i] = false;
  }

  FastLED.setBrightness(brightness);
  FastLED.show();
}

void printMenu() {
  Serial.println("");
  Serial.println("Menu");
  Serial.println("----");
  Serial.println("  1. Set brightness");
  Serial.println("  2. Read brightness");
  Serial.println("");
}