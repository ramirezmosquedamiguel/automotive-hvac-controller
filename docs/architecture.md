# System Architecture

## Purpose

This document describes the distributed architecture of the automotive HVAC controller prototype.

The system is implemented using two ATmega328P microcontrollers connected through the hardware TWI peripheral.

The architecture separates user interaction from sensing, display management, control evaluation, and actuator control.

Both controllers operate at 8 MHz.

---

## Architectural Overview

```text
                         USER
                          │
                     4×4 Keypad
                          │
                          ▼
                ┌──────────────────┐
                │ ATmega328P       │
                │ MASTER           │
                │                  │
                │ • Keypad scan    │
                │ • Debouncing     │
                │ • Numeric input  │
                │ • Mode selection │
                └────────┬─────────┘
                         │
                   TWI / I²C
                     100 kHz
                         │
                         ▼
                ┌──────────────────┐
                │ ATmega328P       │
                │ SLAVE            │
                │                  │
                │ • TWI reception  │
                │ • LM35 / ADC     │
                │ • LCD interface  │
                │ • HVAC logic     │
                │ • PWM control    │
                └───┬────┬─────┬──┘
                    │    │     │
                  LM35  LCD   L293D
                               │
                               ▼
                            DC Fan
```

---

## Master Controller

The master controller acts as the user-interface node of the system.

Its main responsibilities are:

* Scanning the 4×4 matrix keypad
* Debouncing keypad events
* Capturing numeric values of up to two digits
* Clearing and confirming user input
* Selecting OFF, Automatic, and Manual modes
* Cycling through manual fan levels
* Encoding commands into two-byte application packets
* Transmitting commands to the slave using TWI

The master does not directly control the LCD, temperature sensor, or fan actuator in the final implementation.

Its main role is to convert user interaction into structured control commands.

---

## Slave Controller

The slave controller acts as the sensing, control, display, and actuation node.

Its responsibilities include:

* Receiving TWI packets from the master
* Processing application commands
* Reading the LM35 interior-temperature sensor through ADC0
* Storing the user-provided exterior temperature
* Evaluating the automatic HVAC control state
* Managing OFF, Automatic, and Manual operating modes
* Updating the 16×2 LCD
* Generating the fan PWM signal
* Driving the L293D motor driver
* Handling the `INT1` exterior-temperature assignment button

The slave therefore performs the main HVAC control logic after receiving user commands from the master.

---

## Communication Architecture

The two controllers communicate through the ATmega328P hardware TWI peripheral.

The bus configuration is:

| Parameter     | Value      |
| ------------- | ---------- |
| Interface     | TWI / I²C  |
| Master        | ATmega328P |
| Slave         | ATmega328P |
| Bus frequency | 100 kHz    |
| Slave address | `0x08`     |
| SDA           | PC4        |
| SCL           | PC5        |

The application protocol uses two-byte packets:

```text
+-----------+-----------+
| Command   | Value     |
| 1 byte    | 1 byte    |
+-----------+-----------+
```

The command byte identifies the requested operation, while the value byte contains the associated parameter.

Examples include:

```text
[0x06][0x01] → Select Automatic mode

[0x06][0x02] → Select Manual mode

[0x07][0x03] → Select HIGH manual fan level
```

The complete protocol is documented in:

```text
docs/communication-protocol.md
```

---

## User Interaction Flow

The keypad is connected exclusively to the master controller.

A typical user command follows this path:

```text
User presses key
      ↓
Master scans keypad
      ↓
Key event is debounced
      ↓
Command and value are generated
      ↓
TWI packet is transmitted
      ↓
Slave receives packet
      ↓
Command is processed
      ↓
LCD and/or fan behavior is updated
```

This separates user-input acquisition from the HVAC control implementation.

---

## Temperature Architecture

The final implementation uses one physical temperature sensor.

### Interior Temperature

The interior temperature is measured by an LM35 connected to ADC0 on the slave controller.

```text
LM35
  │
  ▼
ADC0
  │
  ▼
ATmega328P Slave
  │
  ▼
Interior Temperature (Ti)
```

The sensor is sampled periodically and multiple ADC readings are averaged before the temperature is used by the control logic.

### Exterior Temperature

The exterior temperature is not measured by a second physical sensor.

Instead, the user enters a numeric temperature through the keypad.

The sequence is:

```text
Keypad input
     ↓
Master
     ↓
TWI
     ↓
Slave stores pending value
     ↓
INT1 button
     ↓
Exterior Temperature (Te)
```

This allows the prototype to simulate an external temperature condition while using only one physical LM35 sensor.

---

## Control Architecture

The slave contains the HVAC control logic.

The system supports three operating modes:

```text
OFF
AUTOMATIC
MANUAL
```

### OFF

The fan output is disabled.

```text
PWM = 0%
```

### Manual

The user directly selects one of four fan levels:

```text
OFF
LOW
MEDIUM
HIGH
```

These levels correspond to discrete PWM duty cycles.

### Automatic

The slave compares:

```text
Ti - Te
```

using a ±2 °C deadband.

The resulting states are:

```text
COOLING
STABLE
HEATING
WAIT TEMPERATURES
```

Only the COOLING state activates the fan in the current physical implementation.

The HEATING state is detected and displayed but does not control a physical heating actuator.

---

## Automatic Control Flow

```text
Read Ti from LM35
        │
        ▼
Is Te available?
        │
       No
        │
        ▼
WAIT TEMPERATURES

        Yes
        │
        ▼
Calculate Ti - Te
        │
        ├── > +2 °C ─────► COOLING ─────► Fan HIGH
        │
        ├── < -2 °C ─────► HEATING ─────► Fan OFF
        │
        └── otherwise ───► STABLE ──────► Fan OFF
```

---

## Actuation Architecture

The fan is controlled by the slave through the L293D motor driver.

```text
ATmega328P Slave
       │
       ├── PD4 ─────► L293D IN1
       │
       ├── PD5 ─────► L293D IN2
       │
       └── PD6 ─────► L293D EN1
             PWM
              │
              ▼
            L293D
              │
              ▼
            DC Fan
```

Timer0 generates the PWM signal using the hardware `OC0A` output on PD6.

The fan operates in one direction only.

---

## Display Architecture

The LCD is connected to the slave controller.

Its role is to provide feedback about:

* Current operating mode
* Manual fan level
* Interior temperature
* Exterior temperature
* Automatic control state
* Numeric input
* Pending-value confirmation
* Invalid or unavailable input conditions

Temporary interface messages are handled independently from the actuator update logic.

This allows the display to provide user feedback without delaying fan-control decisions.

---

## Timing Architecture

Timer1 is used independently by both controllers.

### Master Timer1

The master generates a 1 ms periodic interrupt.

This timing base is used to schedule keypad scanning every 5 ms and implement software debouncing.

### Slave Timer1

The slave also generates a 1 ms periodic interrupt.

This timing base is used for:

* LM35 sampling scheduling
* `INT1` button debounce
* Temporary LCD message timing

### Slave Timer0

Timer0 operates in Fast PWM mode and generates the fan-control signal on `OC0A / PD6`.

With an 8 MHz system clock and a prescaler of 8, the PWM frequency is approximately:

```text
3.91 kHz
```

---

## Functional Distribution

The final responsibility distribution is:

| Function                     | Master | Slave |
| ---------------------------- | :----: | :---: |
| Keypad scanning              |    ✓   |       |
| Keypad debouncing            |    ✓   |       |
| Numeric input capture        |    ✓   |       |
| Mode selection input         |    ✓   |       |
| TWI transmission             |    ✓   |       |
| TWI reception                |        |   ✓   |
| LM35 acquisition             |        |   ✓   |
| Exterior-temperature storage |        |   ✓   |
| LCD management               |        |   ✓   |
| Automatic control evaluation |        |   ✓   |
| Manual fan-level application |        |   ✓   |
| PWM generation               |        |   ✓   |
| L293D control                |        |   ✓   |
| `INT1` handling              |        |   ✓   |

---

## Design Rationale

The architecture intentionally divides the system into two functional nodes.

The master focuses on human-machine interaction and communication.

The slave concentrates sensing, control decisions, user feedback, and actuation.

This separation demonstrates several embedded-system concepts:

* Distributed control
* Peripheral specialization
* Inter-microcontroller communication
* Interrupt-driven communication
* Periodic task scheduling
* ADC acquisition
* PWM actuator control
* Human-machine interface design

The project therefore demonstrates more than simple peripheral integration: it implements coordinated behavior between two embedded controllers.

---

## System Limitations

This project is an academic embedded-system prototype and is not intended to represent a production automotive HVAC ECU.

The current implementation does not include:

* A physical heating actuator
* A second physical exterior-temperature sensor
* CAN communication
* Automotive diagnostics
* Functional-safety mechanisms
* Automotive-qualified power electronics
* Electromagnetic compatibility validation
* Production-grade fault handling

The TWI architecture and application protocol are used to demonstrate distributed embedded control in a controlled prototype environment.

---

## Related Documentation

Additional technical details are available in:

```text
docs/communication-protocol.md
docs/hardware-connections.md
docs/operation.md
```
