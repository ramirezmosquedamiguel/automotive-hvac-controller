/*
 * Automotive HVAC Control System - Master Controller
 *
 * Microcontroller: ATmega328P
 * CPU frequency:   8 MHz
 *
 * Responsibilities:
 * - Scan a 4x4 keypad and generate debounced key events.
 * - Capture numeric temperature input of up to two digits.
 * - Select OFF, automatic, and manual operating modes.
 * - Cycle manual fan levels (OFF, LOW, MEDIUM, HIGH).
 * - Transmit two-byte application packets over TWI at 100 kHz.
 *
 * TWI application packet:
 *   Byte 0: command
 *   Byte 1: value
 */

#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <util/twi.h>
#include <stdint.h>

/* =========================================================
 * TWI configuration
 * ========================================================= */

#define TWI_FREQUENCY       100000UL
#define TWI_SLAVE_ADDRESS   0x08U
#define TWI_TIMEOUT         60000U

/* =========================================================
 * Application protocol commands
 * ========================================================= */

#define CMD_INPUT_UPDATE    0x03U
#define CMD_INPUT_CLEAR     0x04U
#define CMD_VALUE_CONFIRM   0x05U
#define CMD_MODE_SELECT     0x06U
#define CMD_MANUAL_LEVEL    0x07U

/* =========================================================
 * Operating modes and manual levels
 * ========================================================= */

#define MODE_OFF            0x00U
#define MODE_AUTOMATIC      0x01U
#define MODE_MANUAL         0x02U

#define MANUAL_LEVEL_OFF     0x00U
#define MANUAL_LEVEL_LOW     0x01U
#define MANUAL_LEVEL_MEDIUM  0x02U
#define MANUAL_LEVEL_HIGH    0x03U

/* =========================================================
 * Keypad configuration
 * ========================================================= */

/*
 * Filas:
 *
 * R1 -> PD7
 * R2 -> PD6
 * R3 -> PD5
 * R4 -> PD4
 */
#define KEYPAD_ROW_DDR      DDRD
#define KEYPAD_ROW_PORT     PORTD
#define KEYPAD_ROW_MASK     0xF0U

/*
 * Columnas:
 *
 * C1 -> PC3
 * C2 -> PC2
 * C3 -> PC1
 * C4 -> PC0
 *
 * PC4 y PC5 quedan reservados para SDA y SCL.
 */
#define KEYPAD_COLUMN_DDR   DDRC
#define KEYPAD_COLUMN_PORT  PORTC
#define KEYPAD_COLUMN_PIN   PINC
#define KEYPAD_COLUMN_MASK  0x0FU

#define KEYPAD_NO_KEY       0xFFU
#define KEYPAD_INVALID      0xFEU

/*
 * El keypad se procesa cada 5 ms.
 * Cuatro muestras estables equivalen a 20 ms.
 */
#define KEYPAD_DEBOUNCE_SAMPLES  4U

/* =========================================================
 * Keypad map
 * ========================================================= */

static const uint8_t keypad_map[4][4] =
{
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

/* =========================================================
 * Types
 * ========================================================= */

typedef enum
{
    TWI_RESULT_OK = 0,
    TWI_RESULT_TIMEOUT,
    TWI_RESULT_START_ERROR,
    TWI_RESULT_ADDRESS_NACK,
    TWI_RESULT_DATA_NACK
} twi_result_t;

/* =========================================================
 * Timer1 shared state
 * ========================================================= */

static volatile uint8_t keypad_scan_flag = 0;

/* =========================================================
 * Function prototypes
 * ========================================================= */

static void timer1_init_1ms(void);

static void keypad_init(void);
static uint8_t keypad_scan_raw(void);
static uint8_t keypad_get_event(void);

static void twi_master_init(void);
static uint8_t twi_wait_twint(void);

static twi_result_t twi_start_write(
    uint8_t slave_address
);

static twi_result_t twi_write_byte(
    uint8_t data
);

static void twi_stop(void);

static twi_result_t twi_send_packet(
    uint8_t command,
    uint8_t value
);

/* =========================================================
 * Main program
 * ========================================================= */

int main(void)
{
    /*
     * Último modo enviado correctamente.
     */
    uint8_t selected_mode = MODE_OFF;

    /*
     * Nivel manual actual.
     */
    uint8_t manual_level = MANUAL_LEVEL_OFF;

    /*
     * Próximo nivel manual que se enviará.
     */
    uint8_t next_manual_level = MANUAL_LEVEL_OFF;

    uint8_t key_event;
    uint8_t digit;

    /*
     * Valor numérico capturado.
     */
    uint8_t input_value = 0;

    /*
     * Cantidad de cifras capturadas.
     */
    uint8_t input_digits = 0;

    uint8_t command_to_send = 0;
    uint8_t value_to_send = 0;
    uint8_t send_packet = 0;

    twi_result_t result;

    keypad_init();
    twi_master_init();
    timer1_init_1ms();

    sei();

    while (1)
    {
        if (keypad_scan_flag != 0)
        {
            keypad_scan_flag = 0;

            key_event = keypad_get_event();

            if (key_event == KEYPAD_NO_KEY)
            {
                continue;
            }

            /*
             * Se reinicia antes de procesar cada tecla.
             */
            send_packet = 0;

            /* =========================================
             * Teclas numéricas
             * ========================================= */

            if (
                (key_event >= '0') &&
                (key_event <= '9')
            )
            {
                if (input_digits < 2)
                {
                    digit =
                        (uint8_t)(key_event - '0');

                    if (input_digits == 0)
                    {
                        input_value = digit;
                    }
                    else
                    {
                        input_value =
                            (uint8_t)(
                                input_value * 10U +
                                digit
                            );
                    }

                    input_digits++;

                    command_to_send =
                        CMD_INPUT_UPDATE;

                    value_to_send =
                        input_value;

                    send_packet = 1;
                }
            }

            /* =========================================
             * Borrar captura
             * ========================================= */

            else if (key_event == '*')
            {
                input_value = 0;
                input_digits = 0;

                command_to_send =
                    CMD_INPUT_CLEAR;

                value_to_send = 0;

                send_packet = 1;
            }

            /* =========================================
             * Confirmar valor
             * ========================================= */

            else if (key_event == '#')
            {
                if (input_digits > 0)
                {
                    command_to_send =
                        CMD_VALUE_CONFIRM;

                    value_to_send =
                        input_value;

                    send_packet = 1;
                }
            }

            /* =========================================
             * A: modo automático
             * ========================================= */

            else if (key_event == 'A')
            {
                command_to_send =
                    CMD_MODE_SELECT;

                value_to_send =
                    MODE_AUTOMATIC;

                send_packet = 1;
            }

            /* =========================================
             * B: modo manual
             * ========================================= */

            else if (key_event == 'B')
            {
                command_to_send =
                    CMD_MODE_SELECT;

                value_to_send =
                    MODE_MANUAL;

                send_packet = 1;
            }

            /* =========================================
             * C: sistema apagado
             * ========================================= */

            else if (key_event == 'C')
            {
                command_to_send =
                    CMD_MODE_SELECT;

                value_to_send =
                    MODE_OFF;

                send_packet = 1;
            }

            /* =========================================
             * D: cambiar nivel manual
             * ========================================= */

            else if (key_event == 'D')
            {
                /*
                 * D solamente funciona cuando el modo
                 * manual fue enviado correctamente.
                 */
                if (selected_mode == MODE_MANUAL)
                {
                    next_manual_level =
                        (uint8_t)(
                            manual_level + 1U
                        );

                    if (
                        next_manual_level >
                        MANUAL_LEVEL_HIGH
                    )
                    {
                        next_manual_level =
                            MANUAL_LEVEL_OFF;
                    }

                    command_to_send =
                        CMD_MANUAL_LEVEL;

                    value_to_send =
                        next_manual_level;

                    send_packet = 1;
                }
            }

            /*
             * Cualquier otra tecla se ignora.
             */
            else
            {
                send_packet = 0;
            }

            /* =========================================
             * Transmisión TWI
             * ========================================= */

            if (send_packet != 0)
            {
                result = twi_send_packet(
                    command_to_send,
                    value_to_send
                );

                if (result == TWI_RESULT_OK)
                {
                    /* =================================
                     * Actualización del modo local
                     * ================================= */

                    if (
                        command_to_send ==
                        CMD_MODE_SELECT
                    )
                    {
                        selected_mode =
                            value_to_send;

                        /*
                         * Todo cambio de modo reinicia
                         * el nivel manual.
                         */
                        manual_level =
                            MANUAL_LEVEL_OFF;

                        next_manual_level =
                            MANUAL_LEVEL_OFF;
                    }

                    /* =================================
                     * Actualización del nivel manual
                     * ================================= */

                    else if (
                        command_to_send ==
                        CMD_MANUAL_LEVEL
                    )
                    {
                        manual_level =
                            value_to_send;

                        next_manual_level =
                            value_to_send;
                    }

                    /* =================================
                     * Reinicio de captura numérica
                     * ================================= */

                    if (
                        command_to_send ==
                        CMD_VALUE_CONFIRM
                    )
                    {
                        input_value = 0;
                        input_digits = 0;
                    }
                }
            }
        }
    }
}

/* =========================================================
 * Timer1 interrupt service routine
 * ========================================================= */

ISR(TIMER1_COMPA_vect)
{
    static uint8_t keypad_divider = 0;

    keypad_divider++;

    /*
     * Solicita escaneo cada 5 ms.
     */
    if (keypad_divider >= 5)
    {
        keypad_divider = 0;
        keypad_scan_flag = 1;
    }
}

/* =========================================================
 * Timer1 configuration
 * ========================================================= */

static void timer1_init_1ms(void)
{
    TCCR1A = 0;
    TCCR1B = 0;

    TCNT1 = 0;

    /*
     * F_CPU = 8 MHz
     * Prescaler = 64
     * Periodo = 1 ms
     */
    OCR1A = 124;

    /*
     * Limpia bandera pendiente.
     */
    TIFR1 = (1U << OCF1A);

    /*
     * Habilita interrupción por comparación A.
     */
    TIMSK1 = (1U << OCIE1A);

    /*
     * Modo CTC.
     * Prescaler de 64.
     */
    TCCR1B =
        (1U << WGM12) |
        (1U << CS11)  |
        (1U << CS10);
}

/* =========================================================
 * Keypad initialization
 * ========================================================= */

static void keypad_init(void)
{
    /*
     * Filas inicialmente como entradas con pull-up.
     */
    KEYPAD_ROW_DDR &=
        (uint8_t)~KEYPAD_ROW_MASK;

    KEYPAD_ROW_PORT |=
        KEYPAD_ROW_MASK;

    /*
     * Columnas PC0-PC3 como entradas con pull-up.
     *
     * PC4 y PC5 no se modifican porque pertenecen
     * al periférico TWI.
     */
    KEYPAD_COLUMN_DDR &=
        (uint8_t)~KEYPAD_COLUMN_MASK;

    KEYPAD_COLUMN_PORT |=
        KEYPAD_COLUMN_MASK;
}

/* =========================================================
 * Keypad electrical scan
 * ========================================================= */

static uint8_t keypad_scan_raw(void)
{
    uint8_t row;
    uint8_t column;

    uint8_t row_pin;
    uint8_t column_pin;

    uint8_t detected_key =
        KEYPAD_NO_KEY;

    for (row = 0; row < 4; row++)
    {
        /*
         * Todas las filas como entradas con pull-up.
         */
        KEYPAD_ROW_DDR &=
            (uint8_t)~KEYPAD_ROW_MASK;

        KEYPAD_ROW_PORT |=
            KEYPAD_ROW_MASK;

        /*
         * R1 = PD7
         * R2 = PD6
         * R3 = PD5
         * R4 = PD4
         */
        row_pin =
            (uint8_t)(PD7 - row);

        /*
         * Prepara nivel bajo antes de convertir
         * la fila seleccionada en salida.
         */
        KEYPAD_ROW_PORT &=
            (uint8_t)~(1U << row_pin);

        KEYPAD_ROW_DDR |=
            (1U << row_pin);

        _delay_us(5);

        for (column = 0; column < 4; column++)
        {
            /*
             * C1 = PC3
             * C2 = PC2
             * C3 = PC1
             * C4 = PC0
             */
            column_pin =
                (uint8_t)(PC3 - column);

            if (
                (
                    KEYPAD_COLUMN_PIN &
                    (1U << column_pin)
                ) == 0
            )
            {
                /*
                 * Si ya se detectó otra tecla,
                 * se considera combinación inválida.
                 */
                if (
                    detected_key !=
                    KEYPAD_NO_KEY
                )
                {
                    detected_key =
                        KEYPAD_INVALID;
                }
                else
                {
                    detected_key =
                        keypad_map[row][column];
                }
            }
        }
    }

    /*
     * Estado seguro al terminar el escaneo.
     */
    KEYPAD_ROW_DDR &=
        (uint8_t)~KEYPAD_ROW_MASK;

    KEYPAD_ROW_PORT |=
        KEYPAD_ROW_MASK;

    return detected_key;
}

/* =========================================================
 * Debouncing and event generation
 * ========================================================= */

static uint8_t keypad_get_event(void)
{
    static uint8_t candidate_key =
        KEYPAD_NO_KEY;

    static uint8_t stable_samples = 0;

    /*
     * Impide generar repeticiones mientras una tecla
     * permanezca presionada.
     */
    static uint8_t keypad_locked = 0;

    uint8_t raw_key;

    raw_key = keypad_scan_raw();

    /*
     * Combinaciones múltiples se ignoran.
     */
    if (raw_key == KEYPAD_INVALID)
    {
        candidate_key =
            KEYPAD_INVALID;

        stable_samples = 0;

        return KEYPAD_NO_KEY;
    }

    if (raw_key == candidate_key)
    {
        if (stable_samples < 255)
        {
            stable_samples++;
        }
    }
    else
    {
        candidate_key = raw_key;
        stable_samples = 1;
    }

    /*
     * Todavía no existe suficiente estabilidad.
     */
    if (
        stable_samples <
        KEYPAD_DEBOUNCE_SAMPLES
    )
    {
        return KEYPAD_NO_KEY;
    }

    /*
     * Liberación estable.
     */
    if (candidate_key == KEYPAD_NO_KEY)
    {
        keypad_locked = 0;

        return KEYPAD_NO_KEY;
    }

    /*
     * Primera pulsación estable.
     */
    if (keypad_locked == 0)
    {
        keypad_locked = 1;

        return candidate_key;
    }

    /*
     * La tecla permanece presionada.
     */
    return KEYPAD_NO_KEY;
}

/* =========================================================
 * TWI master configuration
 * ========================================================= */

static void twi_master_init(void)
{
    /*
     * Prescaler TWI = 1.
     */
    TWSR &=
        (uint8_t)~(
            (1U << TWPS1) |
            (1U << TWPS0)
        );

    /*
     * F_CPU = 8 MHz
     * SCL = 100 kHz
     * TWBR = 32
     */
    TWBR = (uint8_t)(
        (
            (F_CPU / TWI_FREQUENCY) -
            16UL
        ) / 2UL
    );

    TWCR = (1U << TWEN);
}

/* =========================================================
 * TWINT wait with timeout
 * ========================================================= */

static uint8_t twi_wait_twint(void)
{
    uint16_t timeout = TWI_TIMEOUT;

    while (
        (TWCR & (1U << TWINT)) == 0
    )
    {
        timeout--;

        if (timeout == 0)
        {
            return 0;
        }
    }

    return 1;
}

/* =========================================================
 * START and SLA+W address
 * ========================================================= */

static twi_result_t twi_start_write(
    uint8_t slave_address)
{
    uint8_t status;

    /*
     * Genera START.
     */
    TWCR =
        (1U << TWINT) |
        (1U << TWSTA) |
        (1U << TWEN);

    if (!twi_wait_twint())
    {
        return TWI_RESULT_TIMEOUT;
    }

    status = TWSR & 0xF8;

    if (
        (status != TW_START) &&
        (status != TW_REP_START)
    )
    {
        return TWI_RESULT_START_ERROR;
    }

    /*
     * Envía SLA+W.
     */
    TWDR = (uint8_t)(
        (slave_address << 1) |
        TW_WRITE
    );

    TWCR =
        (1U << TWINT) |
        (1U << TWEN);

    if (!twi_wait_twint())
    {
        return TWI_RESULT_TIMEOUT;
    }

    status = TWSR & 0xF8;

    if (status != TW_MT_SLA_ACK)
    {
        return TWI_RESULT_ADDRESS_NACK;
    }

    return TWI_RESULT_OK;
}

/* =========================================================
 * Byte transmission
 * ========================================================= */

static twi_result_t twi_write_byte(
    uint8_t data)
{
    uint8_t status;

    TWDR = data;

    TWCR =
        (1U << TWINT) |
        (1U << TWEN);

    if (!twi_wait_twint())
    {
        return TWI_RESULT_TIMEOUT;
    }

    status = TWSR & 0xF8;

    if (status != TW_MT_DATA_ACK)
    {
        return TWI_RESULT_DATA_NACK;
    }

    return TWI_RESULT_OK;
}

/* =========================================================
 * STOP
 * ========================================================= */

static void twi_stop(void)
{
    uint16_t timeout = TWI_TIMEOUT;

    TWCR =
        (1U << TWINT) |
        (1U << TWSTO) |
        (1U << TWEN);

    while (
        (TWCR & (1U << TWSTO)) != 0
    )
    {
        timeout--;

        if (timeout == 0)
        {
            break;
        }
    }
}

/* =========================================================
 * Two-byte packet transmission
 * ========================================================= */

static twi_result_t twi_send_packet(
    uint8_t command,
    uint8_t value)
{
    twi_result_t result;

    result =
        twi_start_write(
            TWI_SLAVE_ADDRESS
        );

    if (result != TWI_RESULT_OK)
    {
        twi_stop();
        return result;
    }

    result =
        twi_write_byte(command);

    if (result != TWI_RESULT_OK)
    {
        twi_stop();
        return result;
    }

    result =
        twi_write_byte(value);

    twi_stop();

    return result;
}
