/*
  Desktop Focus Timer
  Arduino Mega 2560

  Hardware:
    LCD1602 (4-bit mode)      -> pins 12(RS),11(E),5(D4),4(D5),3(D6),2(D7)
    Start/Pause button        -> pin 6  (INPUT_PULLUP)
    Reset button               -> pin 7  (INPUT_PULLUP)
    Mode button                -> pin 8  (INPUT_PULLUP)
    Passive buzzer              -> pin 9
    RGB LED (common cathode)   -> R=44, G=45, B=46
    LCD contrast (V0)          -> fixed resistor divider (~8.5k to 5V, ~1k to GND) - no pin used

  Behavior:
    IDLE          -> LCD shows focus/break lengths + sessions completed today,
                     cycle options with Mode, press Start to begin
    RUNNING_FOCUS -> red LED (slow breathing effect), countdown, auto-advances to break when done
    RUNNING_BREAK -> green LED (solid), countdown, auto-returns to IDLE when done
    PAUSED        -> Start/Pause resumes with remaining time saved
    Reset (short press) -> skips to the next phase immediately
    Reset (held 1s+)    -> full reset back to IDLE, cancels current session

  Last-used session option is saved to EEPROM and restored on power-up.
  Session counter resets on power-up (not saved, since it's meant to track "today").
*/

#include <LiquidCrystal.h>
#include <EEPROM.h>

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

const int startPin = 6;
const int resetPin = 7;
const int modePin = 8;
const int buzzerPin = 9;
const int redPin = 44;
const int greenPin = 46; // physical wiring swap accounted for
const int bluePin = 45;  // physical wiring swap accounted for

const unsigned long DEBOUNCE_MS = 50;
const unsigned long REPEAT_LOCKOUT_MS = 200;
const unsigned long LONG_PRESS_MS = 1000; // hold Reset this long for a full reset
const int EEPROM_OPTION_ADDR = 0;

enum State { IDLE, RUNNING_FOCUS, RUNNING_BREAK, PAUSED };
State currentState = IDLE;

const int focusOptions[] = {25, 15, 50};
const int breakOptions[] = {5, 5, 10};
const int NUM_OPTIONS = 3;
int optionIndex = 0;
int sessionsCompleted = 0;

unsigned long sessionLengthMs;
unsigned long startTime;
unsigned long remainingMs;
bool wasFocusBeforePause = true;

// ---- non-blocking debounce state ----
struct Button {
  int pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeTime;
  unsigned long lastAcceptedTime;
  bool longPressFired;
};

Button startBtn = {startPin, HIGH, HIGH, 0, 0, false};
Button resetBtn = {resetPin, HIGH, HIGH, 0, 0, false};
Button modeBtn  = {modePin,  HIGH, HIGH, 0, 0, false};

// returns true exactly once, the moment a debounced press (HIGH->LOW) is accepted
bool checkPressed(Button &b) {
  bool reading = digitalRead(b.pin);
  unsigned long now = millis();

  if (reading != b.lastReading) {
    b.lastChangeTime = now;
    b.lastReading = reading;
  }

  if ((now - b.lastChangeTime) > DEBOUNCE_MS && b.stableState != reading) {
    b.stableState = reading;
    if (b.stableState == LOW && (now - b.lastAcceptedTime) > REPEAT_LOCKOUT_MS) {
      b.lastAcceptedTime = now;
      b.longPressFired = false;
      return true;
    }
  }
  return false;
}

// returns true exactly once, when a currently-held button crosses the long-press threshold
bool checkLongPress(Button &b) {
  if (b.stableState == LOW && !b.longPressFired) {
    if (millis() - b.lastChangeTime > LONG_PRESS_MS) {
      b.longPressFired = true;
      return true;
    }
  }
  return false;
}

// ---- non-blocking buzzer melody player ----
int melodyNotes[8];
int melodyDurations[8];
int melodyLength = 0;
int melodyIndex = 0;
unsigned long melodyNoteStart = 0;
bool melodyPlaying = false;

void playMelody(int *notes, int *durations, int length) {
  for (int i = 0; i < length; i++) {
    melodyNotes[i] = notes[i];
    melodyDurations[i] = durations[i];
  }
  melodyLength = length;
  melodyIndex = 0;
  melodyPlaying = true;
  melodyNoteStart = millis();
  tone(buzzerPin, melodyNotes[0], melodyDurations[0]);
}

void updateMelody() {
  if (!melodyPlaying) return;
  if (millis() - melodyNoteStart >= (unsigned long)melodyDurations[melodyIndex] + 30) {
    melodyIndex++;
    if (melodyIndex >= melodyLength) {
      melodyPlaying = false;
      return;
    }
    melodyNoteStart = millis();
    tone(buzzerPin, melodyNotes[melodyIndex], melodyDurations[melodyIndex]);
  }
}

// ---- non-blocking breathing LED (red channel pulse during focus) ----
unsigned long breatheStart = 0;
const unsigned long BREATHE_PERIOD_MS = 3000;

void updateBreathingRed() {
  unsigned long t = (millis() - breatheStart) % BREATHE_PERIOD_MS;
  float phase = (float)t / BREATHE_PERIOD_MS; // 0..1
  float level = (1.0 - cos(phase * 2.0 * PI)) / 2.0; // 0..1 smooth pulse
  int brightness = 60 + (int)(level * 195); // floor at 60 so it never fully goes dark
  analogWrite(redPin, brightness);
  analogWrite(greenPin, 0);
  analogWrite(bluePin, 0);
}

void setup() {
  Serial.begin(9600);
  pinMode(startPin, INPUT_PULLUP);
  pinMode(resetPin, INPUT_PULLUP);
  pinMode(modePin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);

  int savedOption = EEPROM.read(EEPROM_OPTION_ADDR);
  if (savedOption >= 0 && savedOption < NUM_OPTIONS) {
    optionIndex = savedOption;
  }

  lcd.begin(16, 2);
  lcd.print("Focus Timer");
  delay(1000);
  showIdleScreen();
}

void loop() {
  bool startPressed = checkPressed(startBtn);
  bool resetPressed = checkPressed(resetBtn);
  bool modePressed  = checkPressed(modeBtn);
  bool resetLongPress = checkLongPress(resetBtn);

  updateMelody();

  if (resetLongPress) {
    // full reset, cancels whatever was happening
    currentState = IDLE;
    setColor(0, 0, 0);
    tone(buzzerPin, 300, 200);
    showIdleScreen();
  } else if (resetPressed && currentState != IDLE) {
    // short press while running/paused: skip to next phase
    skipToNextPhase();
  } else if (resetPressed && currentState == IDLE) {
    // short press while idle: just a gentle no-op beep, nothing to skip
    tone(buzzerPin, 400, 80);
  }

  if (modePressed && currentState == IDLE) {
    optionIndex = (optionIndex + 1) % NUM_OPTIONS;
    EEPROM.update(EEPROM_OPTION_ADDR, optionIndex);
    showIdleScreen();
  }

  if (startPressed) {
    handleStartPause();
  }

  if (currentState == RUNNING_FOCUS || currentState == RUNNING_BREAK) {
    updateCountdown();
    if (currentState == RUNNING_FOCUS) {
      updateBreathingRed();
    }
  }
}

void handleStartPause() {
  if (currentState == IDLE) {
    beginFocusSession();
  } else if (currentState == RUNNING_FOCUS || currentState == RUNNING_BREAK) {
    unsigned long elapsed = millis() - startTime;
    remainingMs = (elapsed < sessionLengthMs) ? (sessionLengthMs - elapsed) : 0;
    wasFocusBeforePause = (currentState == RUNNING_FOCUS);
    currentState = PAUSED;
    setColor(0, 0, 0);
    tone(buzzerPin, 600, 150);
    lcd.setCursor(0, 0);
    lcd.print("PAUSED          ");
  } else if (currentState == PAUSED) {
    sessionLengthMs = remainingMs;
    startTime = millis();
    currentState = wasFocusBeforePause ? RUNNING_FOCUS : RUNNING_BREAK;
    if (wasFocusBeforePause) {
      breatheStart = millis();
    } else {
      setColor(0, 255, 0);
    }
    tone(buzzerPin, 1000, 150);
  }
}

void beginFocusSession() {
  sessionLengthMs = (unsigned long)focusOptions[optionIndex] * 60000UL;
  startTime = millis();
  currentState = RUNNING_FOCUS;
  breatheStart = millis();
  tone(buzzerPin, 1000, 150);
}

void beginBreakSession() {
  sessionLengthMs = (unsigned long)breakOptions[optionIndex] * 60000UL;
  startTime = millis();
  currentState = RUNNING_BREAK;
  setColor(0, 255, 0);
}

// short-press Reset while running: jump straight to the next phase
void skipToNextPhase() {
  bool inFocus = (currentState == RUNNING_FOCUS) ||
                 (currentState == PAUSED && wasFocusBeforePause);
  if (inFocus) {
    sessionsCompleted++;
    beginBreakSession();
    tone(buzzerPin, 800, 100);
  } else {
    currentState = IDLE;
    setColor(0, 0, 0);
    tone(buzzerPin, 500, 150);
    showIdleScreen();
  }
}

void updateCountdown() {
  unsigned long elapsed = millis() - startTime;
  long remaining = sessionLengthMs - elapsed;

  if (remaining <= 0) {
    if (currentState == RUNNING_FOCUS) {
      sessionsCompleted++;
      beginBreakSession();
      int notes[] = {880, 988, 1175};
      int durs[]  = {100, 100, 150};
      playMelody(notes, durs, 3);
    } else {
      currentState = IDLE;
      setColor(0, 0, 0);
      int notes[] = {1175, 988, 784, 1047};
      int durs[]  = {100, 100, 100, 250};
      playMelody(notes, durs, 4);
      showIdleScreen();
    }
    return;
  }

  int totalSeconds = remaining / 1000;
  int mins = totalSeconds / 60;
  int secs = totalSeconds % 60;

  lcd.setCursor(0, 0);
  lcd.print(currentState == RUNNING_FOCUS ? "FOCUS           " : "BREAK           ");
  lcd.setCursor(0, 1);
  if (mins < 10) lcd.print("0");
  lcd.print(mins);
  lcd.print(":");
  if (secs < 10) lcd.print("0");
  lcd.print(secs);
  lcd.print("        ");
}

void showIdleScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Focus:");
  lcd.print(focusOptions[optionIndex]);
  lcd.print("m Brk:");
  lcd.print(breakOptions[optionIndex]);
  lcd.setCursor(0, 1);
  lcd.print("Sessions: ");
  lcd.print(sessionsCompleted);
}

void setColor(int r, int g, int b) {
  analogWrite(redPin, r);
  analogWrite(greenPin, g);
  analogWrite(bluePin, b);
}
