# Hardware Connections

This document records the interface assignments that are already established for the prototype. Connections not listed here should be confirmed against the final schematic before wiring or compiling the firmware.

## Master ATmega328P

### LCD 16×2 interface

The display is connected in 4-bit mode.

| LCD signal | ATmega328P pin |
|---|---|
| RS | PB2 |
| EN | PB3 |
| D4 | PB4 |
| D5 | PB5 |
| D6 | PB6 |
| D7 | PB7 |

The LCD power, contrast, and backlight connections depend on the display module used. A contrast potentiometer is normally required on the `V0` pin.

### TWI/I²C interface

| Bus signal | ATmega328P pin |
|---|---|
| SDA | PC4 |
| SCL | PC5 |

Use one 4.7 kΩ pull-up resistor from SDA to the logic supply and another from SCL to the logic supply. Both controllers must share the same ground reference.

## Slave ATmega328P

### TWI/I²C interface

| Bus signal | ATmega328P pin |
|---|---|
| SDA | PC4 |
| SCL | PC5 |

The slave address used in the original implementation is:

```c
#define SLAVE_ADDR 0x20
```

The address representation must remain consistent between the master API and the slave register configuration. Some low-level implementations use a seven-bit address while others expect the value shifted into the address register.

## Temperature sensors

The project uses two temperature channels representing interior and exterior conditions. Their final ADC channels and conditioning circuits should be documented together with the cleaned firmware because these assignments may vary between the simulation and physical prototype.

Before connecting an analog sensor, verify:

- Sensor supply voltage
- Common ground
- Output-voltage range
- Selected ADC reference
- Maximum voltage permitted at the ATmega328P input

## PWM fan outputs

The fan or simulated fan load must be connected through an appropriate driver stage. An ATmega328P output pin must not directly supply a motor.

A valid implementation should include:

- A transistor or motor-driver stage
- A flyback diode for inductive loads when required
- A separate or adequately sized power supply
- A common ground between logic and power stages
- Supply decoupling near each microcontroller

## Power and clock

The prototype has been tested in 8 MHz and 16 MHz configurations. Firmware timing parameters, TWI bit rate, delays, and PWM settings must match the selected clock frequency.

Connect and decouple all required supply pins, including `VCC`, `AVCC`, and ground. `AVCC` must be powered even when only part of Port C or the ADC is used.

## Pending documentation

The following details will be added when the final source and schematic are incorporated:

- Keypad row and column pinout
- Interior and exterior sensor ADC channels
- PWM output pins and timer selection
- Interrupt input assignments
- Reset and clock circuitry
- Complete wiring or simulation schematic
