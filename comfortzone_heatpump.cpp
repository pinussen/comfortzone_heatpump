#include "comfortzone_heatpump.h"
#include "comfortzone_config.h"

#include "comfortzone_frame.h"
#include "comfortzone_decoder.h"

#include "comfortzone_crafting.h"

void comfortzone_heatpump::begin(HardwareSerial &rs485_serial, int de_pin)
{
	rs485 = rs485_serial;
	rs485_de_pin = de_pin;

	rs485.begin(19200, SERIAL_8N1);

	pinMode(rs485_de_pin, OUTPUT);
	digitalWrite(rs485_de_pin, LOW);		// enable RS485 receive mode
}

comfortzone_heatpump::PROCESSED_FRAME_TYPE comfortzone_heatpump::process()
{
	PROCESSED_FRAME_TYPE pft;

	// there is no frame header. First we must collect 21 bytes (sizeof (CZ_PACKET_HEADER)).
	// To find start of frame, it is possible to check if the duration between 2 bytes is not too long
	// (= > 8 bits sent @19.2Kbit/s) which is not possible here as the library does not receive data itself
	// and moreover receiver may (should) have a buffer hidding data stream pause.
	// The 2nd solution is to check if 
	//  - unknown variable (byte[5 & 6]) is either {0xD3, 0x5E} (command) or {0x07, 0x8A} (reply)
	//  - and if cmd byte is 'R' or 'W' (in command case) or 'r' or 'w' (in reply case)
	//
	// if not, first byte is discarded

	while(rs485.available())
	{
		cz_buf[cz_size++] = rs485.read();

		if(cz_size == sizeof(cz_buf))
		{	// something goes wrong. packet size is store in a single byte, how can it goes above 255 ???
			// disable_cz_buf_clear_on_completion = true ?
			cz_size = 0;
			continue;
		}

		if(cz_size < sizeof(CZ_PACKET_HEADER))
			continue;

		if(cz_size == sizeof(CZ_PACKET_HEADER))
		{
			CZ_PACKET_HEADER *czph = (CZ_PACKET_HEADER *)cz_buf;
			byte comp1_dest[4];

			comp1_dest[0] = czph->destination[0] ^ 0xFF;
			comp1_dest[1] = czph->destination[1] ^ 0xFF;
			comp1_dest[2] = czph->destination[2] ^ 0xFF;
			comp1_dest[3] = czph->destination[3] ^ 0xFF;

			if(
				(czph->destination_crc == CRC8.maxim(czph->destination, 4))
				&& (czph->comp1_destination_crc == CRC8.maxim(comp1_dest, 4))
				&& (
						(czph->cmd == 'W') || (czph->cmd == 'R') || (czph->cmd == 'w') || (czph->cmd == 'r')
					)
				)
			{
				cz_full_frame_size = czph->packet_size;
			}
			else
			{
				memcpy(cz_buf, cz_buf + 1, sizeof(CZ_PACKET_HEADER) - 1);
				cz_size--;
				continue;
			}
		}

		if(cz_size == cz_full_frame_size)
			break;
	}

	// not enough data received to be a header
	if(cz_size < sizeof(CZ_PACKET_HEADER))
		return PFT_NONE;

	// not enough data received to be a packet (header+data)
	if(cz_size != cz_full_frame_size)
		return PFT_NONE;

	if(cz_size == sizeof(cz_buf))
	{	// something goes wrong. packet size is store in a single byte, how can it goes above 255 ???
		// disable_cz_buf_clear_on_completion = true ?
		cz_size = 0;
		return PFT_NONE;
	}

	// check frame CRC (last byte of buffer is CRC
	if(CRC8.maxim(cz_buf, cz_size - 1) == cz_buf[cz_size - 1])
	{
		pft = czdec::process_frame(this, (CZ_PACKET_HEADER *)cz_buf);
	}
	else
	{
		pft = PFT_CORRUPTED;
	}

	if(grab_buffer)
	{
		if(cz_size > grab_buffer_size)
		{
			// frame is too big for grab buffer => notify empty frame
			*grab_buffer_frame_size = 0;
		}
		else
		{
			memcpy(grab_buffer, cz_buf, cz_size);
			*grab_buffer_frame_size = cz_size;
		}
	}

	if(!disable_cz_buf_clear_on_completion)
		cz_size = 0;

	last_frame_timestamp = millis();

	if(pft == comfortzone_heatpump::PFT_REPLY)
		last_reply_frame_timestamp = last_frame_timestamp;

	return pft;
}

// for debug purpose, it can be useful to get full frame
// input: pointer on buffer where last full frame will be copied
//        max size of buffer
//        size of the last full frame received
// If buffer is set to NULL, frame grabber is disabled
// If buffer is not NULL, each time comfortzone_receive() reply is not PFT_NONE, the received frame will
// be copied into buffer and *frame_size will be updated
// recommended buffer_size is 256 bytes
void comfortzone_heatpump::set_grab_buffer(byte *buffer, uint16_t buffer_size, uint16_t *frame_size)
{
	if(buffer == NULL)
	{
		grab_buffer = NULL;
		grab_buffer_size = 0;
		grab_buffer_frame_size = NULL;
	}
	else
	{
		grab_buffer = buffer;
		grab_buffer_size = buffer_size;
		grab_buffer_frame_size = frame_size;
	}
}

#if 0
// craft one command frame
// input: pointer to output buffer, min size is sizeof(W_CMD) = 30 bytes
//        name of the command to craft
//        parameter of the command (depend on crafted command, see KNOWN_REGISTER_CRAFT_NAME enum)
// output: 0 = uncraftable packet or crafting error else number of bytes used in buffer
uint16_t comfortzone_craft(byte *output_buffer, KNOWN_REGISTER_CRAFT_NAME reg_cname, uint16_t parameter)
{
	int kr_idx;

	kr_idx = kr_craft_name_to_index(reg_cname);

	if(kr_idx == -1)
		return 0;
	
	switch(reg_cname)
	{
		case KR_UNCRAFTABLE:				// uncraftable packet
									break;

		case KR_FAN_SPEED:				// set fan speed, parameter => 1=slow, 2=normal, 3=fast
									switch(parameter)
									{
										case 1:
													return cz_craft_w_small_cmd(output_buffer, kr_decoder[kr_idx].reg_num, 0x01, 0x95);
										case 2:
													return cz_craft_w_small_cmd(output_buffer, kr_decoder[kr_idx].reg_num, 0x02, 0x77);
										case 3:
													return cz_craft_w_small_cmd(output_buffer, kr_decoder[kr_idx].reg_num, 0x03, 0x29);
									}
									break;

		case KR_LED_LUMINOSITY:			// set led luminosity, parameter => 0=off to 6=full power
									{
										//                                      vv    vv    vv    vv  invalid value
										static byte led_crc[7] = {0xED, 0xB3, 0x00, 0x00, 0x00, 0x00, 0x30 };

										if(parameter > 6)
											parameter = 6;

										return cz_craft_w_small_cmd(output_buffer, kr_decoder[kr_idx].reg_num, parameter, led_crc[parameter]);
									}
									break;

		case KR_ROOM_HEATING_TEMP:		// set room heating temperature, parameter => 120 (=12.0°) to 240 (=24.0°)
									{
										static byte hwt_crc[800 - 120 +1] =
												{
													// 120 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 140 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 160 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 180 - 
													0x??, 0x??, 0x??, 0xDD, 0xC5, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 200 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 220 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 240 
													0x??
												};

										if(parameter < 120)
											parameter = 120;
										else if(parameter > 240)
											parameter = 240;

										return cz_craft_w_cmd(output_buffer, kr_decoder[kr_idx].reg_num, parameter, rwt_crc[parameter - 120]);
									}
									break;

		case KR_HOT_WATER_TEMP:			// set hot water temperature, parameter => 120 (=12.0°) to 800 (=80.0°)
									{
										static byte hwt_crc[800 - 120 +1] =
												{
													// 120 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 140 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 160 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 180 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 200 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 220 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 240 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 260 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 280 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 300 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 320 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 340 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 360 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 380 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 400 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 420 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 440 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 460 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x9B, 0x??, 0x??, 0x??, 
													// 480 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 500 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 520 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 540 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 560 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 580 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 600 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 620 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 640 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 660 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 680 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 700 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 720 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 740 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 750 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 760 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 780 - 
													0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 
													// 800
													0x??
												};

										if(parameter < 120)
											parameter = 120;
										else if(parameter > 800)
											parameter = 800;

										return cz_craft_w_cmd(output_buffer, kr_decoder[kr_idx].reg_num, parameter, hwt_crc[parameter - 120]);
									}
									break;

		case KR_EXTRA_HOT_WATER_ON:	// enable extra hot water, no parameter
									return cz_craft_w_cmd(output_buffer, kr_decoder[kr_idx].reg_num, 0x0001, 0x06);

		case KR_EXTRA_HOT_WATER_OFF:	// disable extra hot water, no parameter
									return cz_craft_w_cmd(output_buffer, kr_decoder[kr_idx].reg_num, 0xFFFE, 0x90);

	}

	return 0;
}
#endif

// Functions to modify heatpump settings
// timeout (in second) is the maximum duration before giving up (RS485 bus always busy)
// output: true = ok, false = failed to process

// from logical analyzer, after a command, there is roughly 50ms-60ms before the reply
// except for status 19 where reply comes ~200ms later
// after status 08 reply, there is a pause of ~1.1s.
// between a replay and the next command, there is a variable gap between 2 and 10 ms

// algorithm: 
// 1) craft command packet and expected reply
// 2) we assume there is only 3 devices on the bus, the heatpump, its control panel and us.
//    This means after a reply, we can immediatly send a command without collision
//    and we expect the reply within 200ms
// Another solution is to wait for status 08 reply and use the large pause. However, the pause
// appears once only every 5 seconds

// fan speed: 1 = low, 2 = normal, 3 = fast
bool comfortzone_heatpump::set_fan_speed(uint8_t fan_speed, int timeout)
{
}

// room temperature temperature in °C
bool comfortzone_heatpump::set_room_temperature(float room_temp, int timeout)
{
}

// hot water temperature in °C
bool comfortzone_heatpump::set_hot_water_temperature(float room_temp, int timeout)
{
}

// led level: 0 = off -> 6 = highest level
const char *comfortzone_heatpump::set_led_luminosity(uint8_t led_level, int timeout)
{
	byte cmd[256];
	byte expected_reply[256];
	int cmd_length;
	int expected_reply_length;
	czdec::KNOWN_REGISTER *kr;

	if(led_level > 6)
	{
		DPRINTLN("comfortzone_heatpump::set_led_luminosity - Invalid led level, must be between 0 and 6");
		return "comfortzone_heatpump::set_led_luminosity - Invalid led level, must be between 0 and 6";
		//return false;
	}

	kr = czdec::kr_craft_name_to_index(czcraft::KR_LED_LUMINOSITY);

	if(kr == NULL)
	{
		DPRINTLN("comfortzone_heatpump::set_led_luminosity - czcraft::KR_LED_LUMINOSITY not found");
		return "comfortzone_heatpump::set_led_luminosity - czcraft::KR_LED_LUMINOSITY not found";
		//return false;
	}

	cmd_length = czcraft::craft_w_small_cmd(this, cmd, kr->reg_num, led_level);
	expected_reply_length = czcraft::craft_w_reply(this, expected_reply, kr->reg_num, led_level);

	return push_settings(cmd, cmd_length, expected_reply, expected_reply_length, timeout);
}

static char debug_buf[512];
static int debug_buf_len = 0;

// send a command to the heatpump and wait for the given reply
// on error, several retries may occur and the command may take up to "timeout" seconds
const char *comfortzone_heatpump::push_settings(byte *cmd, int cmd_length, byte *expected_reply, int expected_reply_length, int timeout)
{
	unsigned long now;
	unsigned long timeout_time;
	unsigned long min_time_after_reply;
	unsigned long reply_frame_time;
	unsigned long reply_timeout;
	PROCESSED_FRAME_TYPE pft;

	now = millis();
	timeout_time = now + timeout * 1000;

	debug_buf[0] = '\0';

#if 1
	int i;

	for(i=0; i < expected_reply_length; i ++)
	{
		if(debug_buf_len >= (sizeof(debug_buf) - 4))
			break;

		sprintf(debug_buf + debug_buf_len, "%02X ", (int)(expected_reply[i]));

		debug_buf_len = strlen(debug_buf);
	}
	//return debug_buf;
#endif

	while(now < timeout_time)
	{
		if(last_frame_timestamp == last_reply_frame_timestamp)
		{
			if(debug_buf_len < (sizeof(debug_buf) - 1))
				debug_buf[debug_buf_len++] = 'a';

			// last received frame was a reply frame
			min_time_after_reply = now + 40;		// during test, there is always less than 10ms after a reply and controller next command
			reply_frame_time = last_reply_frame_timestamp;

			// wait a little time to see if the controller has not strated to send a new command
			while(now < min_time_after_reply)
			{
				pft = process();

				if(pft != comfortzone_heatpump::PFT_NONE)
					break;

				now = millis();
			}

			// no new frame or reply frame and incoming frame buffer is empty, we have a go
			if(
				(last_frame_timestamp == last_reply_frame_timestamp)
				&& (reply_frame_time == last_reply_frame_timestamp)
				&& (cz_size == 0))
			{
			if(debug_buf_len < (sizeof(debug_buf) - 1))
				debug_buf[debug_buf_len++] = 'b';

				digitalWrite(rs485_de_pin, HIGH);	// enable send mode
				disable_cz_buf_clear_on_completion = true;
				rs485.write(cmd, cmd_length);
				rs485.flush();
				digitalWrite(rs485_de_pin, LOW);		// enable receive mode

				// now, wait for a frame at most 100ms
				now = millis();
				reply_timeout = now + 200;

				while(now < reply_timeout)
				{
					pft = process();

					if(pft != comfortzone_heatpump::PFT_NONE)
						break;

					now = millis();
				}
				
				// if we have a reply frame with the correct size and content, command was successfully processed
				if( (pft == comfortzone_heatpump::PFT_REPLY)
					 && (cz_size == expected_reply_length)
					 && (!memcmp(cz_buf, expected_reply, expected_reply_length)) )
				{
			if(debug_buf_len < (sizeof(debug_buf) - 1))
				debug_buf[debug_buf_len++] = 'c';

					// clear input buffer and restart normal frame processing
					disable_cz_buf_clear_on_completion = false;
					cz_size = 0;

				debug_buf[debug_buf_len] = '\0';
					return debug_buf;
					//return true;
				}

				// no correct reply received, retry

			if(debug_buf_len < (sizeof(debug_buf) - 1))
				debug_buf[debug_buf_len++] = 'd';
			if(debug_buf_len < (sizeof(debug_buf) - 32))
			{
				sprintf(debug_buf + debug_buf_len, "%d-(%02X) %02X %02X %02X %c-%d=%d ", (int)pft, 
												((CZ_PACKET_HEADER*)cz_buf)->comp1_destination_crc,
												((CZ_PACKET_HEADER*)cz_buf)->reg_num[6],
												((CZ_PACKET_HEADER*)cz_buf)->reg_num[7],
												((CZ_PACKET_HEADER*)cz_buf)->reg_num[8],
												((CZ_PACKET_HEADER*)cz_buf)->cmd, cz_size, expected_reply_length);
				debug_buf_len = strlen(debug_buf);
			}

				if(pft != comfortzone_heatpump::PFT_NONE)
				{
					if(debug_buf_len < (sizeof(debug_buf) - 1))
						debug_buf[debug_buf_len++] = '\n';

					for(i=0; i < cz_size; i ++)
					{
						if(debug_buf_len >= (sizeof(debug_buf) - 4))
							break;
				
						sprintf(debug_buf + debug_buf_len, "%02X ", (int)(cz_buf[i]));
				
						debug_buf_len = strlen(debug_buf);
					}
				}
				// clear input buffer and restart normal frame processing
				disable_cz_buf_clear_on_completion = false;
				cz_size = 0;

				if(pft != comfortzone_heatpump::PFT_NONE)
					return debug_buf;
			}
		}

		process();
		now = millis();
	}

	debug_buf[debug_buf_len] = '\0';
		return debug_buf;
	//return false;
}

