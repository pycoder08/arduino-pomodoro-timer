# Arduino Pomodoro Timer

A desktop focus timer I built around an Arduino Mega, a 16x2 LCD, three push buttons, a buzzer, and an RGB LED. It runs configurable focus/break cycles and signals each transition with color and sound.

![Breadboard prototype with Mega 2560, LCD, buttons, buzzer, and RGB LED](prototype.jpg)

## Status

The breadboard prototype works — I've run the timer through its focus and break cycles on it. The schematic and PCB layout are drawn in KiCad and included here. I'm currently soldering the circuit onto an Arduino protoshield.

I have not fabricated the custom PCB yet, so the layout is a work in progress rather than something I'd send to a fab house today. Routing and design-rule checks still need a pass.

## Features

- Three focus/break presets: 25/5, 15/5, and 50/10 minutes
- Start, pause, and resume, with the remaining time preserved across a pause
- Tap Reset to skip to the next phase; hold it for a second to cancel the session outright
- Preset selection with the mode button while idle, remembered in EEPROM across power cycles
- Count of sessions finished since power-on, shown on the idle screen
- Countdown on a 16x2 LCD
- Red RGB LED breathing during focus, solid green during breaks
- Multi-note buzzer melodies at session transitions

Button handling, the melodies, and the LED breathing are all non-blocking, so the countdown
stays accurate and the buttons stay responsive no matter what else is happening.

## Hardware

Arduino Mega 2560, 16x2 LCD, three push buttons, a passive buzzer, a common-cathode RGB LED,
resistors, breadboard, and jumper wire. LCD contrast comes from a fixed divider (roughly 8.5k to
5V and 1k to ground) rather than a trim potentiometer, so no pin is spent on it.

The sketch drives the RGB LED on pins 44–46. Those are PWM-capable on the Mega, which the
breathing effect needs, and they don't exist on a Uno — remap them if you build this on a smaller
board. I still need to document exact component models and resistor values.

### Pin assignments

| Function | Arduino pin |
| --- | --- |
| LCD RS | 12 |
| LCD enable | 11 |
| LCD D4–D7 | 5, 4, 3, 2 |
| Start / pause | 6 |
| Reset | 7 |
| Mode | 8 |
| Buzzer | 9 |
| RGB red | 44 |
| RGB blue | 45 |
| RGB green | 46 |

Buttons are wired active-low using `INPUT_PULLUP` and are debounced in software. This table covers
the software pin assignment only — check LCD contrast wiring, your RGB LED's common pin, and
current-limiting resistors against your own build.

## Running it

1. Open `timer.ino` in the Arduino IDE, letting it create the enclosing `timer` folder if prompted.
2. Make sure the `LiquidCrystal` library is installed. `EEPROM` ships with the IDE.
3. Select your board and port, confirming the pins above exist on it.
4. Compile and upload.

## KiCad files

`Timer.kicad_pro` is the main project, with `Timer.kicad_sch` for the schematic and `Timer.kicad_pcb` for the layout. `Arduino_MountingHole.pretty` holds the custom mounting-hole footprints the board references.

`focus_timer_schematic.kicad_sch` is an earlier sheet covering just the Arduino header connections, kept for reference.

## A note on the code

I wrote this project's firmware with AI assistance. The circuit design, breadboard build and testing, schematic capture, PCB layout, and protoshield assembly are my own work.

## To do

- Finish the protoshield soldering and retest every control
- Document component models and resistor values
- Run design-rule checks on the PCB before fabricating
- Test all three presets, pause/resume in both phases, tap and hold Reset, and the full
  focus-to-break transition
- Persist the session count across power cycles, or roll it over on a real clock rather than at
  power-on
