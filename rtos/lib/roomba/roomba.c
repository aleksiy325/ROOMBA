/*
 * roomba.c
 *
 *  Created on: 4-Feb-2009
 *      Author: nrqm
 */

#define F_CPU 16000000UL

#include <util/delay.h>
#include "../uart/uart.h"
#include "roomba.h"
#include "roomba_sci.h"
#include "sensor_struct.h"
#include "../os/os.h"

#define LOW_BYTE(v)   ((unsigned char) (v))
#define HIGH_BYTE(v)  ((unsigned char) (((unsigned int) (v)) >> 8))

#define DD_DDR DDRC
#define DD_PORT PORTC
#define DD_PIN PC5
#define ROOMBA_UART 1
#define BT_UART 2
#define TIMEOUT_MS 50

STATUS_LED_STATE status = LED_OFF;
LED_STATE spot = LED_OFF;
LED_STATE clean = LED_OFF;
LED_STATE max = LED_OFF;
LED_STATE dd = LED_OFF;
uint8_t power_colour = 0;		// green
uint8_t power_intensity = 255;	// full intensity

ROOMBA_STATE state = SAFE_MODE;

static void update_leds();

void Roomba_Init()
{
	uint8_t i;
	DD_DDR |= _BV(DD_PIN);
	// Wake up the Roomba by driving the DD pin low for 500 ms.
	DD_PORT &= ~_BV(DD_PIN);
	_delay_ms(500);
	DD_PORT |= _BV(DD_PIN);

	// Wait for 2 seconds, Then pulse the DD pin 3 times to set the Roomba to operate at 19200 baud.
	// This ensures that we know what baud rate to talk at.
	_delay_ms(2000);
	for (i = 0; i < 3; i++)
	{
		DD_PORT &= ~_BV(DD_PIN);
		_delay_ms(200);
		DD_PORT |= _BV(DD_PIN);
		_delay_ms(200);
	}

	uart_init(UART_19200);

	// start the Roomba's SCI
	uart_putchar(START, ROOMBA_UART);
	_delay_ms(20);

	// // See the appropriate AVR hardware specification, at the end of the USART section, for a table of baud rate
	// // framing error probabilities.  The best we can do with a 16 or 8 MHz crystal is 38400 bps, which has a framing
	// // error rate of 0.2% (1 bit out of every 500).  Well, the best is 76800 bps, but the Roomba doesn't support
	// // that.  38400 at 0.2% is sufficient for our purposes.  An 18.432 MHz crystal will generate all the Roomba's
	// // baud rates with 0.0% error!.  Anyway, the point is we want to use a 38400 bps baud rate to avoid framing
	// // errors.  Also, we have to wait for 100 ms after changing the baud rate.
	// uart_putchar(BAUD, ROOMBA_UART);
	// uart_putchar(ROOMBA_38400BPS, ROOMBA_UART);
	// _delay_ms(100);

	// // change the AVR's UART clock to the new baud rate.
	// uart_init(UART_38400);

	// put the Roomba into safe mode.
	uart_putchar(CONTROL, ROOMBA_UART);
	_delay_ms(20);

	uart_putchar(SAFE, ROOMBA_UART);
	_delay_ms(20);

	// // Set the Roomba's LEDs to the defaults defined above (to verify defaults).
	update_leds();
}

/**
 * Use this function instead of the while loops in Roomba_UpdateSensorPacket if you have a system
 * clock.  This will add a timeout when it's waiting for the bytes to come in, so that the
 * function doesn't enter an infinite loop if a byte is missed.  You'll have to modify this function
 * and insert it into Roomba_UpdateSensorPacket to suit your application.
 */

uint8_t wait_for_bytes(int uart, uint8_t num_bytes, uint8_t timeout)
{
	uint16_t start;
	start = Now();	// current system time
	while (((Now() - start) < timeout) && uart_bytes_received(uart) < num_bytes){
		uint8_t test = uart_bytes_received(uart);
	}
	if (uart_bytes_received(uart) >= num_bytes)
		return 1;
	else
		return 0;
}

void Roomba_UpdateSensorPacket(ROOMBA_SENSOR_GROUP group, roomba_sensor_data_t* sensor_packet)
{
	uart_reset_receive(ROOMBA_UART);
	// No, I don't feel bad about manual loop unrolling.
	uart_putchar(SENSORS, ROOMBA_UART);
	uart_putchar(group, ROOMBA_UART);
	switch(group)
	{
	case EXTERNAL:
		// environment sensors
		if (wait_for_bytes(ROOMBA_UART, 10, TIMEOUT_MS)) {		
			sensor_packet->bumps_wheeldrops = uart_get_byte(0, ROOMBA_UART);
			sensor_packet->wall = uart_get_byte(1, ROOMBA_UART);
			sensor_packet->cliff_left = uart_get_byte(2, ROOMBA_UART);
			sensor_packet->cliff_front_left = uart_get_byte(3, ROOMBA_UART);
			sensor_packet->cliff_front_right = uart_get_byte(4, ROOMBA_UART);
			sensor_packet->cliff_right = uart_get_byte(5, ROOMBA_UART);
			sensor_packet->virtual_wall = uart_get_byte(6, ROOMBA_UART);
			sensor_packet->motor_overcurrents = uart_get_byte(7, ROOMBA_UART);
			sensor_packet->dirt_left = uart_get_byte(8, ROOMBA_UART);
			sensor_packet->dirt_right = uart_get_byte(9, ROOMBA_UART);
		}
		break;
	case CHASSIS:
		// chassis sensors
		if (wait_for_bytes(ROOMBA_UART, 6, TIMEOUT_MS)) {
			sensor_packet->remote_opcode = uart_get_byte(0, ROOMBA_UART);
			sensor_packet->buttons = uart_get_byte(1, ROOMBA_UART);
			sensor_packet->distance.bytes.high_byte = uart_get_byte(2, ROOMBA_UART);
			sensor_packet->distance.bytes.low_byte = uart_get_byte(3, ROOMBA_UART);
			sensor_packet->angle.bytes.high_byte = uart_get_byte(4, ROOMBA_UART);
			sensor_packet->angle.bytes.low_byte = uart_get_byte(5, ROOMBA_UART);
		}
		break;
	case INTERNAL:
		// internal sensors
		if (wait_for_bytes(ROOMBA_UART, 10, TIMEOUT_MS)) {
			sensor_packet->charging_state = uart_get_byte(0, ROOMBA_UART);
			sensor_packet->voltage.bytes.high_byte = uart_get_byte(1, ROOMBA_UART);
			sensor_packet->voltage.bytes.low_byte = uart_get_byte(2, ROOMBA_UART);
			sensor_packet->current.bytes.high_byte = uart_get_byte(3, ROOMBA_UART);
			sensor_packet->current.bytes.low_byte = uart_get_byte(4, ROOMBA_UART);
			sensor_packet->temperature = uart_get_byte(5, ROOMBA_UART);
			sensor_packet->charge.bytes.high_byte = uart_get_byte(6, ROOMBA_UART);
			sensor_packet->charge.bytes.low_byte = uart_get_byte(7, ROOMBA_UART);
			sensor_packet->capacity.bytes.high_byte = uart_get_byte(8, ROOMBA_UART);
			sensor_packet->capacity.bytes.low_byte = uart_get_byte(9, ROOMBA_UART);
		}
		break;
	}
	uart_reset_receive(ROOMBA_UART);
}


void Roomba_ChangeState(ROOMBA_STATE newState)
{
	if (newState == SAFE_MODE)
	{
		if (state == PASSIVE_MODE)
			uart_putchar(CONTROL, ROOMBA_UART);
		else if (state == FULL_MODE)
			uart_putchar(SAFE, ROOMBA_UART);
	}
	else if (newState == FULL_MODE)
	{
		Roomba_ChangeState(SAFE_MODE);
		uart_putchar(FULL, ROOMBA_UART);
	}
	else if (newState == PASSIVE_MODE)
	{
		uart_putchar(POWER, ROOMBA_UART);
	}
	else
	{
		// already in the requested state
		return;
	}

	state = newState;
	_delay_ms(20);
}

void Roomba_Drive( int16_t velocity, int16_t radius )
{
	uart_putchar(DRIVE, ROOMBA_UART);
	uart_putchar(HIGH_BYTE(velocity), ROOMBA_UART);
	uart_putchar(LOW_BYTE(velocity), ROOMBA_UART);
	uart_putchar(HIGH_BYTE(radius), ROOMBA_UART);
	uart_putchar(LOW_BYTE(radius), ROOMBA_UART);
}

void Roomba_DriveDirect( int16_t rwheel, int16_t lwheel )
{
	uart_putchar(DIRECT, ROOMBA_UART);
	uart_putchar(HIGH_BYTE(rwheel), ROOMBA_UART);
	uart_putchar(LOW_BYTE(rwheel), ROOMBA_UART);
	uart_putchar(HIGH_BYTE(lwheel), ROOMBA_UART);
	uart_putchar(LOW_BYTE(lwheel), ROOMBA_UART);
}


/**
 * Update the LEDs on the Roomba to match the configured state
 */
void update_leds()
{
	// The status, spot, clean, max, and dirt detect LED states are combined in a single byte.
	uint8_t leds = status << 4 | spot << 3 | clean << 2 | max << 1 | dd;

	uart_putchar(LEDS, ROOMBA_UART);
	uart_putchar(leds, ROOMBA_UART);
	uart_putchar(power_colour, ROOMBA_UART);
	uart_putchar(power_intensity, ROOMBA_UART);
}

void Roomba_ConfigPowerLED(uint8_t colour, uint8_t intensity)
{
	power_colour = colour;
	power_intensity = intensity;
	update_leds();
}

void Roomba_ConfigStatusLED(STATUS_LED_STATE state)
{
	status = state;
	update_leds();
}

void Roomba_ConfigSpotLED(LED_STATE state)
{
	spot = state;
	update_leds();
}

void Roomba_ConfigCleanLED(LED_STATE state)
{
	clean = state;
	update_leds();
}

void Roomba_ConfigMaxLED(LED_STATE state)
{
	max = state;
	update_leds();
}

void Roomba_ConfigDirtDetectLED(LED_STATE state)
{
	dd = state;
	update_leds();
}

void Roomba_LoadSong(uint8_t songNum, uint8_t* notes, uint8_t* notelengths, uint8_t numNotes)
{
	uint8_t i = 0;

	uart_putchar(SONG, ROOMBA_UART);
	uart_putchar(songNum, ROOMBA_UART);
	uart_putchar(numNotes, ROOMBA_UART);

	for (i=0; i<numNotes; i++)
	{
		uart_putchar(notes[i], ROOMBA_UART);
		uart_putchar(notelengths[i], ROOMBA_UART);
	}
}

void Roomba_PlaySong(int songNum)
{
	uart_putchar(PLAY, ROOMBA_UART);
	uart_putchar(songNum, ROOMBA_UART);
}

uint8_t Roomba_BumperActivated(roomba_sensor_data_t* sensor_data)
{
	// if either of the bumper bits is set, then return true.
	return (sensor_data->bumps_wheeldrops & 0x03) != 0;
}
