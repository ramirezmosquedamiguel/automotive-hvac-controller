# Simulation and Validation

## Purpose

This directory contains the simulation evidence used to validate the automotive HVAC controller before and during physical implementation.

The project was simulated primarily in Proteus to verify the interaction between the two ATmega328P controllers, the user interface, TWI communication, temperature handling, and fan-control logic.

The simulation complements the physical prototype and provides a visual reference for the system architecture and operating modes.

---

## Simulation Environment

The final simulation was developed using:

- Proteus 8
- ATmega328P microcontrollers
- AVR-GCC / Microchip Studio firmware
- 4×4 matrix keypad
- 16×2 LCD
- LM35 temperature sensor
- TWI / I²C communication
- L293D motor driver
- PWM-controlled DC fan

Both microcontrollers operate at 8 MHz.

The TWI bus operates at 100 kHz and uses the slave address `0x08`.

---

## Proteus System Overview

The following image shows the complete simulated HVAC architecture.

![Proteus system overview](images/proteus-overview.png)

The simulation includes the master and slave controllers, keypad interface, LCD, temperature input, TWI communication, and fan-control stage.

The master handles user input and transmits control commands to the slave.

The slave performs temperature acquisition, HVAC control-state evaluation, display management, and PWM fan control.

---

## Manual Mode Simulation

The manual-mode simulation verifies direct user control of the fan level.

![Manual mode simulation](images/manual-overview.png)

Manual mode is selected using the `B` key.

Once manual mode is active, the `D` key cycles through the available fan levels:

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

The corresponding PWM duty cycles are:

| Fan level | PWM duty cycle |
|---|---:|
| OFF | 0% |
| LOW | ~33% |
| MEDIUM | ~67% |
| HIGH | 100% |

The LCD provides feedback about the currently selected manual level.

---

## Automatic Mode Simulation

The automatic-mode simulation validates the temperature-based control logic.

![Automatic mode simulation](images/automatic-simulation.png)

Automatic mode is selected using the `A` key.

The controller compares the interior temperature `Ti` with the exterior temperature `Te`:

```text
Ti - Te
```

using a ±2 °C deadband.

The resulting control behavior is:

| Condition | State | Fan |
|---|---|---|
| `Ti - Te > 2 °C` | COOLING | HIGH |
| `Ti - Te < -2 °C` | HEATING | OFF |
| `-2 °C ≤ Ti - Te ≤ 2 °C` | STABLE | OFF |
| Missing temperature data | WAIT TEMPERATURES | OFF |

The current prototype only includes a ventilation fan.

Therefore, the HEATING state is detected and displayed by the firmware but does not activate a physical heating actuator.

---

## Temperature Handling

The final implementation uses one physical LM35 sensor.

The interior temperature is measured by the LM35 connected to ADC0 on the slave controller.

The exterior temperature is entered manually through the keypad and assigned using the button connected to `INT1`.

The sequence is:

```text
Enter Te using keypad
        ↓
Press #
        ↓
Value stored as pending
        ↓
Press INT1 button
        ↓
Te becomes active
```

This arrangement allows different exterior-temperature conditions to be tested without requiring a second physical temperature sensor.

---

## TWI Communication Validation

The simulation also validates communication between the two ATmega328P controllers.

```text
ATmega328P Master
        │
        │ TWI @ 100 kHz
        │ [Command][Value]
        ▼
ATmega328P Slave
```

The application protocol uses two-byte packets:

```text
+-----------+-----------+
| Command   | Value     |
| 1 byte    | 1 byte    |
+-----------+-----------+
```

The slave receives packets through the `TWI_vect` interrupt service routine and processes completed packets in the main application loop.

Detailed communication behavior is documented in:

[Communication Protocol](../docs/communication-protocol.md)

---

## Validation Goals

The simulation was used to verify that:

1. Both ATmega328P controllers initialize correctly.
2. The keypad generates valid user commands.
3. Numeric temperature values can be entered and confirmed.
4. TWI communication operates between master and slave.
5. The slave receives complete two-byte packets.
6. The LCD displays system status and temperature information.
7. Manual mode changes the fan PWM level correctly.
8. Automatic mode evaluates the temperature difference correctly.
9. The LM35 is sampled periodically through the ADC.
10. The L293D receives the expected PWM and direction signals.

---

## Physical Prototype Validation

The simulated design was later implemented as a physical prototype.

![Physical prototype](../media/prototype/images/final-assembly.jpg)

The physical implementation was used to confirm that the behavior validated in simulation could also be reproduced using the real microcontrollers, sensor, display, keypad, motor driver, and DC motor.

A manual-mode demonstration is available here:

[Manual Mode Prototype Demonstration](../media/prototype/videos/manual-mode.mp4)

---

## Simulation Limitations

Proteus was used primarily to validate firmware logic and system integration.

The simulation does not reproduce all electrical characteristics of a production automotive environment, including:

- Electromagnetic interference
- Automotive voltage transients
- Thermal behavior of the power stage
- Real motor-current transients
- Automotive EMC compliance
- Functional-safety requirements

The physical prototype therefore serves as an additional validation stage beyond simulation.

---

## Related Documentation

- [System Architecture](../docs/architecture.md)
- [TWI Communication Protocol](../docs/communication-protocol.md)
- [Hardware Connections](../docs/hardware-connections.md)
- [System Operation](../docs/operation.md)