# Slave Firmware

This directory is reserved for the delegated ATmega328P firmware.

## Responsibilities

The slave application supports the master controller by:

- Listening for its TWI/I²C address
- Receiving commands from the master
- Reading the remotely assigned temperature channel
- Applying the commanded PWM fan output
- Returning sensor or status data when requested
- Handling TWI state transitions through interrupts

## Planned source organization

```text
slave/
├── src/
│   ├── main.c
│   ├── adc.c
│   ├── pwm.c
│   └── twi_slave.c
└── include/
    ├── adc.h
    ├── pwm.h
    └── twi_slave.h
```

The original implementation may initially be published as a single verified `main.c`. Refactoring should be performed only after the communication and actuator behavior have been reproduced successfully.

## TWI implementation notes

The original slave address is documented as `0x20`. The interrupt service routine should explicitly handle the expected slave-receiver states, including address acknowledgement and data reception.

Typical status cases include:

- Slave address received and acknowledged
- Data byte received and acknowledged
- Stop or repeated-start condition
- Unexpected or error state

The firmware should restore the TWI peripheral to a receptive state after each transaction and avoid performing long blocking operations inside the interrupt service routine.

## Integration notes

- Confirm whether the received command represents a mode, a fan level, or a raw PWM duty cycle.
- Define the returned data format before publishing the master and slave source files.
- Keep shared command identifiers in a common protocol definition when the code is modularized.
- Verify the PWM timer and output pin against the final schematic.
