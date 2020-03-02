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

#include <SoftwareSerial.h>
SoftwareSerial s(3,1);


// Pins to capacitive touch chips (touch and presence for each of the Azoteq IQS127D chips in the four corners)
const int pinTRB = 9;   // Touch Right Bottom
const int pinTRT = 3;   // Touch Right Top
const int pinTLT = 4;   // Touch Left Top
const int pinTLB = 10;  // Touch Left Bottom
// The presence pins are connected in hardware, but not used in this firmware.
// Leaving the pin numbers in as comments for future reference.
//const int pinPRB = 5; // Presence Right Bottom
//const int pinPRT = 8; // Presence Right Top
//const int pinPLT = 7; // Presence Left Bottom
//const int pinPLB = 6; // Presence Left Top

// Pins to led drivers
const int pinData = 14; // A0 (used as digital pin)
const int pinLoad = 15; // A1 (used as digital pin)
const int pinClock = 16; // A2 (used as digital pin)

// Other pins (buzzer and light sensor)
const int pinBuzzer = 2;
const int pinLDR = 3; // A3 (used as analog pin)

// Led strips
#define NUM_LEDS = 196;
#define NUM_COLS = 14;

CRGB leds[NUM_LEDS];
boolean ledsBuffer[NUM_LEDS];
int brightness; // Between 0 and 255

// The real time clock chip (DS3231)
Chronodot RTC;

// Tasks
const int wait = 10;
const int noTasks = 3;
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

const int w_it[3] =        { 0,  0,  2 };
const int w_is[3] =        { 0,  3,  2 };
const int w_half[3] =      { 7,  9,  4 };
const int w_to[3] =        { 8,  8,  2 };
const int w_past[3] =      { 8,  10, 4 };
const int w_oclock[3] =    { 13, 8,  6 };
const int w_noon[3] =      { 10, 5,  4 };
const int w_midnight[3] =  { 13, 0,  8 };

const int w_minutes[20][3] = {
  { 1,  0,  3 }, // one
  { 1,  4,  3 }, // two
  { 1,  8,  5 }, // three
  { 2,  1,  4 }, // four
  { 2, 10,  4 }, // five
  { 3,  0,  3 }, // six
  { 4,  0,  5 }, // seven
  { 6,  2,  5 }, // eight
  { 7,  1,  4 }, // nine
  { 4, 10,  3 }, // ten
  { 3,  8,  6 }, // eleven
  { 5,  0,  6 }, // twelve
  { 5,  6,  8 }, // thirteen
  { 2,  1,  8 }, // fourteen
  { 8,  0,  7 }, // quarter
  { 3,  0,  7 }, // sixteen
  { 4,  0,  9 }, // seventeen
  { 6,  2,  8 }, // eighteen
  { 7,  1,  8 }, // nineteen
  { 0,  6,  6 }  // twenty
};

const int w_hours[12][3] = {
  { 9,  0,  3 }, // one
  { 9,  3,  3 }, // two
  { 10, 0,  5 }, // three
  { 9,  6,  4 }, // four
  { 9, 10,  4 }, // five
  { 11, 0,  3 }, // six
  { 11, 4,  5 }, // seven
  { 10, 9,  5 }, // eight
  { 12, 2,  4 }, // nine
  { 11,10,  3 }, // ten
  { 12, 8,  4 }, // eleven
};

// Touch
boolean tlt;
boolean trt;
boolean tlb;
boolean trb;

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
  if (! RTC.isrunning()) {
    Serial.println("[WARNING] RTC is NOT running!");
    RTC.adjust(DateTime(__DATE__, __TIME__));
  }
  
  // Initiate the capacitive touch inputs
  Serial.println("[INFO] 4. Capacitive touch");
  pinMode(pinTRB, INPUT);
  pinMode(pinTRT, INPUT);
  pinMode(pinTLT, INPUT);
  pinMode(pinTLB, INPUT);
  //pinMode(pinPRB, INPUT);
  //pinMode(pinPRT, INPUT);
  //pinMode(pinPLT, INPUT);
  //pinMode(pinPLB, INPUT);
  
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

  // Read the touch inputs
  tasks[2].previous = 0;
  tasks[2].interval = 100;
  tasks[2].function = readTouch;
  
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
    if (hour != 0 && hour != 12) {
      displayWord(w_oclock);
    }
  }
  else {
    if (minute <= 20) {
      displayWord(w_minutes[minute - 1]);
    } else if (minute < 30) {
      displayWord(w_minutes[19]); // twenty
      displayWord(w_minutes[minute - 21]);
    } else if (minute == 30) {
      displayWord(w_half);
    } else if (minute < 40) {
      displayWord(w_minutes[19]); // twenty
      displayWord(w_minutes[60 - minute - 21]);
    } else {
      displayWord(w_minutes[60 - minute - 1]);
    }
 
    if (minute <= 30) {
      displayWord(w_past);
    } else {
      displayWord(w_to);
      hourToDisplay++;
    }
    
  } 
  
  if (hour == 0) {
    displayWord(w_midnight);
  } else if (hour == 12) {
    displayWord(w_noon);
  } else {
    // Hours
    if (hourToDisplay < 12) {
      displayWord(w_hours[hourToDisplay - 1]);
    } else {
      displayWord(w_hours[hourToDisplay - 13]);
    }
  }

  // Update display
  updateDisplayAndClearBuffer();
}

void readTouch() {
  boolean lt = debounce(digitalRead(pinTLT) == LOW, &tlt);
  boolean rt = debounce(digitalRead(pinTRT) == LOW, &trt);
  boolean lb = debounce(digitalRead(pinTLB) == LOW, &tlb);
  boolean rb = debounce(digitalRead(pinTRB) == LOW, &trb);
  if(lt || rt || lb || rb) {
    tone(pinBuzzer, 500, 100);
  }
}

boolean debounce(boolean value, boolean* store) {
  if(value) {
    if(*store) {
      value = false;
    } else {
      *store = true;
    }
  } else {
    *store = false;
  }
  return value;
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