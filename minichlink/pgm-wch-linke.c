// Tricky: You need to use wch link to use WCH-LinkRV.
//  you can always uninstall it in device manager.  It will be under USB devices or something like that at the bottom.

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "libusb.h"
#include "minichlink.h"

struct LinkEProgrammerStruct
{
	void * internal;
	libusb_device_handle * devh;
	int lasthaltmode;
};

#define WCHTIMEOUT 5000
#define WCHCHECK(x) if( (status = x) ) { fprintf( stderr, "Bad USB Operation on " __FILE__ ":%d (%d)\n", __LINE__, status ); exit( status ); }

const uint8_t * bootloader = (const uint8_t*)
"\x21\x11\x22\xca\x26\xc8\x93\x77\x15\x00\x99\xcf\xb7\x06\x67\x45" \
"\xb7\x27\x02\x40\x93\x86\x36\x12\x37\x97\xef\xcd\xd4\xc3\x13\x07" \
"\xb7\x9a\xd8\xc3\xd4\xd3\xd8\xd3\x93\x77\x25\x00\x9d\xc7\xb7\x27" \
"\x02\x40\x98\x4b\xad\x66\x37\x33\x00\x40\x13\x67\x47\x00\x98\xcb" \
"\x98\x4b\x93\x86\xa6\xaa\x13\x67\x07\x04\x98\xcb\xd8\x47\x05\x8b" \
"\x63\x16\x07\x10\x98\x4b\x6d\x9b\x98\xcb\x93\x77\x45\x00\xa9\xcb" \
"\x93\x07\xf6\x03\x99\x83\x2e\xc0\x2d\x63\x81\x76\x3e\xc4\xb7\x32" \
"\x00\x40\xb7\x27\x02\x40\x13\x03\xa3\xaa\xfd\x16\x98\x4b\xb7\x03" \
"\x02\x00\x33\x67\x77\x00\x98\xcb\x02\x47\xd8\xcb\x98\x4b\x13\x67" \
"\x07\x04\x98\xcb\xd8\x47\x05\x8b\x69\xe7\x98\x4b\x75\x8f\x98\xcb" \
"\x02\x47\x13\x07\x07\x04\x3a\xc0\x22\x47\x7d\x17\x3a\xc4\x79\xf7" \
"\x93\x77\x85\x00\xf1\xcf\x93\x07\xf6\x03\x2e\xc0\x99\x83\x37\x27" \
"\x02\x40\x3e\xc4\x1c\x4b\xc1\x66\x2d\x63\xd5\x8f\x1c\xcb\x37\x07" \
"\x00\x20\x13\x07\x07\x20\xb7\x27\x02\x40\xb7\x03\x08\x00\xb7\x32" \
"\x00\x40\x13\x03\xa3\xaa\x94\x4b\xb3\xe6\x76\x00\x94\xcb\xd4\x47" \
"\x85\x8a\xf5\xfe\x82\x46\xba\x84\x37\x04\x04\x00\x36\xc2\xc1\x46" \
"\x36\xc6\x92\x46\x84\x40\x11\x07\x84\xc2\x94\x4b\xc1\x8e\x94\xcb" \
"\xd4\x47\x85\x8a\xb1\xea\x92\x46\xba\x84\x91\x06\x36\xc2\xb2\x46" \
"\xfd\x16\x36\xc6\xf9\xfe\x82\x46\xd4\xcb\x94\x4b\x93\xe6\x06\x04" \
"\x94\xcb\xd4\x47\x85\x8a\x85\xee\xd4\x47\xc1\x8a\x85\xce\xd8\x47" \
"\xb7\x06\xf3\xff\xfd\x16\x13\x67\x07\x01\xd8\xc7\x98\x4b\x21\x45" \
"\x75\x8f\x98\xcb\x52\x44\xc2\x44\x61\x01\x02\x90\x23\x20\xd3\x00" \
"\xf5\xb5\x23\xa0\x62\x00\x3d\xb7\x23\xa0\x62\x00\x55\xb7\x23\xa0" \
"\x62\x00\xc1\xb7\x82\x46\x93\x86\x06\x04\x36\xc0\xa2\x46\xfd\x16" \
"\x36\xc4\xb5\xf2\x98\x4b\xb7\x06\xf3\xff\xfd\x16\x75\x8f\x98\xcb" \
"\x41\x89\x05\xcd\x2e\xc0\x0d\x06\x02\xc4\x09\x82\xb7\x07\x00\x20" \
"\x32\xc6\x93\x87\x07\x20\x98\x43\x13\x86\x47\x00\xa2\x47\x82\x46" \
"\x8a\x07\xb6\x97\x9c\x43\x63\x1c\xf7\x00\xa2\x47\x85\x07\x3e\xc4" \
"\xa2\x46\x32\x47\xb2\x87\xe3\xe0\xe6\xfe\x01\x45\x61\xb7\x41\x45" \
"\x51\xb7\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" \
"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" \
"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";

int bootloader_len = 512;

void wch_link_command( libusb_device_handle * devh, const void * command_v, int commandlen, int * transferred, uint8_t * reply, int replymax )
{
	uint8_t * command = (uint8_t*)command_v;
	uint8_t buffer[1024];
	int got_to_recv = 0;
	int status;
	int transferred_local;
	if( !transferred ) transferred = &transferred_local;
	status = libusb_bulk_transfer( devh, 0x01, command, commandlen, transferred, WCHTIMEOUT );
	if( status ) goto sendfail;

	got_to_recv = 1;
	if( !reply )
	{
		reply = buffer; replymax = sizeof( buffer );
	}
	
	status = libusb_bulk_transfer( devh, 0x81, reply, replymax, transferred, WCHTIMEOUT );
	if( status ) goto sendfail;
	return;
sendfail:
	fprintf( stderr, "Error sending WCH command (%s): ", got_to_recv?"on recv":"on send" );
	int i;
	for( i = 0; i < commandlen; i++ )
	{
		printf( "%02x ", command[i] );
	}
	printf( "\n" );
	exit( status );
}

static void wch_link_multicommands( libusb_device_handle * devh, int nrcommands, ... )
{
	int i;
	va_list argp;
	va_start(argp, nrcommands);
	for( i = 0; i < nrcommands; i++ )
	{
		int clen = va_arg(argp, int);
		wch_link_command( devh, va_arg(argp, char *), clen, 0, 0, 0 );
	}
	va_end( argp );
}

static inline libusb_device_handle * wch_link_base_setup( int inhibit_startup )
{
	libusb_context * ctx = 0;
	int status;
	status = libusb_init(&ctx);
	if (status < 0) {
		fprintf( stderr, "Error: libusb_init_context() returned %d\n", status );
		exit( status );
	}
	
	libusb_device **list;
	libusb_device *found = NULL;
	ssize_t cnt = libusb_get_device_list(ctx, &list);
	ssize_t i = 0;
	for (i = 0; i < cnt; i++) {
		libusb_device *device = list[i];
		struct libusb_device_descriptor desc;
		int r = libusb_get_device_descriptor(device,&desc);
		if( r == 0 && desc.idVendor == 0x1a86 && desc.idProduct == 0x8010 ) { found = device; }
	}

	if( !found )
	{
		return 0;
	}

	libusb_device_handle * devh;
	status = libusb_open( found, &devh );
	if( status )
	{
		fprintf( stderr, "Error: couldn't open wch link device (libusb_open() = %d)\n", status );
		return 0;
	}
		
	WCHCHECK( libusb_claim_interface(devh, 0) );
	
	uint8_t rbuff[1024];
	int transferred;
	libusb_bulk_transfer( devh, 0x81, rbuff, 1024, &transferred, 1 ); // Clear out any pending transfers.  Don't wait though.

	return devh;
}

static int LESetupInterface( void * d )
{
	libusb_device_handle * dev = ((struct LinkEProgrammerStruct*)d)->devh;
	uint8_t rbuff[1024];
	uint32_t transferred = 0;

	// Place part into reset.
	wch_link_command( dev, "\x81\x0d\x01\x01", 4, (int*)&transferred, rbuff, 1024 );	// Reply is: "\x82\x0d\x04\x02\x08\x02\x00"

	// TODO: What in the world is this?  It doesn't appear to be needed.
	wch_link_command( dev, "\x81\x0c\x02\x09\x01", 5, 0, 0, 0 ); //Reply is: 820c0101

	// This puts the processor on hold to allow the debugger to run.
	wch_link_command( dev, "\x81\x0d\x01\x02", 4, 0, 0, 0 ); // Reply: Ignored, 820d050900300500

	wch_link_command( dev, "\x81\x11\x01\x09", 4, (int*)&transferred, rbuff, 1024 ); // Reply: Chip ID + Other data (see below)
	if( transferred != 20 )
	{
		fprintf( stderr, "Error: could not get part status\n" );
		return -1;
	}
	fprintf( stderr, "Part Type (A): 0x%02x%02x (This is the capacity code, in KB)\n", rbuff[2], rbuff[3] );  // Is this Flash size?
	fprintf( stderr, "Part UUID    : %02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x\n", rbuff[4], rbuff[5], rbuff[6], rbuff[7], rbuff[8], rbuff[9], rbuff[10], rbuff[11] );
	fprintf( stderr, "PFlags       : %02x-%02x-%02x-%02x\n", rbuff[12], rbuff[13], rbuff[14], rbuff[15] );
	fprintf( stderr, "Part Type (B): %02x-%02x-%02x-%02x\n", rbuff[16], rbuff[17], rbuff[18], rbuff[19] );
	return 0;
}

static int LEControl3v3( void * d, int bOn )
{
	libusb_device_handle * dev = ((struct LinkEProgrammerStruct*)d)->devh;
printf( "3v3: %d\n", bOn );
	if( bOn )
		wch_link_command( (libusb_device_handle *)dev, "\x81\x0d\x01\x09", 4, 0, 0, 0 );
	else
		wch_link_command( (libusb_device_handle *)dev, "\x81\x0d\x01\x09", 4, 0, 0, 0 );
	return 0;
}

static int LEControl5v( void * d, int bOn )
{
	libusb_device_handle * dev = ((struct LinkEProgrammerStruct*)d)->devh;
printf( "  5: %d\n", bOn );

	if( bOn )
		wch_link_command( (libusb_device_handle *)dev, "\x81\x0d\x01\x0b", 4, 0, 0, 0 );
	else
		wch_link_command( (libusb_device_handle *)dev, "\x81\x0d\x01\x0c", 4, 0, 0, 0 );
	return 0;
}

static int LEUnbrick( void * d )
{
	printf( "Sending unbrick\n" );
	libusb_device_handle * dev = ((struct LinkEProgrammerStruct*)d)->devh;
	wch_link_command( (libusb_device_handle *)dev, "\x81\x0d\x01\x0f\x09", 5, 0, 0, 0 );
	printf( "Done unbrick\n" );
	return 0;
}

static int LEHaltMode( void * d, int mode )
{
	libusb_device_handle * dev = ((struct LinkEProgrammerStruct*)d)->devh;
	if( mode == ((struct LinkEProgrammerStruct*)d)->lasthaltmode )
		return 0;
	((struct LinkEProgrammerStruct*)d)->lasthaltmode = mode;
	
	if( mode == 0 )
	{
		printf( "Holding in reset\n" );
		// Part one "immediately" places the part into reset.  Part 2 says when we're done, leave part in reset.
		wch_link_multicommands( (libusb_device_handle *)dev, 2, 4, "\x81\x0d\x01\x02", 4, "\x81\x0d\x01\x01" );
	}
	else if( mode == 1 )
	{
		// This is clearly not the "best" method to exit reset.  I don't know why this combination works.
		wch_link_multicommands( (libusb_device_handle *)dev, 3, 4, "\x81\x0b\x01\x01", 4, "\x81\x0d\x01\x02", 4, "\x81\x0d\x01\xff" );
	}
	else
	{
		return -93;
	}
	return 0;
}

static int LEConfigureNRSTAsGPIO( void * d, int one_if_yes_gpio )
{
	libusb_device_handle * dev = ((struct LinkEProgrammerStruct*)d)->devh;

	if( one_if_yes_gpio )
	{
		wch_link_multicommands( (libusb_device_handle *)dev, 2, 11, "\x81\x06\x08\x02\xff\xff\xff\xff\xff\xff\xff", 4, "\x81\x0b\x01\x01" );
	}
	else
	{
		wch_link_multicommands( (libusb_device_handle *)dev, 2, 11, "\x81\x06\x08\x02\xf7\xff\xff\xff\xff\xff\xff", 4, "\x81\x0b\x01\x01" );
	}
	return 0;
}


static int LEReadBinaryBlob( void * d, uint32_t offset, uint32_t amount, uint8_t * readbuff )
{
	libusb_device_handle * dev = ((struct LinkEProgrammerStruct*)d)->devh;

	LEHaltMode( d, 0 );

	int i;
	int status;
	uint8_t rbuff[1024];
	int transferred = 0;
	int readbuffplace = 0;

	wch_link_command( (libusb_device_handle *)dev, "\x81\x06\x01\x01", 4, 0, 0, 0 );

	// Flush out any pending data.
	libusb_bulk_transfer( (libusb_device_handle *)dev, 0x82, rbuff, 1024, &transferred, 1 );

	// 3/8 = Read Memory
	// First 4 bytes are big-endian location.
	// Next 4 bytes are big-endian amount.
	uint8_t readop[11] = { 0x81, 0x03, 0x08, };
	
	readop[3] = (offset>>24)&0xff;
	readop[4] = (offset>>16)&0xff;
	readop[5] = (offset>>8)&0xff;
	readop[6] = (offset>>0)&0xff;

	readop[7] = (amount>>24)&0xff;
	readop[8] = (amount>>16)&0xff;
	readop[9] = (amount>>8)&0xff;
	readop[10] = (amount>>0)&0xff;
	
	wch_link_command( (libusb_device_handle *)dev, readop, 11, 0, 0, 0 );

	// Perform operation
	wch_link_command( (libusb_device_handle *)dev, "\x81\x02\x01\x0c", 4, 0, 0, 0 );

	uint32_t remain = amount;
	while( remain )
	{
		transferred = 0;
		WCHCHECK( libusb_bulk_transfer( (libusb_device_handle *)dev, 0x82, rbuff, 1024, &transferred, WCHTIMEOUT ) );
		memcpy( ((uint8_t*)readbuff) + readbuffplace, rbuff, transferred );
		readbuffplace += transferred;
		remain -= transferred;
	}

	// Flip internal endian.  Must be done separately in case something was unaligned when
	// reading.
	for( i = 0; i < readbuffplace/4; i++ )
	{
		uint32_t r = ((uint32_t*)readbuff)[i];
		((uint32_t*)readbuff)[i] = (r>>24) | ((r & 0xff0000) >> 8) | ((r & 0xff00)<<8) | (( r & 0xff )<<24); 
	}

	return 0;
}

static int LEWriteBinaryBlob( void * d, uint32_t address_to_write, uint32_t len, uint8_t * blob )
{
	libusb_device_handle * dev = ((struct LinkEProgrammerStruct*)d)->devh;

	LEHaltMode( d, 0 );

	int i;
	int status;
	uint8_t rbuff[1024];
	int transferred;

	int padlen = ((len-1) & (~0x3f)) + 0x40;

	wch_link_command( (libusb_device_handle *)dev, "\x81\x06\x01\x01", 4, 0, 0, 0 );
	wch_link_command( (libusb_device_handle *)dev, "\x81\x06\x01\x01", 4, 0, 0, 0 ); // Not sure why but it seems to work better when we request twice.

	// This contains the write data quantity, in bytes.  (The last 2 octets)
	// Then it just rollllls on in.
	char rksbuff[11] = { 0x81, 0x01, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	rksbuff[9] = len >> 8;
	rksbuff[10] = len & 0xff;
	wch_link_command( (libusb_device_handle *)dev, rksbuff, 11, 0, 0, 0 );
	
	wch_link_command( (libusb_device_handle *)dev, "\x81\x02\x01\x05", 4, 0, 0, 0 );
	
	int pplace = 0;
	for( pplace = 0; pplace < bootloader_len; pplace += 64 )
	{
		WCHCHECK( libusb_bulk_transfer( (libusb_device_handle *)dev, 0x02, (uint8_t*)(bootloader+pplace), 64, &transferred, WCHTIMEOUT ) );
	}
	
	for( i = 0; i < 10; i++ )
	{
		wch_link_command( (libusb_device_handle *)dev, "\x81\x02\x01\x07", 4, &transferred, rbuff, 1024 );
		if( transferred == 4 && rbuff[0] == 0x82 && rbuff[1] == 0x02 && rbuff[2] == 0x01 && rbuff[3] == 0x07 )
		{
			break;
		}
	} 
	if( i == 10 )
	{
		fprintf( stderr, "Error, confusing respones to 02/01/07\n" );
		exit( -109 );
	}
	
	wch_link_command( (libusb_device_handle *)dev, "\x81\x02\x01\x02", 4, 0, 0, 0 );

	for( pplace = 0; pplace < padlen; pplace += 64 )
	{
		if( pplace + 64 > len )
		{
			uint8_t paddeddata[64];
			int gap = pplace + 64 - len;
			int okcopy = len - pplace;
			memcpy( paddeddata, blob + pplace, okcopy );
			memset( paddeddata + okcopy, 0xff, gap );
			WCHCHECK( libusb_bulk_transfer( (libusb_device_handle *)dev, 0x02, paddeddata, 64, &transferred, WCHTIMEOUT ) );
		}
		else
		{
			WCHCHECK( libusb_bulk_transfer( (libusb_device_handle *)dev, 0x02, blob+pplace, 64, &transferred, WCHTIMEOUT ) );
		}
	}
	return 0;
}

int LEExit( void * d )
{
	libusb_device_handle * dev = ((struct LinkEProgrammerStruct*)d)->devh;

	wch_link_command( (libusb_device_handle *)dev, "\x81\x0d\x01\xff", 4, 0, 0, 0);
	return 0;
}

void * TryInit_WCHLinkE()
{
	libusb_device_handle * wch_linke_devh;
	wch_linke_devh = wch_link_base_setup(0);
	if( !wch_linke_devh ) return 0;


	struct LinkEProgrammerStruct * ret = malloc( sizeof( struct LinkEProgrammerStruct ) );
	memset( ret, 0, sizeof( *ret ) );
	ret->devh = wch_linke_devh;
	ret->lasthaltmode = 0;

	MCF.WriteReg32 = 0;
	MCF.ReadReg32 = 0;

	MCF.SetupInterface = LESetupInterface;
	MCF.Control3v3 = LEControl3v3;
	MCF.Control5v = LEControl5v;
	MCF.Unbrick = LEUnbrick;
	MCF.HaltMode = LEHaltMode;
	MCF.ConfigureNRSTAsGPIO = LEConfigureNRSTAsGPIO;
	MCF.WriteBinaryBlob = LEWriteBinaryBlob;
	MCF.ReadBinaryBlob = LEReadBinaryBlob;
	MCF.Exit = LEExit;
	return ret;
};



