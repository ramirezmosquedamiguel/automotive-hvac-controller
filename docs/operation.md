# System Operation

## Purpose

This document describes how the automotive HVAC controller is operated from the user interface and how the master and slave ATmega328P controllers coordinate the system behavior.

The system supports three operating modes:

* OFF
* Automatic
* Manual

User commands are entered through the 4×4 keypad connected to the master controller. The master interprets the keypad input and transmits application commands to the slave through the TWI bus.

The slave processes these commands, reads the interior temperature from the LM35 sensor, updates the 16×2 LCD, evaluates the HVAC control state, and drives the ventilation fan through PWM and an L293D motor driver.

---

## User Interface

The 4×4 keypad provides the primary user interface for entering temperature values and controlling the HVAC operating mode.

| Key   | Function                            |
| ----- | ----------------------------------- |
| `0–9` | Enter a numeric temperature value   |
| `*`   | Clear the current numeric input     |
| `#`   | Confirm the entered value           |
| `A`   | Select Automatic mode               |
| `B`   | Select Manual mode                  |
| `C`   | Turn the system OFF                 |
| `D`   | Cycle through the manual fan levels |

The master accepts numeric values containing up to two digits.

After every valid key event, the corresponding command is transmitted to the slave using the two-byte TWI application protocol.

---

## Temperature Inputs

The control system uses two temperature values:

| Temperature                 | Source                                                             |
| --------------------------- | ------------------------------------------------------------------ |
| Interior temperature (`Ti`) | LM35 connected to ADC0 on the slave                                |
| Exterior temperature (`Te`) | Numeric value entered through the keypad and assigned using `INT1` |

### Interior Temperature

The LM35 provides the real interior-temperature measurement.

The slave samples the LM35 through ADC0 using the ATmega328P ADC with the internal 1.1 V reference.

A new temperature measurement is requested every 500 ms.

For each measurement, 16 ADC conversions are averaged to reduce short-term variations in the sensor reading.

The resulting temperature is rounded to an integer value in degrees Celsius before being used by the control logic.

### Exterior Temperature

The exterior temperature is entered manually through the keypad.

Entering a value does not immediately modify the stored exterior temperature.

The input follows this sequence:

```text
Enter numeric value
        ↓
Press #
        ↓
Value becomes pending
        ↓
Press INT1 button
        ↓
Pending value becomes Te
```

For example, to assign an exterior temperature of 25 °C:

```text
Press 2
Press 5
Press #
Press INT1 button
```

After `#` is pressed, the value is stored as a pending temperature.

The button connected to `INT1` then assigns that pending value as the exterior temperature.

If no pending value exists when the button is pressed, the LCD displays a warning instead of modifying the temperature.

---

## Operating Modes

The HVAC controller operates in one of three mutually exclusive modes.

---

### OFF Mode

OFF mode disables the ventilation output.

It can be selected by pressing:

```text
C
```

When OFF mode is active:

```text
Fan PWM = 0%
L293D IN1 = LOW
L293D IN2 = LOW
```

The LCD displays:

```text
SYSTEM OFF
```

Switching to OFF mode also resets the currently selected manual fan level.

---

### Manual Mode

Manual mode allows the user to directly select the ventilation fan level.

It is selected by pressing:

```text
B
```

When manual mode is first selected, the fan level is initialized to OFF.

The `D` key cycles through the available fan levels:

```text
OFF
 ↓
LOW
 ↓
MEDIUM
 ↓
HIGH
 ↓
OFF
```

The corresponding PWM values are:

| Manual level | OCR0A value | Approximate duty cycle |
| ------------ | ----------: | ---------------------: |
| OFF          |         `0` |                     0% |
| LOW          |        `85` |                    33% |
| MEDIUM       |       `170` |                    67% |
| HIGH         |       `255` |                   100% |

The LCD indicates the currently selected level using messages such as:

```text
MANUAL: OFF
MANUAL: LOW
MANUAL: MEDIUM
MANUAL: HIGH
```

The `CMD_MANUAL_LEVEL` command is only accepted while the slave is operating in manual mode.

If a manual-level command is received in another operating mode, the requested level is ignored.

---

## Manual Mode Sequence

A typical manual-mode operation is:

```text
Press B
   ↓
Manual mode selected
   ↓
Fan level = OFF
   ↓
Press D
   ↓
LOW
   ↓
Press D
   ↓
MEDIUM
   ↓
Press D
   ↓
HIGH
   ↓
Press D
   ↓
OFF
```

The user can leave manual mode at any time by selecting Automatic mode with `A` or OFF mode with `C`.

---

### Automatic Mode

Automatic mode evaluates the difference between the interior and exterior temperatures.

It is selected by pressing:

```text
A
```

Both temperature values must be valid before the automatic controller can determine a thermal state.

The control variable is:

```text
Temperature difference = Ti - Te
```

where:

```text
Ti = interior temperature
Te = exterior temperature
```

The controller uses a deadband of ±2 °C.

---

## Automatic Control Logic

The automatic controller evaluates the temperature difference using the following conditions:

| Condition                            | Control state     | Fan behavior |
| ------------------------------------ | ----------------- | ------------ |
| `Ti - Te > 2 °C`                     | COOLING           | HIGH         |
| `Ti - Te < -2 °C`                    | HEATING           | OFF          |
| `-2 °C ≤ Ti - Te ≤ 2 °C`             | STABLE            | OFF          |
| One or both temperatures unavailable | WAIT TEMPERATURES | OFF          |

### Cooling

If the interior is more than 2 °C warmer than the exterior:

```text
Ti - Te > 2 °C
```

the controller enters:

```text
CONTROL_COOLING
```

and the ventilation fan operates at the automatic PWM level:

```text
100%
```

The LCD displays:

```text
AUTO: COOLING
```

### Stable

If the temperature difference remains inside the ±2 °C deadband:

```text
-2 °C ≤ Ti - Te ≤ 2 °C
```

the controller enters:

```text
CONTROL_STABLE
```

and the fan remains OFF.

The LCD displays:

```text
AUTO: STABLE
```

### Heating Request

If the interior is more than 2 °C colder than the exterior:

```text
Ti - Te < -2 °C
```

the controller enters:

```text
CONTROL_HEATING
```

The LCD displays:

```text
AUTO: HEATING
```

The current prototype does not include a physical heating actuator.

Therefore, the HEATING state is detected and displayed by the control software, but it does not activate the ventilation motor.

### Waiting for Temperatures

If either the interior or exterior temperature has not yet been established, the controller enters:

```text
CONTROL_WAIT_TEMPERATURES
```

The LCD displays:

```text
AUTO: WAIT TEMPS
```

and the fan remains disabled.

---

## Automatic Mode Sequence

A typical automatic-mode operation is:

```text
LM35 measures Ti
        ↓
User enters Te
        ↓
Press #
        ↓
Press INT1 button
        ↓
Te becomes valid
        ↓
Press A
        ↓
Automatic mode selected
        ↓
Calculate Ti - Te
        ↓
Evaluate ±2 °C deadband
        ↓
Select COOLING / STABLE / HEATING
        ↓
Apply fan output
```

Because the LM35 is sampled periodically, changes in the interior temperature can trigger a new automatic-control evaluation without requiring further user input.

---

## Fan Control

The ventilation fan is controlled by Timer0 using the `OC0A` PWM output on `PD6`.

The PWM output drives the enable input of the L293D motor driver.

The direction inputs are:

```text
PD4 → L293D IN1
PD5 → L293D IN2
PD6 → L293D EN1 / PWM
```

Whenever the fan is active:

```text
IN1 = HIGH
IN2 = LOW
```

The motor therefore operates in a single direction.

When the fan is disabled:

```text
OCR0A = 0
IN1 = LOW
IN2 = LOW
```

This places the motor-driver channel in an inactive state.

---

## PWM Configuration

Timer0 operates in Fast PWM mode with:

```text
F_CPU = 8 MHz
Prescaler = 8
TOP = 255
```

The resulting PWM frequency is:

```text
8,000,000 / (8 × 256)
≈ 3906.25 Hz
```

Therefore, the fan PWM frequency is approximately:

```text
3.91 kHz
```

---

## LCD Behavior

The 16×2 LCD provides feedback about user input, temperature values, operating modes, and control states.

Examples of displayed messages include:

```text
SYSTEM OFF
MANUAL: LOW
MANUAL: MEDIUM
MANUAL: HIGH
AUTO: COOLING
AUTO: STABLE
AUTO: HEATING
AUTO: WAIT TEMPS
```

The second LCD row displays the current temperature values using:

```text
Ti:XX°C Te:XX°C
```

If a temperature has not yet been established, the corresponding value is displayed as:

```text
--°C
```

Temporary user-interface messages are also displayed when values are entered, cleared, confirmed, or rejected.

These temporary messages do not stop the control logic or fan update.

---

## Timing Behavior

Timer1 provides a 1 ms periodic interrupt used by the slave for software timing.

The timer supports:

* LM35 sampling every 500 ms
* `INT1` button debounce timing
* Temporary LCD message timing

Temporary LCD messages are retained for approximately 1.5 seconds before the current HVAC status is displayed again.

The control output is updated independently of these temporary LCD messages, so interface feedback does not delay actuator control.

---

## System Operation Summary

The complete system behavior can be summarized as:

```text
                  USER
                   │
             4×4 Keypad
                   │
                   ▼
          ATmega328P Master
                   │
             TWI @ 100 kHz
                   │
                   ▼
          ATmega328P Slave
          ┌────────┼─────────┐
          │        │         │
        LM35      LCD      Control
          │                  │
          │                  ▼
          │               Timer0
          │                  │
          │                 PWM
          │                  │
          └──────────────► L293D
                             │
                             ▼
                           DC Fan
```

The master primarily handles user interaction and command transmission, while the slave performs temperature acquisition, control-state evaluation, display management, and actuator control.

This separation creates a distributed embedded architecture in which communication, sensing, user interaction, and actuation are divided between two microcontrollers.

---

## Prototype Limitation

This project is an embedded-system prototype intended to demonstrate distributed HVAC control using AVR peripherals and TWI communication.

The automatic logic distinguishes between cooling, stable, and heating conditions; however, the physical implementation includes only a ventilation fan.

The `CONTROL_HEATING` state therefore represents a software-detected heating request and does not drive a heating actuator in the current hardware implementation.
