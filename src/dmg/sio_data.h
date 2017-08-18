// GB Enhanced+ Copyright Daniel Baxter 2016
// Licensed under the GPLv2
// See LICENSE.txt for full license text

// File : sio_data.h
// Date : April 25, 2015
// Description : Serial I/O data
//
// Defines the SIO data structures and enums that the MMU will update whenever values are written in memory
// Only the SIO should read values from this namespace. Only the MMU should write values to this namespace.

#ifndef GB_SIO_DATA
#define GB_SIO_DATA

#include "common.h"

//Serial Input-Output device-type enumeration
enum sio_types
{ 
	NO_GB_DEVICE,
	GB_LINK,
	GB_PRINTER,
	GB_MOBILE_ADAPTER,
	GB_BARDIGUN_SCANNER,
	GB_BARCODE_BOY,
};

//Infrared device-type enumeration
enum ir_types
{
	GBC_IR_PORT,
	GBC_FULL_CHANGER,
	GBC_POCKET_PIKACHU_2,
	GBC_POCKET_SAKURA,
};

enum printer_state
{
	GBP_AWAITING_PACKET,
	GBP_RECEIVE_COMMAND,
	GBP_RECEIVE_COMPRESSION_FLAG,
	GBP_RECEIVE_LENGTH,
	GBP_RECEIVE_DATA,
	GBP_RECEIVE_CHECKSUM,
	GBP_ACKNOWLEDGE_PACKET,
};

enum mobile_state
{
	GBMA_AWAITING_PACKET,
	GBMA_RECEIVE_HEADER,
	GBMA_RECEIVE_DATA,
	GBMA_RECEIVE_CHECKSUM,
	GBMA_ACKNOWLEDGE_PACKET,
	GBMA_ECHO_PACKET,
};

enum bardigun_state
{
	BARDIGUN_INACTIVE,
	BARDIGUN_SEND_BARCODE,
};

enum barcode_boy_state
{
	BARCODE_BOY_INACTIVE,
	BARCODE_BOY_ACTIVE,
	BARCODE_BOY_SEND_BARCODE,
};

enum full_changer_state
{
	FULL_CHANGER_INACTIVE,
	FULL_CHANGER_SEND_SIGNAL,
};

struct dmg_sio_data
{
	bool connected;
	bool active_transfer;
	bool double_speed;
	bool internal_clock;
	bool sync;
	u8 shifts_left;
	u8 transfer_byte;
	u32 shift_counter;
	u32 shift_clock;
	u32 sync_counter;
	u32 sync_clock;
	sio_types sio_type;
	ir_types ir_type;
};

#endif // GB_SIO_DATA 
