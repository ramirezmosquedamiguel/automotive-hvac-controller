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