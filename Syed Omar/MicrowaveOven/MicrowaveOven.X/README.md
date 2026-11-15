# Microwave Oven Controller

A microwave oven controller for the **PIC16F877A**, written in Embedded C with a 4x4 matrix keypad and a 16x4 character LCD. Built in MPLAB X with the XC8 compiler and tested in PICSimLab.

## Modes (shown on the LCD)
```
1. Micro
2. Grill
3. Convection   (set a temperature, limit 180 °C, then pre-heat)
4. Quick 30s
```
- **Micro / Grill:** enter the cooking time as `MM:SS`.
- **Convection:** enter the temperature, the oven pre-heats, then you set the time.
- **Quick 30s:** starts a 30-second cook immediately.
- **During cooking:** `4` Start, `5` Pause, `6` Stop. `*` clears an entry and `#` confirms it.
- When time runs out the display shows "TIME UP!!!".

## How it works
- **Timer2** generates a 1-second tick (20 MHz clock, prescaler 1:16, PR2 = 250, interrupt every 1.25 ms).
- A **door interlock** stops the fan and the screen timer and restores everything when the door closes.
- A **buzzer** and a **fan** are driven from port C (`RC1` buzzer, `RC2` fan); the door switch is on `RB3`.
- The program is a screen-flag state machine in `main.c` with the cooking logic in `micro_oven.c`.

## Files
| File | Purpose |
|------|---------|
| `main.c`, `main.h` | Start-up, pin definitions, main loop |
| `micro_oven.c` / `.h` | Menu, time and temperature entry, pre-heat, cooking |
| `matrix_keypad.c` / `.h` | Keypad driver |
| `clcd.c` / `.h` | LCD driver |
| `timers.c` / `.h`, `isr.c` | Timer setup and the interrupt routine |

## Build
Open `MicrowaveOven.X` in **MPLAB X IDE**, select XC8 and build. Load the resulting `.hex` in PICSimLab.

## Concepts used
Embedded C, Timer2 interrupts, matrix keypad scanning, character LCD, GPIO control, state machine.
