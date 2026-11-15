# Car Black Box (Event Data Recorder)

An embedded C project for the **PIC16F877A**. It records driving events with the time and speed, and lets the user review them through a password-protected menu. Built in MPLAB X with the XC8 compiler and tested in PICSimLab.

## What it does
- **Dashboard** shows the time (DS1307 RTC), the current event and the speed read from an ADC channel.
- **Events** recorded: ON, neutral, gears 1–4, reverse and collision. Each one is saved with its time and speed in an external I2C EEPROM.
- **Password-protected menu** (3 attempts, then a 30-second lockout):
  - View log
  - Clear log
  - Download log over UART
  - Set time
  - Change password

## Hardware and peripherals
PIC16F877A, 16x2 character LCD (CLCD), digital keypad, DS1307 real-time clock, external EEPROM, UART, ADC input for speed.

## Source layout
| File | Purpose |
|------|---------|
| `main.c`, `main.h` | Start-up and main loop |
| `dashboard.c` | Dashboard screen, event handling, password entry |
| `main_menu.c` | Menu, log view/clear/download, set time, change password |
| `clcd.c`, `digital_keypad.c`, `adc.c`, `uart.c` | Peripheral drivers |
| `i2c.c`, `ds1307.c`, `external_eeprom.c` | I2C bus, RTC and EEPROM drivers |
| `timers.c`, `isr.c` | Timer0 and the interrupt service routine, used for the lockout and blinking fields |

## Build
Open the project in **MPLAB X IDE** (the `nbproject` folder is included), select the XC8 compiler and build. `Car Black Box.hex` is the compiled firmware and can be loaded in PICSimLab.

## Concepts used
Embedded C, ADC, I2C, UART, Timer0 interrupts, external EEPROM record storage, menu-driven state machine on a character LCD.
