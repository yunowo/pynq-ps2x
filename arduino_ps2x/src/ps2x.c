#include "circular_buffer.h"
#include "gpio.h"
#include "timer.h"
#include "xio_switch.h"

#define _BV(bit) (1 << (bit))

#define READ_GAMEPAD       0x1
#define INIT_GAMEPAD       0x2

#define CTRL_CLK        4

gpio gpio_data;
gpio gpio_clock;
gpio gpio_command;
gpio gpio_attention;

static u8 data[21];

/* Private. The actual serial transfer. Handles clock. The PSX controller is full
 * duplex, so this will send a byte as well as receive one.
 */
u8 _ps2x_gamepad_shift(u8 transmit_byte) {
	u8 received_byte = 0;
	for (u8 i = 0; i < 8; i++) {
		//Drop the clock...
		gpio_write(gpio_clock, 0);

		//...set the command (outgoing) pin...
		if (transmit_byte & (_BV(i))) {
			gpio_write(gpio_command, 1);
		} else {
			gpio_write(gpio_command, 0);
		}

		//...wait half the clock cycle...
		delay_us(CTRL_CLK);

		//...raise the clock to HIGH...
		gpio_write(gpio_clock, 1);

		//...at which point you read the data...
		if (gpio_read(gpio_data)) {
			received_byte |= _BV(i);
		}

		//...and wait the other half of the clock cycle
		delay_us(CTRL_CLK);
	}

	//Clock should already be high at this point, but just to be sure...
	gpio_write(gpio_clock, 1);

	return received_byte;
}

/* Private.  Sends a command using the shift method. */
void _ps2x_send_command(u8 send_data[], u8 size) {
	//Before you submit each command packet, you must set attention low; once
	// you are done each packet, return it high.  You have to toggle the line before
	// you submit another command.
	gpio_write(gpio_attention, 0);
	gpio_write(gpio_command, 1);

	//Clock should always be high; it is an active low line...
	gpio_write(gpio_clock, 1);

	for (u8 i = 0; i < size; i++) {
		send_data[i] = _ps2x_gamepad_shift(send_data[i]);
	}

	gpio_write(gpio_attention, 1);
}

/* Updates the current value of the game pad. */
void ps2x_read_gamepad() {
	data[0] = 0x01;
	data[1] = 0x42;
	for (u8 i = 2; i < 21; i++) {
		data[i] = 0x00;
	}
	_ps2x_send_command(data, 21);
}

void ps2x_init() {
	gpio_set_direction(gpio_data, GPIO_IN);
	gpio_set_direction(gpio_clock, GPIO_OUT);
	gpio_set_direction(gpio_command, GPIO_OUT);
	gpio_set_direction(gpio_attention, GPIO_OUT);

	//Initialize game pad
	gpio_write(gpio_clock, 1);
	gpio_write(gpio_command, 1);

	//Init by polling once
	ps2x_read_gamepad();

	//Enter config mode
	u8 enter_config_command[] = { 0x01, 0x43, 0x00, 0x01, 0x00 };
	_ps2x_send_command(enter_config_command, 5);

	//Lock to analog mode on stick
	u8 lock_analog_mode_command[] = { 0x01, 0x44, 0x00, 0x01, 0x03, 0x00, 0x00,
			0x00, 0x00 };
	_ps2x_send_command(lock_analog_mode_command, 9);

	//Exit config mode
	u8 exit_config_command[] = { 0x01, 0x43, 0x00, 0x00, 0x5A, 0x5A, 0x5A, 0x5A,
			0x5A };
	_ps2x_send_command(exit_config_command, 9);
}

int main(void) {
	u32 cmd;

	gpio_data = gpio_open(14); // A0
	gpio_clock = gpio_open(17); // A3
	gpio_command = gpio_open(15); // A1
	gpio_attention = gpio_open(16); // A2

	ps2x_init();

	while (1) {
		while (MAILBOX_CMD_ADDR == 0)
			;
		cmd = MAILBOX_CMD_ADDR;
		switch (cmd) {
		case READ_GAMEPAD:
			ps2x_read_gamepad();
			for (int i = 0; i < 21; i++) {
				MAILBOX_DATA(i) = data[i];
			}
			MAILBOX_CMD_ADDR = 0x0;
			break;
		case INIT_GAMEPAD:
			ps2x_init();
			MAILBOX_CMD_ADDR = 0x0;
			break;
		default:
			MAILBOX_CMD_ADDR = 0x0;
			break;
		}
	}
	return 0;
}
