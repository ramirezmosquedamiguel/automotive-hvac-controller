# Master Firmware

This directory is reserved for the supervisory ATmega328P firmware.

## Responsibilities

The master application coordinates the HVAC system by:

- Scanning the 4×4 keypad
- Updating the 16×2 LCD
- Reading the locally assigned temperature sensor
- Managing manual and automatic operating modes
- Calculating the requested fan command
- Transmitting commands over TWI/I²C
- Requesting and validating data from the slave controller

## Planned source organization

```text
master/
├── src/
│   ├── main.c
│   ├── lcd.c
│   ├── keypad.c
│   ├── adc.c
│   ├── twi_master.c
│   └── control.c
└── include/
    ├── lcd.h
    ├── keypad.h
    ├── adc.h
    ├── twi_master.h
    └── control.h
```

The final organization may be simplified if the original implementation is contained in a single source file. The first priority is to preserve verified behavior; modularization should follow after the code has been compiled and tested.

## Integration notes

- Confirm `F_CPU` before calculating delays or TWI settings.
- Keep the slave address consistent with the slave firmware.
- Use timeouts or error handling so the user interface does not block indefinitely when the bus fails.
- Document the mapping between keypad commands, operating modes, and transmitted command values.
