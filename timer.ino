#include <LiquidCrystal.h>
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

const int startPin = 6;
const int resetPin = 7;
const int modePin = 8;
const int buzzerPin = 9;
const int redPin = 44;
const int bluePin = 45;  // swapped per earlier fix
const int greenPin = 46; // swapped per earlier fix

enum State { IDLE, RUNNING_FOCUS, RUNNING_BREAK, PAUSED };
State currentState = IDLE;

// session lengths in minutes, cycled by Mode button while IDLE
const int focusOptions[] = {25, 15, 50};
const int breakOptions[] = {5, 5, 10};
int optionIndex = 0;

unsigned long sessionLengthMs;
unsigned long startTime;
unsigned long remainingMs; // used when pausing/resuming

bool lastStartState = HIGH;
bool lastResetState = HIGH;
bool lastModeState = HIGH;
bool wasFocusBeforePause = true;

void setup() {
  Serial.begin(9600);
  pinMode(startPin, INPUT_PULLUP);
  pinMode(resetPin, INPUT_PULLUP);
  pinMode(modePin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);

  lcd.begin(16, 2);
  showIdleScreen();
}

void loop() {
  bool startState = digitalRead(startPin);
  bool resetState = digitalRead(resetPin);
  bool modeState = digitalRead(modePin);

  // Reset button - returns to IDLE from anywhere
  if (resetState == LOW && lastResetState == HIGH) {
    currentState = IDLE;
    setColor(0, 0, 0);
    showIdleScreen();
    delay(200); // debounce
  }

  // Mode button - only cycles options while IDLE
  if (modeState == LOW && lastModeState == HIGH && currentState == IDLE) {
    optionIndex = (optionIndex + 1) % 3;
    showIdleScreen();
    delay(200);
  }

  // Start/Pause button
  if (startState == LOW && lastStartState == HIGH) {
    handleStartPause();
    delay(200);
  }

  // Update countdown if running
  if (currentState == RUNNING_FOCUS || currentState == RUNNING_BREAK) {
    updateCountdown();
  }

  lastStartState = startState;
  lastResetState = resetState;
  lastModeState = modeState;
}

void handleStartPause() {
  if (currentState == IDLE) {
    sessionLengthMs = (unsigned long)focusOptions[optionIndex] * 60000UL;
    startTime = millis();
    currentState = RUNNING_FOCUS;
    setColor(255, 0, 0); // red = focus
    tone(buzzerPin, 1000, 150);
  } else if (currentState == RUNNING_FOCUS || currentState == RUNNING_BREAK) {
    // pause: save how much time was left
    unsigned long elapsed = millis() - startTime;
    remainingMs = (elapsed < sessionLengthMs) ? (sessionLengthMs - elapsed) : 0;
    currentState = PAUSED;
    setColor(0, 0, 0);
    tone(buzzerPin, 600, 150);
  } else if (currentState == PAUSED) {
    // resume with remaining time
    sessionLengthMs = remainingMs;
    startTime = millis();
    // restore correct color depending on what was paused
    currentState = wasFocusBeforePause ? RUNNING_FOCUS : RUNNING_BREAK;
    setColor(wasFocusBeforePause ? 255 : 0, wasFocusBeforePause ? 0 : 255, 0);
    tone(buzzerPin, 1000, 150);
  }
}


void updateCountdown() {
  unsigned long elapsed = millis() - startTime;
  long remaining = sessionLengthMs - elapsed;

  if (remaining <= 0) {
    // session complete - transition
    if (currentState == RUNNING_FOCUS) {
      wasFocusBeforePause = false;
      sessionLengthMs = (unsigned long)breakOptions[optionIndex] * 60000UL;
      startTime = millis();
      currentState = RUNNING_BREAK;
      setColor(0, 255, 0); // green = break
      tone(buzzerPin, 800, 100);
      delay(120);
      tone(buzzerPin, 800, 100);
    } else {
      wasFocusBeforePause = true;
      currentState = IDLE;
      setColor(0, 0, 0);
      tone(buzzerPin, 1200, 300);
      showIdleScreen();
    }
    return;
  }

  int totalSeconds = remaining / 1000;
  int mins = totalSeconds / 60;
  int secs = totalSeconds % 60;

  lcd.setCursor(0, 0);
  lcd.print(currentState == RUNNING_FOCUS ? "FOCUS   " : "BREAK   ");
  lcd.setCursor(0, 1);
  lcd.print(mins < 10 ? "0" : ""); lcd.print(mins);
  lcd.print(":");
  lcd.print(secs < 10 ? "0" : ""); lcd.print(secs);
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
  lcd.print("Press Start");
}

void setColor(int r, int g, int b) {
  analogWrite(redPin, r);
  analogWrite(greenPin, g);
  analogWrite(bluePin, b);
}