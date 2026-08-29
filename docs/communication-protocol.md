# TWI Communication Protocol

## Purpose

The master and slave ATmega328P microcontrollers communicate through the hardware TWI peripheral using a simple two-byte application protocol.

The master is responsible for reading user input from the 4×4 keypad and transmitting commands to the slave. The slave receives those commands, updates the system operating state, controls the LCD interface, and applies the corresponding HVAC control behavior.

The communication link operates at 100 kHz and uses the slave address `0x08`.

## Bus Configuration

| Parameter           | Value      |
| ------------------- | ---------- |
| Interface           | TWI / I²C  |
| Master              | ATmega328P |
| Slave               | ATmega328P |
| Slave address       | `0x08`     |
| TWI bus frequency   | 100 kHz    |
| MCU clock frequency | 8 MHz      |
| SDA                 | PC4        |
| SCL                 | PC5        |

## Packet Format

Each TWI transaction carries a two-byte application packet:

```text
+-----------+-----------+
| Command   | Value     |
| 1 byte    | 1 byte    |
+-----------+-----------+
```

The first byte identifies the requested operation. The second byte contains the value associated with that command.

For example:

```text
[0x06][0x02]
```

selects manual operating mode.

## Command Set

| Command | Value                   | Description                                                              |
| ------- | ----------------------- | ------------------------------------------------------------------------ |
| `0x03`  | `0–99`                  | Updates the numeric value currently being entered from the keypad.       |
| `0x04`  | `0x00`                  | Clears the current numeric input.                                        |
| `0x05`  | `0–99`                  | Confirms the entered value and stores it as a pending temperature value. |
| `0x06`  | Mode identifier         | Selects the HVAC operating mode.                                         |
| `0x07`  | Manual level identifier | Selects the fan level when the system is in manual mode.                 |

### Operating Mode Values

| Value  | Mode      |
| ------ | --------- |
| `0x00` | OFF       |
| `0x01` | Automatic |
| `0x02` | Manual    |

### Manual Fan Levels

| Value  | Level  | PWM duty |
| ------ | ------ | -------- |
| `0x00` | OFF    | 0%       |
| `0x01` | LOW    | ~33%     |
| `0x02` | MEDIUM | ~67%     |
| `0x03` | HIGH   | 100%     |

## TWI Transaction Sequence

Each application packet is transmitted using a standard TWI master-write transaction.

```text
START
  ↓
SLA+W
  ↓
ACK
  ↓
COMMAND
  ↓
ACK
  ↓
VALUE
  ↓
ACK
  ↓
STOP
```

The master first generates a START condition and transmits the 7-bit slave address together with the write bit.

After the slave acknowledges the address, the master sends the command byte followed by the associated value byte.

The transaction ends when the master generates a STOP condition.

## Master-Side Status Validation

The master verifies the TWI peripheral status after each critical stage of the transaction.

The following status conditions are expected during a successful write operation:

| Stage                      | Expected TWI status          |
| -------------------------- | ---------------------------- |
| START transmitted          | `TW_START` or `TW_REP_START` |
| Slave address acknowledged | `TW_MT_SLA_ACK`              |
| Data byte acknowledged     | `TW_MT_DATA_ACK`             |

If one of these expected states is not received, the transaction is treated as an error.

### Master Result Codes

The master firmware represents the communication result using the following internal states:

| Result                    | Meaning                                                                  |
| ------------------------- | ------------------------------------------------------------------------ |
| `TWI_RESULT_OK`           | Transaction completed successfully                                       |
| `TWI_RESULT_TIMEOUT`      | The peripheral did not complete the operation before the timeout expired |
| `TWI_RESULT_START_ERROR`  | An unexpected state occurred after generating START                      |
| `TWI_RESULT_ADDRESS_NACK` | The slave did not acknowledge its address                                |
| `TWI_RESULT_DATA_NACK`    | The slave did not acknowledge a transmitted data byte                    |

### Timeout Protection

The master uses a software timeout while waiting for the `TWINT` flag.

This prevents the application from remaining indefinitely blocked if the TWI bus does not complete an expected operation.

If the timeout expires, the transaction returns `TWI_RESULT_TIMEOUT`.

## Slave-Side Reception

The slave receives TWI packets through the `TWI_vect` interrupt service routine.

The receive process is based on the TWI status register and follows the expected slave-receiver states.

| TWI status               | Meaning                                       | Slave action                                             |
| ------------------------ | --------------------------------------------- | -------------------------------------------------------- |
| `TW_SR_SLA_ACK`          | Slave address received and acknowledged       | Reset receive index and prepare to receive data          |
| `TW_SR_ARB_LOST_SLA_ACK` | Arbitration lost, then slave address received | Reset receive index and continue reception               |
| `TW_SR_DATA_ACK`         | Data byte received and acknowledged           | Store the byte in the receive buffer                     |
| `TW_SR_STOP`             | STOP or repeated START received               | Validate the complete packet and mark it as ready        |
| `TW_BUS_ERROR`           | Illegal START or STOP condition detected      | Reset the receive process and recover the TWI peripheral |

### Receive Buffer

The slave uses a two-byte buffer:

```c
static volatile uint8_t twi_rx_buffer[TWI_PACKET_SIZE];
```

with:

```c
#define TWI_PACKET_SIZE 2U
```

The first received byte is interpreted as the command and the second as the associated value.

### Packet Completion

When the slave detects `TW_SR_STOP`, it checks whether exactly two bytes were received.

If the packet size is valid:

```c
received_command = twi_rx_buffer[0];
received_value = twi_rx_buffer[1];
packet_ready = 1;
```

The application then processes the packet outside the interrupt service routine.

This keeps the TWI interrupt routine short and prevents the HVAC control logic from being executed directly inside the ISR.

## Command Processing

Once a complete packet is available, the slave copies the received command and value from the shared variables and processes them in the main application loop.

The command byte determines how the associated value is interpreted.

### `CMD_INPUT_UPDATE` — `0x03`

Updates the numeric value currently being entered from the keypad.

The slave uses this command to refresh the LCD and show the current input value.

### `CMD_INPUT_CLEAR` — `0x04`

Clears the current numeric input.

The slave updates the LCD to indicate that the entry was cleared.

### `CMD_VALUE_CONFIRM` — `0x05`

Stores the received numeric value as a pending temperature value.

The value is not immediately assigned as the exterior temperature. It remains pending until the exterior-temperature button connected to `INT1` is pressed.

### `CMD_MODE_SELECT` — `0x06`

Changes the HVAC operating mode.

Valid values are:

| Value  | Mode      |
| ------ | --------- |
| `0x00` | OFF       |
| `0x01` | Automatic |
| `0x02` | Manual    |

Every valid mode change resets the manual fan level to OFF.

### `CMD_MANUAL_LEVEL` — `0x07`

Changes the requested fan level when the system is operating in manual mode.

Valid values are:

| Value  | Manual level |
| ------ | ------------ |
| `0x00` | OFF          |
| `0x01` | LOW          |
| `0x02` | MEDIUM       |
| `0x03` | HIGH         |

If this command is received while the system is not in manual mode, the slave ignores the requested level and displays a warning on the LCD.

## Communication Examples

The following examples show how application-level commands are encoded into the two-byte TWI packet.

### Select Automatic Mode

```text
[0x06][0x01]
```

* `0x06` → `CMD_MODE_SELECT`
* `0x01` → `MODE_AUTOMATIC`

The slave switches the HVAC controller to automatic mode.

### Select Manual Mode

```text
[0x06][0x02]
```

* `0x06` → `CMD_MODE_SELECT`
* `0x02` → `MODE_MANUAL`

The slave switches the HVAC controller to manual mode and initializes the manual fan level to OFF.

### Select HIGH Manual Fan Level

```text
[0x07][0x03]
```

* `0x07` → `CMD_MANUAL_LEVEL`
* `0x03` → `MANUAL_LEVEL_HIGH`

This command is accepted only when the system is already operating in manual mode.

### Enter and Confirm an Exterior Temperature

If the user enters `25` on the keypad, the master first transmits intermediate input updates:

```text
[0x03][0x02]
[0x03][0x19]
```

The first packet represents the intermediate value `2`.

The second packet represents decimal `25`, which corresponds to hexadecimal `0x19`.

When the user presses `#`, the master sends:

```text
[0x05][0x19]
```

The slave stores `25` as a pending value.

Pressing the button connected to `INT1` then assigns this pending value as the exterior temperature.

### Turn the System OFF

```text
[0x06][0x00]
```

* `0x06` → `CMD_MODE_SELECT`
* `0x00` → `MODE_OFF`

The slave switches the HVAC controller to the OFF state.

## Error Handling and Bus Recovery

Both controllers include basic mechanisms to detect abnormal TWI communication conditions.

### Master

The master checks the TWI status after the START condition, slave addressing, and each transmitted data byte.

A timeout prevents the application from remaining indefinitely blocked while waiting for the `TWINT` flag.

If a communication error occurs, the current transaction is terminated with a STOP condition whenever possible.

### Slave

The slave monitors the TWI status register inside the `TWI_vect` interrupt service routine.

If `TW_BUS_ERROR` is detected, the current receive operation is discarded, the receive index is reset, and the TWI peripheral is returned to a receptive state.

Unexpected TWI states also reset the current packet reception to prevent incomplete data from being processed as a valid command.

Only packets containing exactly two received bytes are accepted by the application.
