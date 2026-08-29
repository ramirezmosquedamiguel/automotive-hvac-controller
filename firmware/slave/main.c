/*
 * Automotive HVAC Control System - Slave Controller
 *
 * Microcontroller: ATmega328P
 * CPU frequency:   8 MHz
 *
 * Responsibilities:
 * - Receive two-byte application packets through TWI interrupts.
 * - Read the LM35 interior-temperature sensor through ADC0.
 * - Accept the keypad-entered exterior temperature through INT1.
 * - Evaluate OFF, automatic, and manual HVAC operating modes.
 * - Generate fan PWM with Timer0 on OC0A/PD6.
 * - Drive a DC fan in one direction through an L293D.
 * - Maintain the user interface on a 16x2 LCD.
 * - Use Timer1 as a 1 ms system tick for sampling, UI timing, and
 *   button debouncing.
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
#include <util/atomic.h>
#include <stdint.h>

/* =========================================================
 * LM35 and ADC configuration
 * ========================================================= */

#define LM35_ADC_CHANNEL       0U
#define LM35_SAMPLE_PERIOD_MS  500U
#define LM35_AVERAGE_SAMPLES   16U

/* Cada unidad equivale a 100 ms. */
#define UI_HOLD_TICKS          15U

/* =========================================================
 * LCD connections
 * ========================================================= */

#define LCD_DDR   DDRB
#define LCD_PORT  PORTB

#define LCD_D4    PB0
#define LCD_D5    PB1
#define LCD_D6    PB2
#define LCD_D7    PB3
#define LCD_EN    PB4
#define LCD_RS    PB5

#define LCD_DATA_MASK ( \
    (1U << LCD_D4) |   \
    (1U << LCD_D5) |   \
    (1U << LCD_D6) |   \
    (1U << LCD_D7))

#define LCD_CONTROL_MASK ( \
    (1U << LCD_EN) |      \
    (1U << LCD_RS))

/* =========================================================
 * TWI configuration
 * ========================================================= */

#define TWI_SLAVE_ADDRESS  0x08U
#define TWI_PACKET_SIZE    2U

/* =========================================================
 * Application protocol commands
 * ========================================================= */

#define CMD_INPUT_UPDATE   0x03U
#define CMD_INPUT_CLEAR    0x04U
#define CMD_VALUE_CONFIRM  0x05U
#define CMD_MODE_SELECT    0x06U
#define CMD_MANUAL_LEVEL   0x07U

/* =========================================================
 * Operating modes and manual levels
 * ========================================================= */

#define MODE_OFF           0x00U
#define MODE_AUTOMATIC     0x01U
#define MODE_MANUAL        0x02U

#define MANUAL_LEVEL_OFF     0x00U
#define MANUAL_LEVEL_LOW     0x01U
#define MANUAL_LEVEL_MEDIUM  0x02U
#define MANUAL_LEVEL_HIGH    0x03U

/* =========================================================
 * Automatic control and buttons
 * ========================================================= */

#define AUTO_DEADBAND_C     2
#define BUTTON_DEBOUNCE_MS  30U

/* =========================================================
 * Fan and L293D configuration
 * ========================================================= */

/* OC0A / PD6 se conecta a EN1 del L293D. */
#define FAN_PWM_DDR        DDRD
#define FAN_PWM_PORT       PORTD
#define FAN_PWM_PIN        PD6

/* Entradas de dirección del canal 1 del L293D. */
#define FAN_DIRECTION_DDR  DDRD
#define FAN_DIRECTION_PORT PORTD
#define FAN_IN1_PIN        PD4
#define FAN_IN2_PIN        PD5

/* Niveles PWM de 8 bits. */
#define FAN_PWM_OFF        0U
#define FAN_PWM_LOW        85U
#define FAN_PWM_MEDIUM     170U
#define FAN_PWM_HIGH       255U

/* En automático, COOLING utiliza velocidad alta. */
#define FAN_PWM_AUTOMATIC  FAN_PWM_HIGH

/* =========================================================
 * Types
 * ========================================================= */

typedef enum
{
    CONTROL_WAIT_TEMPERATURES = 0,
    CONTROL_STABLE,
    CONTROL_COOLING,
    CONTROL_HEATING
} control_state_t;

/* =========================================================
 * Interrupt-shared state
 * ========================================================= */

static volatile uint8_t twi_rx_buffer[TWI_PACKET_SIZE];
static volatile uint8_t twi_rx_index = 0;
static volatile uint8_t received_command = 0;
static volatile uint8_t received_value = 0;
static volatile uint8_t packet_ready = 0;
static volatile uint8_t twi_last_error = 0;

static volatile uint8_t lm35_sample_flag = 0;
static volatile uint8_t ui_hold_ticks = 0;

static volatile uint8_t exterior_button_request = 0;
static volatile uint8_t int1_debounce_ms = 0;

/* =========================================================
 * Application state
 * ========================================================= */

static uint8_t pending_value = 0;
static uint8_t pending_value_valid = 0;

static uint8_t interior_temperature = 0;
static uint8_t exterior_temperature = 0;
static uint8_t interior_temperature_valid = 0;
static uint8_t exterior_temperature_valid = 0;

static uint8_t operating_mode = MODE_OFF;
static uint8_t manual_level = MANUAL_LEVEL_OFF;

static control_state_t control_state =
    CONTROL_WAIT_TEMPERATURES;

static uint8_t control_refresh_request = 1;
static uint8_t status_display_pending = 1;

/* =========================================================
 * LCD function prototypes
 * ========================================================= */

static void lcd_init(void);
static void lcd_pulse_enable(void);
static void lcd_write_nibble(uint8_t nibble);
static void lcd_send(uint8_t value, uint8_t register_select);
static void lcd_command(uint8_t command);
static void lcd_data(uint8_t data);
static void lcd_clear(void);
static void lcd_set_cursor(uint8_t row, uint8_t column);
static void lcd_print(const char *text);
static void lcd_print_hex8(uint8_t value);
static void lcd_print_u8_2digits(uint8_t value);
static void lcd_print_degree_c(void);
static void lcd_print_temperature(uint8_t valid, uint8_t value);
static void lcd_show_temperature_status(const char *title);

/* =========================================================
 * Peripheral and control prototypes
 * ========================================================= */

static void timer1_init_1ms(void);
static void timer0_pwm_init(void);
static void fan_set_pwm(uint8_t duty);
static void fan_stop(void);
static void fan_apply_control(void);

static void twi_slave_init(void);
static void buttons_init(void);

static void process_button_requests(void);
static void automatic_control_evaluate(void);
static void control_show_status(void);

static void adc_init(void);
static uint16_t adc_read(uint8_t channel);
static uint16_t adc_read_average(uint8_t channel, uint8_t samples);
static uint16_t lm35_read_temperature_x10(void);
static void process_lm35_sample(void);

/* =========================================================
 * Main program
 * ========================================================= */

int main(void)
{
    uint8_t command = 0;
    uint8_t value = 0;

    lcd_init();

    lcd_set_cursor(0, 0);
    lcd_print("TWI Slave 0x08  ");

    lcd_set_cursor(1, 0);
    lcd_print("Waiting master..");

    adc_init();
    timer0_pwm_init();
    timer1_init_1ms();
    twi_slave_init();
    buttons_init();

    /* Solicita una primera lectura inmediatamente. */
    lm35_sample_flag = 1;

    sei();

    while (1)
    {
        /* =============================================
         * Procesamiento de paquetes TWI
         * ============================================= */

        if (packet_ready != 0)
        {
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
            {
                command = received_command;
                value = received_value;
                packet_ready = 0;
            }

            switch (command)
            {
                case CMD_INPUT_UPDATE:

                    lcd_set_cursor(0, 0);
                    lcd_print("ENTER VALUE     ");

                    lcd_set_cursor(1, 0);
                    lcd_print("Input:");
                    lcd_print_u8_2digits(value);
                    lcd_print_degree_c();
                    lcd_print("      ");

                    ui_hold_ticks = UI_HOLD_TICKS;

                    break;

                case CMD_INPUT_CLEAR:

                    lcd_set_cursor(0, 0);
                    lcd_print("ENTRY CLEARED   ");

                    lcd_set_cursor(1, 0);
                    lcd_print("Input: --       ");

                    ui_hold_ticks = UI_HOLD_TICKS;

                    break;

                case CMD_VALUE_CONFIRM:

                    pending_value = value;
                    pending_value_valid = 1;

                    lcd_set_cursor(0, 0);
                    lcd_print("VALUE CONFIRMED ");

                    lcd_set_cursor(1, 0);
                    lcd_print("Pending:");
                    lcd_print_u8_2digits(pending_value);
                    lcd_print_degree_c();
                    lcd_print("    ");

                    ui_hold_ticks = UI_HOLD_TICKS;

                    break;

                case CMD_MODE_SELECT:

                    if (
                        (value == MODE_OFF) ||
                        (value == MODE_AUTOMATIC) ||
                        (value == MODE_MANUAL)
                    )
                    {
                        operating_mode = value;

                        /* Todo cambio de modo inicia con
                         * el nivel manual apagado. */
                        manual_level = MANUAL_LEVEL_OFF;

                        ui_hold_ticks = 0;
                        control_refresh_request = 1;
                    }
                    else
                    {
                        lcd_set_cursor(0, 0);
                        lcd_print("INVALID MODE    ");

                        lcd_set_cursor(1, 0);
                        lcd_print("Value:");
                        lcd_print_hex8(value);
                        lcd_print("        ");

                        ui_hold_ticks = UI_HOLD_TICKS;
                    }

                    break;

                case CMD_MANUAL_LEVEL:

                    if (operating_mode != MODE_MANUAL)
                    {
                        lcd_set_cursor(0, 0);
                        lcd_print("NOT MANUAL MODE ");

                        lcd_set_cursor(1, 0);
                        lcd_print("Command ignored ");

                        ui_hold_ticks = UI_HOLD_TICKS;

                        break;
                    }

                    if (value <= MANUAL_LEVEL_HIGH)
                    {
                        manual_level = value;

                        ui_hold_ticks = 0;
                        control_refresh_request = 1;
                    }
                    else
                    {
                        lcd_set_cursor(0, 0);
                        lcd_print("INVALID LEVEL   ");

                        lcd_set_cursor(1, 0);
                        lcd_print("Value:");
                        lcd_print_hex8(value);
                        lcd_print("        ");

                        ui_hold_ticks = UI_HOLD_TICKS;
                    }

                    break;

                default:

                    lcd_set_cursor(0, 0);
                    lcd_print("UNKNOWN COMMAND ");

                    lcd_set_cursor(1, 0);
                    lcd_print("CMD:");
                    lcd_print_hex8(command);
                    lcd_print(" VAL:");
                    lcd_print_hex8(value);
                    lcd_print("   ");

                    ui_hold_ticks = UI_HOLD_TICKS;

                    break;
            }
        }

        /* Los botones, el LM35 y el control deben procesarse
         * aunque no haya llegado un paquete TWI. */
        process_button_requests();

        if (lm35_sample_flag != 0)
        {
            lm35_sample_flag = 0;
            process_lm35_sample();
        }

        /* La lógica y el actuador se actualizan inmediatamente.
         * La retención de interfaz solo aplaza el mensaje del LCD. */
        if (control_refresh_request != 0)
        {
            control_refresh_request = 0;

            automatic_control_evaluate();
            fan_apply_control();

            if (ui_hold_ticks == 0)
            {
                control_show_status();
                status_display_pending = 0;
            }
            else
            {
                status_display_pending = 1;
            }
        }

        /* Cuando termina el mensaje temporal, muestra el estado
         * más reciente sin volver a modificar el actuador. */
        if (
            (status_display_pending != 0) &&
            (ui_hold_ticks == 0)
        )
        {
            status_display_pending = 0;
            control_show_status();
        }
    }
}

/* =========================================================
 * Timer0 Fast PWM and fan control
 * ========================================================= */

static void timer0_pwm_init(void)
{
    /*
     * PD6 / OC0A como salida PWM.
     * PD4 y PD5 como salidas de dirección del L293D.
     */
    FAN_PWM_DDR |= (1U << FAN_PWM_PIN);

    FAN_DIRECTION_DDR |=
        (1U << FAN_IN1_PIN) |
        (1U << FAN_IN2_PIN);

    /* Estado seguro durante la inicialización. */
    FAN_PWM_PORT &= (uint8_t)~(1U << FAN_PWM_PIN);

    FAN_DIRECTION_PORT &= (uint8_t)~(
        (1U << FAN_IN1_PIN) |
        (1U << FAN_IN2_PIN)
    );

    /*
     * Timer0 en Fast PWM, TOP = 0xFF.
     * WGM01:WGM00 = 11, WGM02 = 0.
     * OC0A en modo no inversor: COM0A1:COM0A0 = 10.
     */
    TCCR0A =
        (1U << COM0A1) |
        (1U << WGM01)  |
        (1U << WGM00);

    /*
     * Prescaler de 8: CS01 = 1.
     * fPWM = 8 MHz / (8 x 256) = 3906.25 Hz.
     */
    TCCR0B = (1U << CS01);

    TCNT0 = 0;
    OCR0A = FAN_PWM_OFF;
}

static void fan_set_pwm(uint8_t duty)
{
    if (duty == FAN_PWM_OFF)
    {
        fan_stop();
        return;
    }

    /* Giro único para el ventilador: IN1 = 1, IN2 = 0. */
    FAN_DIRECTION_PORT |= (1U << FAN_IN1_PIN);
    FAN_DIRECTION_PORT &= (uint8_t)~(1U << FAN_IN2_PIN);

    OCR0A = duty;
}

static void fan_stop(void)
{
    /*
     * OCR0A = 0 deshabilita el ciclo útil.
     * Ambas entradas en cero dejan el canal en estado seguro.
     */
    OCR0A = FAN_PWM_OFF;

    FAN_DIRECTION_PORT &= (uint8_t)~(
        (1U << FAN_IN1_PIN) |
        (1U << FAN_IN2_PIN)
    );
}

static void fan_apply_control(void)
{
    uint8_t requested_pwm = FAN_PWM_OFF;

    switch (operating_mode)
    {
        case MODE_MANUAL:

            switch (manual_level)
            {
                case MANUAL_LEVEL_LOW:
                    requested_pwm = FAN_PWM_LOW;
                    break;

                case MANUAL_LEVEL_MEDIUM:
                    requested_pwm = FAN_PWM_MEDIUM;
                    break;

                case MANUAL_LEVEL_HIGH:
                    requested_pwm = FAN_PWM_HIGH;
                    break;

                case MANUAL_LEVEL_OFF:
                default:
                    requested_pwm = FAN_PWM_OFF;
                    break;
            }

            break;

        case MODE_AUTOMATIC:

            /* Solo COOLING acciona el ventilador en esta etapa. */
            if (control_state == CONTROL_COOLING)
            {
                requested_pwm = FAN_PWM_AUTOMATIC;
            }
            else
            {
                requested_pwm = FAN_PWM_OFF;
            }

            break;

        case MODE_OFF:
        default:
            requested_pwm = FAN_PWM_OFF;
            break;
    }

    fan_set_pwm(requested_pwm);
}

/* =========================================================
 * Timer1 interrupt service routine
 * ========================================================= */

ISR(TIMER1_COMPA_vect)
{
    static uint16_t lm35_period_counter = 0;
    static uint8_t ui_100ms_counter = 0;

    if (int1_debounce_ms > 0)
    {
        int1_debounce_ms--;
    }

    lm35_period_counter++;

    if (lm35_period_counter >= LM35_SAMPLE_PERIOD_MS)
    {
        lm35_period_counter = 0;
        lm35_sample_flag = 1;
    }

    ui_100ms_counter++;

    if (ui_100ms_counter >= 100U)
    {
        ui_100ms_counter = 0;

        if (ui_hold_ticks > 0)
        {
            ui_hold_ticks--;
        }
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

    /* 8 MHz / 64 / (124 + 1) = 1 kHz. */
    OCR1A = 124;

    TIFR1 = (1U << OCF1A);
    TIMSK1 = (1U << OCIE1A);

    TCCR1B =
        (1U << WGM12) |
        (1U << CS11)  |
        (1U << CS10);
}

/* =========================================================
 * LCD driver
 * ========================================================= */

static void lcd_init(void)
{
    LCD_DDR |= LCD_DATA_MASK | LCD_CONTROL_MASK;
    LCD_PORT &= (uint8_t)~(LCD_DATA_MASK | LCD_CONTROL_MASK);

    _delay_ms(40);

    LCD_PORT &= (uint8_t)~(1U << LCD_RS);

    lcd_write_nibble(0x03);
    _delay_ms(5);

    lcd_write_nibble(0x03);
    _delay_us(150);

    lcd_write_nibble(0x03);
    _delay_us(150);

    lcd_write_nibble(0x02);
    _delay_us(150);

    lcd_command(0x28);
    lcd_command(0x08);
    lcd_clear();
    lcd_command(0x06);
    lcd_command(0x0C);
}

static void lcd_pulse_enable(void)
{
    LCD_PORT |= (1U << LCD_EN);
    _delay_us(1);

    LCD_PORT &= (uint8_t)~(1U << LCD_EN);
    _delay_us(50);
}

static void lcd_write_nibble(uint8_t nibble)
{
    LCD_PORT &= (uint8_t)~LCD_DATA_MASK;

    if ((nibble & (1U << 0)) != 0)
    {
        LCD_PORT |= (1U << LCD_D4);
    }

    if ((nibble & (1U << 1)) != 0)
    {
        LCD_PORT |= (1U << LCD_D5);
    }

    if ((nibble & (1U << 2)) != 0)
    {
        LCD_PORT |= (1U << LCD_D6);
    }

    if ((nibble & (1U << 3)) != 0)
    {
        LCD_PORT |= (1U << LCD_D7);
    }

    lcd_pulse_enable();
}

static void lcd_send(uint8_t value, uint8_t register_select)
{
    if (register_select != 0)
    {
        LCD_PORT |= (1U << LCD_RS);
    }
    else
    {
        LCD_PORT &= (uint8_t)~(1U << LCD_RS);
    }

    lcd_write_nibble(value >> 4);
    lcd_write_nibble(value & 0x0F);
}

static void lcd_command(uint8_t command)
{
    lcd_send(command, 0);

    if ((command == 0x01) || (command == 0x02))
    {
        _delay_ms(2);
    }
    else
    {
        _delay_us(50);
    }
}

static void lcd_data(uint8_t data)
{
    lcd_send(data, 1);
    _delay_us(50);
}

static void lcd_clear(void)
{
    lcd_command(0x01);
}

static void lcd_set_cursor(uint8_t row, uint8_t column)
{
    uint8_t address;

    if (column > 15)
    {
        column = 15;
    }

    if (row == 0)
    {
        address = column;
    }
    else
    {
        address = (uint8_t)(0x40U + column);
    }

    lcd_command((uint8_t)(0x80U | address));
}

static void lcd_print(const char *text)
{
    while (*text != '\0')
    {
        lcd_data((uint8_t)*text);
        text++;
    }
}

static void lcd_print_hex8(uint8_t value)
{
    static const char hex_digits[] = "0123456789ABCDEF";

    lcd_data((uint8_t)hex_digits[(value >> 4) & 0x0FU]);
    lcd_data((uint8_t)hex_digits[value & 0x0FU]);
}

static void lcd_print_u8_2digits(uint8_t value)
{
    if (value > 99U)
    {
        value = 99U;
    }

    if (value < 10U)
    {
        lcd_data(' ');
    }
    else
    {
        lcd_data((uint8_t)('0' + (value / 10U)));
    }

    lcd_data((uint8_t)('0' + (value % 10U)));
}

static void lcd_print_degree_c(void)
{
    lcd_data(0xDF);
    lcd_data('C');
}

static void lcd_print_temperature(uint8_t valid, uint8_t value)
{
    if (valid != 0)
    {
        lcd_print_u8_2digits(value);
    }
    else
    {
        lcd_print("--");
    }

    lcd_print_degree_c();
}

static void lcd_show_temperature_status(const char *title)
{
    lcd_set_cursor(0, 0);
    lcd_print(title);

    lcd_set_cursor(1, 0);

    lcd_print("Ti:");
    lcd_print_temperature(
        interior_temperature_valid,
        interior_temperature
    );

    lcd_data(' ');

    lcd_print("Te:");
    lcd_print_temperature(
        exterior_temperature_valid,
        exterior_temperature
    );

    lcd_data(' ');
}

/* =========================================================
 * TWI configuration and interrupt service routine
 * ========================================================= */

static void twi_slave_init(void)
{
    TWAR = (uint8_t)(TWI_SLAVE_ADDRESS << 1);

    TWSR &= (uint8_t)~(
        (1U << TWPS1) |
        (1U << TWPS0)
    );

    TWCR =
        (1U << TWINT) |
        (1U << TWEA)  |
        (1U << TWEN)  |
        (1U << TWIE);
}

ISR(TWI_vect)
{
    uint8_t status = TWSR & 0xF8U;

    switch (status)
    {
        case TW_SR_SLA_ACK:
        case TW_SR_ARB_LOST_SLA_ACK:

            twi_rx_index = 0;

            TWCR =
                (1U << TWINT) |
                (1U << TWEA)  |
                (1U << TWEN)  |
                (1U << TWIE);

            break;

        case TW_SR_DATA_ACK:

            if (twi_rx_index < TWI_PACKET_SIZE)
            {
                twi_rx_buffer[twi_rx_index] = TWDR;
                twi_rx_index++;
            }
            else
            {
                (void)TWDR;
            }

            TWCR =
                (1U << TWINT) |
                (1U << TWEA)  |
                (1U << TWEN)  |
                (1U << TWIE);

            break;

        case TW_SR_STOP:

            if (twi_rx_index == TWI_PACKET_SIZE)
            {
                received_command = twi_rx_buffer[0];
                received_value = twi_rx_buffer[1];
                packet_ready = 1;
            }

            twi_rx_index = 0;

            TWCR =
                (1U << TWINT) |
                (1U << TWEA)  |
                (1U << TWEN)  |
                (1U << TWIE);

            break;

        case TW_BUS_ERROR:

            twi_last_error = status;
            twi_rx_index = 0;

            TWCR =
                (1U << TWINT) |
                (1U << TWSTO) |
                (1U << TWEA)  |
                (1U << TWEN)  |
                (1U << TWIE);

            break;

        default:

            twi_last_error = status;
            twi_rx_index = 0;

            TWCR =
                (1U << TWINT) |
                (1U << TWEA)  |
                (1U << TWEN)  |
                (1U << TWIE);

            break;
    }
}

/* =========================================================
 * INT1 configuration and interrupt service routine
 * ========================================================= */

static void buttons_init(void)
{
    /* PD2 queda reservado. PD3 conserva INT1. */
    DDRD &= (uint8_t)~(
        (1U << DDD2) |
        (1U << DDD3)
    );

    PORTD |=
        (1U << PORTD2) |
        (1U << PORTD3);

    EICRA &= (uint8_t)~(
        (1U << ISC11) |
        (1U << ISC10) |
        (1U << ISC01) |
        (1U << ISC00)
    );

    /* INT1 por flanco descendente. */
    EICRA |= (1U << ISC11);

    EIFR =
        (1U << INTF0) |
        (1U << INTF1);

    EIMSK &= (uint8_t)~(1U << INT0);
    EIMSK |= (1U << INT1);
}

ISR(INT1_vect)
{
    if (int1_debounce_ms == 0)
    {
        exterior_button_request = 1;
        int1_debounce_ms = BUTTON_DEBOUNCE_MS;
    }
}

static void process_button_requests(void)
{
    uint8_t exterior_request;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        exterior_request = exterior_button_request;
        exterior_button_request = 0;
    }

    if (exterior_request == 0)
    {
        return;
    }

    if (pending_value_valid == 0)
    {
        lcd_show_temperature_status("NO VALUE PENDING");
        ui_hold_ticks = UI_HOLD_TICKS;
        return;
    }

    exterior_temperature = pending_value;
    exterior_temperature_valid = 1;
    pending_value_valid = 0;

    ui_hold_ticks = 0;
    control_refresh_request = 1;
}

/* =========================================================
 * Automatic control and status display
 * ========================================================= */

static void automatic_control_evaluate(void)
{
    int16_t temperature_difference;

    if (operating_mode != MODE_AUTOMATIC)
    {
        control_state = CONTROL_WAIT_TEMPERATURES;
        return;
    }

    if (
        (interior_temperature_valid == 0) ||
        (exterior_temperature_valid == 0)
    )
    {
        control_state = CONTROL_WAIT_TEMPERATURES;
        return;
    }

    temperature_difference =
        (int16_t)interior_temperature -
        (int16_t)exterior_temperature;

    if (temperature_difference > AUTO_DEADBAND_C)
    {
        control_state = CONTROL_COOLING;
    }
    else if (temperature_difference < -AUTO_DEADBAND_C)
    {
        control_state = CONTROL_HEATING;
    }
    else
    {
        control_state = CONTROL_STABLE;
    }
}

static void control_show_status(void)
{
    const char *title;

    switch (operating_mode)
    {
        case MODE_OFF:
            title = "SYSTEM OFF      ";
            break;

        case MODE_MANUAL:

            switch (manual_level)
            {
                case MANUAL_LEVEL_OFF:
                    title = "MANUAL: OFF     ";
                    break;

                case MANUAL_LEVEL_LOW:
                    title = "MANUAL: LOW     ";
                    break;

                case MANUAL_LEVEL_MEDIUM:
                    title = "MANUAL: MEDIUM  ";
                    break;

                case MANUAL_LEVEL_HIGH:
                    title = "MANUAL: HIGH    ";
                    break;

                default:
                    title = "MANUAL: ERROR   ";
                    break;
            }

            break;

        case MODE_AUTOMATIC:

            switch (control_state)
            {
                case CONTROL_COOLING:
                    title = "AUTO: COOLING   ";
                    break;

                case CONTROL_HEATING:
                    title = "AUTO: HEATING   ";
                    break;

                case CONTROL_STABLE:
                    title = "AUTO: STABLE    ";
                    break;

                case CONTROL_WAIT_TEMPERATURES:
                default:
                    title = "AUTO: WAIT TEMPS";
                    break;
            }

            break;

        default:
            title = "INVALID MODE    ";
            break;
    }

    lcd_show_temperature_status(title);
}

/* =========================================================
 * ADC and LM35
 * ========================================================= */

static void adc_init(void)
{
    DDRC &= (uint8_t)~(1U << DDC0);
    PORTC &= (uint8_t)~(1U << PORTC0);

    /* Referencia interna de 1.1 V, resultado a la derecha,
     * canal ADC0. */
    ADMUX =
        (1U << REFS1) |
        (1U << REFS0);

    ADCSRB = 0;
    DIDR0 |= (1U << ADC0D);

    /* Habilita ADC, prescaler 64: 8 MHz / 64 = 125 kHz. */
    ADCSRA =
        (1U << ADEN)  |
        (1U << ADPS2) |
        (1U << ADPS1);

    _delay_ms(2);

    /* Descarta la primera conversión. */
    (void)adc_read(LM35_ADC_CHANNEL);
}

static uint16_t adc_read(uint8_t channel)
{
    channel &= 0x07U;

    ADMUX = (uint8_t)(
        (ADMUX & 0xF0U) |
        channel
    );

    ADCSRA |= (1U << ADSC);

    while ((ADCSRA & (1U << ADSC)) != 0)
    {
    }

    return ADC;
}

static uint16_t adc_read_average(uint8_t channel, uint8_t samples)
{
    uint8_t index;
    uint32_t accumulator = 0;

    if (samples == 0)
    {
        return 0;
    }

    for (index = 0; index < samples; index++)
    {
        accumulator += adc_read(channel);
    }

    return (uint16_t)(
        (accumulator + (samples / 2U)) /
        samples
    );
}

static uint16_t lm35_read_temperature_x10(void)
{
    uint16_t adc_value;

    adc_value = adc_read_average(
        LM35_ADC_CHANNEL,
        LM35_AVERAGE_SAMPLES
    );

    return (uint16_t)(
        ((uint32_t)adc_value * 1100UL + 512UL) /
        1024UL
    );
}

static void process_lm35_sample(void)
{
    uint16_t temperature_x10;
    uint16_t rounded_temperature;

    temperature_x10 = lm35_read_temperature_x10();
    rounded_temperature = (temperature_x10 + 5U) / 10U;

    if (rounded_temperature > 99U)
    {
        rounded_temperature = 99U;
    }

    if (
        (interior_temperature_valid == 0) ||
        (interior_temperature != (uint8_t)rounded_temperature)
    )
    {
        interior_temperature = (uint8_t)rounded_temperature;
        interior_temperature_valid = 1;
        control_refresh_request = 1;
    }
}
