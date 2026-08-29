# Hardware Connections

## Purpose

This document defines the hardware connections used by the final automotive HVAC controller prototype.

The system is built around two ATmega328P microcontrollers operating at 8 MHz:

* **Master controller:** handles the 4×4 keypad and TWI command transmission.
* **Slave controller:** handles the LM35 temperature sensor, 16×2 LCD, exterior-temperature assignment button, HVAC control logic, PWM generation, and L293D motor driver.

The pin numbers shown below assume the **28-pin DIP version of the ATmega328P**. Other packages use different physical pin numbers even though the AVR port names remain the same.

---

## System Overview

```text
                     4×4 Keypad
                         │
                         ▼
               ┌─────────────────┐
               │ ATmega328P      │
               │ MASTER          │
               │ 8 MHz           │
               └────────┬────────┘
                        │
                  TWI / I²C
                    100 kHz
                        │
               ┌────────▼────────┐
               │ ATmega328P      │
               │ SLAVE           │
               │ 8 MHz           │
               └───┬────┬────┬──┘
                   │    │    │
                 LM35  LCD  L293D
                              │
                              ▼
                           DC Fan
```

Both microcontrollers must share a common ground reference.

---

# Master Controller

## 4×4 Keypad

The keypad is connected using four row lines and four column lines.

### Rows

| Keypad signal | AVR pin | DIP-28 pin | Function |
| ------------- | ------- | ---------: | -------- |
| R1            | PD7     |         13 | Row scan |
| R2            | PD6     |         12 | Row scan |
| R3            | PD5     |         11 | Row scan |
| R4            | PD4     |          6 | Row scan |

During keypad scanning, the firmware temporarily configures one row as a LOW output while the remaining rows remain inactive.

### Columns

| Keypad signal | AVR pin | DIP-28 pin | Function                    |
| ------------- | ------- | ---------: | --------------------------- |
| C1            | PC3     |         26 | Input with internal pull-up |
| C2            | PC2     |         25 | Input with internal pull-up |
| C3            | PC1     |         24 | Input with internal pull-up |
| C4            | PC0     |         23 | Input with internal pull-up |

The column inputs use the ATmega328P internal pull-up resistors.

This leaves PC4 and PC5 available for the TWI peripheral.

---

## Master TWI Interface

| Signal | AVR pin | DIP-28 pin |
| ------ | ------- | ---------: |
| SDA    | PC4     |         27 |
| SCL    | PC5     |         28 |

The bus operates at:

```text
100 kHz
```

The slave address is:

```text
0x08
```

External pull-up resistors are required on both TWI lines.

The prototype uses:

```text
SDA ── 4.7 kΩ ── Logic supply
SCL ── 4.7 kΩ ── Logic supply
```

Only one pair of pull-up resistors is required for the complete bus.

---

## Master Unused GPIO

The final firmware does not use the diagnostic LEDs that were present during earlier development stages.

Therefore, pins such as:

```text
PB0
PB1
```

are not required by the final master implementation.

---

# Slave Controller

## 16×2 LCD

The LCD operates in 4-bit mode.

| LCD signal | AVR pin | DIP-28 pin |
| ---------- | ------- | ---------: |
| D4         | PB0     |         14 |
| D5         | PB1     |         15 |
| D6         | PB2     |         16 |
| D7         | PB3     |         17 |
| EN         | PB4     |         18 |
| RS         | PB5     |         19 |

The LCD `RW` input can be connected to ground because the firmware only writes to the display.

A typical LCD connection also requires:

* Logic supply
* Ground
* Contrast adjustment through the `V0` pin
* Backlight supply when required by the module

A potentiometer may be used to adjust LCD contrast.

---

## LM35 Interior Temperature Sensor

The LM35 provides the real interior-temperature measurement.

| Signal      | AVR pin    | DIP-28 pin |
| ----------- | ---------- | ---------: |
| LM35 output | PC0 / ADC0 |         23 |

The firmware configures ADC0 using:

```text
ADC resolution: 10 bits
Reference: internal 1.1 V
ADC clock: 125 kHz
Samples per measurement: 16
Measurement period: 500 ms
```

The LM35 output must share the same ground reference as the slave controller.

Because the firmware uses the ATmega328P internal 1.1 V ADC reference, an external voltage must not be applied to the `AREF` pin.

---

## Exterior Temperature Assignment Button

The exterior temperature is entered through the master keypad and stored temporarily as a pending value.

A push button connected to `INT1` assigns this pending value as the current exterior temperature.

| Function                        | AVR pin    | DIP-28 pin |
| ------------------------------- | ---------- | ---------: |
| Exterior-temperature assignment | PD3 / INT1 |          5 |

The firmware enables the internal pull-up resistor on PD3 and detects a falling edge.

The intended connection is:

```text
Internal pull-up
      │
      ▼
     PD3
      │
   Push button
      │
     GND
```

When the button is pressed:

```text
HIGH → LOW
    ↓
Falling edge
    ↓
INT1 interrupt
```

The firmware includes software debounce for this input.

---

## Slave TWI Interface

| Signal | AVR pin | DIP-28 pin |
| ------ | ------- | ---------: |
| SDA    | PC4     |         27 |
| SCL    | PC5     |         28 |

The slave uses the 7-bit address:

```text
0x08
```

The master and slave connections are therefore:

```text
MASTER                 SLAVE

PC4 / SDA ─────────── PC4 / SDA
PC5 / SCL ─────────── PC5 / SCL
GND       ─────────── GND
```

---

# Fan and Motor Driver

## PWM Output

The ventilation fan is controlled using Timer0 and the hardware `OC0A` output.

| Function    | AVR pin    | DIP-28 pin | Driver connection |
| ----------- | ---------- | ---------: | ----------------- |
| Fan PWM     | PD6 / OC0A |         12 | L293D EN1         |
| Direction 1 | PD4        |          6 | L293D IN1         |
| Direction 2 | PD5        |         11 | L293D IN2         |

The final implementation drives the motor in a single direction.

When the fan is active:

```text
IN1 = HIGH
IN2 = LOW
EN1 = PWM
```

When the fan is disabled:

```text
IN1 = LOW
IN2 = LOW
EN1 PWM duty = 0%
```

---

## L293D Channel 1 Connections

For the first L293D channel:

| L293D pin | Function | Connection       |
| --------- | -------- | ---------------- |
| 1         | EN1      | Slave PD6 / OC0A |
| 2         | IN1      | Slave PD4        |
| 3         | OUT1     | Motor terminal   |
| 4         | GND      | Ground           |
| 5         | GND      | Ground           |
| 6         | OUT2     | Motor terminal   |
| 7         | IN2      | Slave PD5        |
| 8         | VCC2     | Motor supply     |
| 16        | VCC1     | Logic supply     |

The motor must not be powered directly from an ATmega328P GPIO pin.

The logic and motor power sections must share a common ground.

---

## PWM Levels

The firmware uses four discrete PWM values in manual mode:

| Fan level | `OCR0A` | Approximate duty cycle |
| --------- | ------: | ---------------------: |
| OFF       |     `0` |                     0% |
| LOW       |    `85` |                    33% |
| MEDIUM    |   `170` |                    67% |
| HIGH      |   `255` |                   100% |

Timer0 operates in Fast PWM mode using a prescaler of 8.

With an 8 MHz MCU clock:

```text
fPWM = 8,000,000 / (8 × 256)
     ≈ 3906.25 Hz
```

Therefore:

```text
PWM frequency ≈ 3.91 kHz
```

---

# ATmega328P Power Connections

Each ATmega328P requires the following supply connections when using the DIP-28 package:

| Function | DIP-28 pin |
| -------- | ---------: |
| VCC      |          7 |
| GND      |          8 |
| AVCC     |         20 |
| AREF     |         21 |
| GND      |         22 |

`AVCC` must be powered because Port C and the ADC are used by the system.

Local decoupling capacitors should be placed close to the microcontroller supply pins.

A typical implementation uses a ceramic decoupling capacitor between each supply rail and ground.

---

# Clock Configuration

Both final firmware files define:

```c
#define F_CPU 8000000UL
```

Therefore, both ATmega328P controllers must operate at:

```text
8 MHz
```

The actual clock source may be internal or external, but the device configuration must provide an effective 8 MHz CPU clock.

The clock configuration is important because it affects:

* TWI timing
* Timer1 timing
* PWM frequency
* Delay functions
* ADC timing

---

# Grounding

All parts of the system must share a common electrical reference:

```text
Master GND
    │
Slave GND
    │
LCD GND
    │
LM35 GND
    │
L293D logic GND
    │
Motor supply GND
```

Without a common ground, TWI communication and motor-control signals may not operate reliably.

---

# Connection Summary

## Master

```text
PD7 → Keypad R1
PD6 → Keypad R2
PD5 → Keypad R3
PD4 → Keypad R4

PC3 → Keypad C1
PC2 → Keypad C2
PC1 → Keypad C3
PC0 → Keypad C4

PC4 → SDA
PC5 → SCL
```

## Slave

```text
PB0 → LCD D4
PB1 → LCD D5
PB2 → LCD D6
PB3 → LCD D7
PB4 → LCD EN
PB5 → LCD RS

PC0 → LM35 / ADC0
PC4 → SDA
PC5 → SCL

PD3 → Exterior-temperature button / INT1
PD4 → L293D IN1
PD5 → L293D IN2
PD6 → L293D EN1 / OC0A PWM
```

---

# Hardware Notes

* TWI uses external 4.7 kΩ pull-up resistors on SDA and SCL.
* The master and slave operate at 8 MHz.
* Both controllers share a common ground.
* The LCD is connected to the slave controller.
* Only one LM35 is used in the final implementation.
* Exterior temperature is entered through the keypad rather than measured by a second LM35.
* The fan is controlled through an L293D driver.
* The motor is driven in one direction only.
* The final implementation does not use the diagnostic LEDs from earlier development stages.
* The final prototype implements ventilation only; no physical heating actuator is connected.
