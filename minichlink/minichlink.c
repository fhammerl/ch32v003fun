// The "bootloader" blob is (C) WCH.
// The rest of the code, Copyright 2023 Charles Lohr
// Freely licensable under the MIT/x11, NewBSD Licenses, or
// public domain where applicable. 

// TODO: Can we make a unified DMPROG for reading + writing?

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "minichlink.h"
#include "../ch32v003fun/ch32v003fun.h"

static int64_t StringToMemoryAddress( const char * number );
static void StaticUpdatePROGBUFRegs( void * dev );
static int InternalUnlockBootloader( void * dev );

void TestFunction(void * v );
struct MiniChlinkFunctions MCF;

int main( int argc, char ** argv )
{
	void * dev = 0;
	if( (dev = TryInit_WCHLinkE()) )
	{
		fprintf( stderr, "Found WCH LinkE\n" );
	}
	else if( (dev = TryInit_ESP32S2CHFUN()) )
	{
		fprintf( stderr, "Found ESP32S2 Programmer\n" );
	}
	else
	{
		fprintf( stderr, "Error: Could not initialize any supported programmers\n" );
		return -32;
	}
	
	SetupAutomaticHighLevelFunctions( dev );

	int status;
	int must_be_end = 0;

	int skip_startup = 
		(argc > 1 && argv[1][0] == '-' && argv[1][1] == 'u' ) |
		(argc > 1 && argv[1][0] == '-' && argv[1][1] == 'X' );

	if( !skip_startup && MCF.SetupInterface )
	{
		if( MCF.SetupInterface( dev ) < 0 )
		{
			fprintf( stderr, "Could not setup interface.\n" );
			return -33;
		}
		printf( "Interface Setup\n" );
	}

//	TestFunction( dev );

	int iarg = 1;
	const char * lastcommand = 0;
	for( ; iarg < argc; iarg++ )
	{
		char * argchar = argv[iarg];

		lastcommand = argchar;
		if( argchar[0] != '-' )
		{
			fprintf( stderr, "Error: Need prefixing - before commands\n" );
			goto help;
		}
		if( must_be_end )
		{
			fprintf( stderr, "Error: the command '%c' cannot be followed by other commands.\n", must_be_end );
			return -1;
		}
		
keep_going:
		switch( argchar[1] )
		{
			default:
				fprintf( stderr, "Error: Unknown command %c\n", argchar[1] );
				goto help;
			case '3':
				if( MCF.Control3v3 )
					MCF.Control3v3( dev, 1 );
				else
					goto unimplemented;
				break;
			case '5':
				if( MCF.Control5v )
					MCF.Control5v( dev, 1 );
				else
					goto unimplemented;
				break;
			case 't':
				if( MCF.Control3v3 )
					MCF.Control3v3( dev, 0 );
				else
					goto unimplemented;
				break;
			case 'f':
				if( MCF.Control5v )
					MCF.Control5v( dev, 0 );
				else
					goto unimplemented;
				break;
			case 'u':
				if( MCF.Unbrick )
					MCF.Unbrick( dev );
				else
					goto unimplemented;
				break;
			case 'U':
				// Unlock Bootloader
				if( InternalUnlockBootloader( dev ) )
					goto unimplemented;
				break;
			case 'b':  //reBoot
				if( !MCF.HaltMode || MCF.HaltMode( dev, 1 ) )
					goto unimplemented;
				break;
			case 'B':  //reBoot into Bootloader
				if( !MCF.HaltMode || MCF.HaltMode( dev, 3 ) )
					goto unimplemented;
				break;
			case 'e':  //rEsume
				if( !MCF.HaltMode || MCF.HaltMode( dev, 2 ) )
					goto unimplemented;
				break;
			case 'E':  //Erase whole chip.
				if( MCF.HaltMode ) MCF.HaltMode( dev, 0 );
				if( !MCF.Erase || MCF.Erase( dev, 0, 0, 1 ) )
					goto unimplemented;
				break;
			case 'h':
				if( !MCF.HaltMode || MCF.HaltMode( dev, 0 ) )
					goto unimplemented;
				break;

			// disable NRST pin (turn it into a GPIO)
			case 'd':  // see "RSTMODE" in datasheet
				if( MCF.HaltMode ) MCF.HaltMode( dev, 0 );
				if( MCF.ConfigureNRSTAsGPIO )
					MCF.ConfigureNRSTAsGPIO( dev, 0 );
				else
					goto unimplemented;
				break;
			case 'D': // see "RSTMODE" in datasheet
				if( MCF.HaltMode ) MCF.HaltMode( dev, 0 );
				if( MCF.ConfigureNRSTAsGPIO )
					MCF.ConfigureNRSTAsGPIO( dev, 1 );
				else
					goto unimplemented;
				break;
			case 'T':
			{
				if( !MCF.PollTerminal )
					goto unimplemented;
				do
				{
					uint8_t buffer[256];
					int r = MCF.PollTerminal( dev, buffer, sizeof( buffer ), 0, 0 );
					if( r < 0 )
					{
						fprintf( stderr, "Terminal dead.  code %d\n", r );
						return -32;
					}
					if( r > 0 )
					{
						fwrite( buffer, r, 1, stdout ); 
					}
				} while( 1 );
			}
			case 'p':
			{
				if( MCF.PrintChipInfo )
					MCF.PrintChipInfo( dev ); 
				else
					goto unimplemented;
				break;
			}
			case 'X':
			{
				iarg++;
				if( iarg >= argc )
				{
					fprintf( stderr, "Vendor command requires an actual command\n" );
					goto unimplemented;
				}
				if( MCF.VendorCommand )
					if( MCF.VendorCommand( dev, argv[iarg++] ) )
						goto unimplemented;
				break;
			}
			case 'r':
			{
				if( MCF.HaltMode ) MCF.HaltMode( dev, 0 );

				if( argchar[2] != 0 )
				{
					fprintf( stderr, "Error: can't have char after paramter field\n" ); 
					goto help;
				}
				iarg++;
				argchar = 0; // Stop advancing
				if( iarg + 2 >= argc )
				{
					fprintf( stderr, "Error: missing file for -o.\n" ); 
					goto help;
				}
				const char * fname = argv[iarg++];
				uint64_t offset = StringToMemoryAddress( argv[iarg++] );

				uint64_t amount = SimpleReadNumberInt( argv[iarg], -1 );
				if( offset > 0xffffffff || amount > 0xffffffff )
				{
					fprintf( stderr, "Error: memory value request out of range\n" );
					return -9;
				}

				// Round up amount.
				amount = ( amount + 3 ) & 0xfffffffc;
				FILE * f = 0;
				int hex = 0;
				if( strcmp( fname, "-" ) == 0 )
					f = stdout;
				else if( strcmp( fname, "+" ) == 0 )
					f = stdout, hex = 1;
				else
					f = fopen( fname, "wb" );
				if( !f )
				{
					fprintf( stderr, "Error: can't open write file \"%s\"\n", fname );
					return -9;
				}
				uint8_t * readbuff = malloc( amount );

				if( MCF.ReadBinaryBlob )
				{
					if( MCF.ReadBinaryBlob( dev, offset, amount, readbuff ) < 0 )
					{
						fprintf( stderr, "Fault reading device\n" );
						return -12;
					}
				}				
				else
				{
					goto unimplemented;
				}

				if( hex )
				{
					int i;
					for( i = 0; i < amount; i++ )
					{
						if( ( i & 0xf ) == 0 )
						{
							if( i != 0 ) printf( "\n" );
							printf( "%08x: ", (uint32_t)(offset + i) );
						}
						printf( "%02x ", readbuff[i] );
					}
					printf( "\n" );
				}
				else
					fwrite( readbuff, amount, 1, f );

				free( readbuff );

				if( f != stdout ) fclose( f );
				break;
			}
			case 'w':
			{
				if( MCF.HaltMode ) MCF.HaltMode( dev, 0 );

				if( argchar[2] != 0 ) goto help;
				iarg++;
				argchar = 0; // Stop advancing
				if( iarg + 1 >= argc ) goto help;

				// Write binary.
				int len = 0;
				uint8_t * image = 0;
				const char * fname = argv[iarg++];

				if( fname[0] == '-' )
				{
					len = strlen( fname + 1 );
					image = (uint8_t*)strdup( fname + 1 );
					status = 1;
				}
				else if( fname[0] == '+' )
				{
					int hl = strlen( fname+1 );
					if( hl & 1 )
					{
						fprintf( stderr, "Error: hex input doesn't align to chars correctly.\n" );
						return -32;
					}
					len = hl/2;
					image = malloc( len );
					int i;
					for( i = 0; i < len; i ++ )
					{
						char c1 = fname[i*2+1];
						char c2 = fname[i*2+2];
						int v1, v2;
						if( c1 >= '0' && c1 <= '9' ) v1 = c1 - '0';
						else if( c1 >= 'a' && c1 <= 'f' ) v1 = c1 - 'a' + 10;
						else if( c1 >= 'A' && c1 <= 'F' ) v1 = c1 - 'A' + 10;
						else
						{
							fprintf( stderr, "Error: Bad hex\n" );
							return -32;
						}

						if( c2 >= '0' && c2 <= '9' ) v2 = c2 - '0';
						else if( c2 >= 'a' && c2 <= 'f' ) v2 = c2 - 'a' + 10;
						else if( c2 >= 'A' && c2 <= 'F' ) v2 = c2 - 'A' + 10;
						else
						{
							fprintf( stderr, "Error: Bad hex\n" );
							return -32;
						}
						image[i] = (v1<<4) | v2;
					}
					status = 1;
				}
				else
				{
					FILE * f = fopen( fname, "rb" );
					fseek( f, 0, SEEK_END );
					len = ftell( f );
					fseek( f, 0, SEEK_SET );
					image = malloc( len );
					status = fread( image, len, 1, f );
					fclose( f );
				}

				uint64_t offset = StringToMemoryAddress( argv[iarg] );
				if( offset > 0xffffffff )
				{
					fprintf( stderr, "Error: Invalid offset (%s)\n", argv[iarg] );
					exit( -44 );
				}
				if( status != 1 )
				{
					fprintf( stderr, "Error: File I/O Fault.\n" );
					exit( -10 );
				}
				if( len > 16384 )
				{
					fprintf( stderr, "Error: Image for CH32V003 too large (%d)\n", len );
					exit( -9 );
				}


				if( MCF.WriteBinaryBlob )
				{
					if( MCF.WriteBinaryBlob( dev, offset, len, image ) )
					{
						fprintf( stderr, "Error: Fault writing image.\n" );
						return -13;
					}
				}
				else
				{
					goto unimplemented;
				}

				printf( "Image written.\n" );

				free( image );
				break;
			}
			
		}
		if( argchar && argchar[2] != 0 ) { argchar++; goto keep_going; }
	}

	if( MCF.FlushLLCommands )
		MCF.FlushLLCommands( dev );

	if( MCF.Exit )
		MCF.Exit( dev );

	return 0;

help:
	fprintf( stderr, "Usage: minichlink [args]\n" );
	fprintf( stderr, " single-letter args may be combined, i.e. -3r\n" );
	fprintf( stderr, " multi-part args cannot.\n" );
	fprintf( stderr, " -3 Enable 3.3V\n" );
	fprintf( stderr, " -5 Enable 5V\n" );
	fprintf( stderr, " -t Disable 3.3V\n" );
	fprintf( stderr, " -f Disable 5V\n" );
	fprintf( stderr, " -u Clear all code flash - by power off (also can unbrick)\n" );
	fprintf( stderr, " -b Reboot out of Halt\n" );
	fprintf( stderr, " -e Resume from halt\n" );
	fprintf( stderr, " -h Place into Halt\n" );
	fprintf( stderr, " -D Configure NRST as GPIO\n" );
	fprintf( stderr, " -d Configure NRST as NRST\n" );
//	fprintf( stderr, " -P Enable Read Protection (UNTESTED)\n" );
//	fprintf( stderr, " -p Disable Read Protection (UNTESTED)\n" );
	fprintf( stderr, " -w [binary image to write] [address, decimal or 0x, try0x08000000]\n" );
	fprintf( stderr, " -r [output binary image] [memory address, decimal or 0x, try 0x08000000] [size, decimal or 0x, try 16384]\n" );
	fprintf( stderr, "   Note: for memory addresses, you can use 'flash' 'launcher' 'bootloader' 'option' 'ram' and say \"ram+0x10\" for instance\n" );
	fprintf( stderr, "   For filename, you can use - for raw or + for hex.\n" );
	fprintf( stderr, " -T is a terminal. This MUST be the last argument.  You MUST have resumed or \n" );

	return -1;	

unimplemented:
	fprintf( stderr, "Error: Command '%s' unimplemented on this programmer.\n", lastcommand );
	return -1;
}


#if defined(WINDOWS) || defined(WIN32) || defined(_WIN32)
#define strtoll _strtoi64
#endif

static int StaticUnlockFlash( void * dev, struct InternalState * iss );

int64_t SimpleReadNumberInt( const char * number, int64_t defaultNumber )
{
	if( !number || !number[0] ) return defaultNumber;
	int radix = 10;
	if( number[0] == '0' )
	{
		char nc = number[1];
		number+=2;
		if( nc == 0 ) return 0;
		else if( nc == 'x' ) radix = 16;
		else if( nc == 'b' ) radix = 2;
		else { number--; radix = 8; }
	}
	char * endptr;
	uint64_t ret = strtoll( number, &endptr, radix );
	if( endptr == number )
	{
		return defaultNumber;
	}
	else
	{
		return ret;
	}
}

static int64_t StringToMemoryAddress( const char * number )
{
	uint32_t base = 0;

	if( strncmp( number, "flash", 5 ) == 0 )       base = 0x08000000, number += 5;
	if( strncmp( number, "launcher", 8 ) == 0 )    base = 0x1FFFF000, number += 8;
	if( strncmp( number, "bootloader", 10 ) == 0 ) base = 0x1FFFF000, number += 10;
	if( strncmp( number, "option", 6 ) == 0 )      base = 0x1FFFF800, number += 6;
	if( strncmp( number, "user", 4 ) == 0 )        base = 0x1FFFF800, number += 4;
	if( strncmp( number, "ram", 3 ) == 0 )         base = 0x20000000, number += 3;

	if( base )
	{
		if( *number != '+' )
			return base;
		number++;
		return base + SimpleReadNumberInt( number, 0 );
	}
	return SimpleReadNumberInt( number, -1 );
}

static int DefaultWaitForFlash( void * dev )
{
	uint32_t rw, timeout = 0;
	do
	{
		rw = 0;
		MCF.ReadWord( dev, (intptr_t)&FLASH->STATR, &rw ); // FLASH_STATR => 0x4002200C
		if( timeout++ > 100 ) return -1;
	} while(rw & 1);  // BSY flag.

	if( rw & FLASH_STATR_WRPRTERR )
	{
		fprintf( stderr, "Memory Protection Error\n" );
		return -44;
	}

	return 0;
}

static int DefaultWaitForDoneOp( void * dev )
{
	int r;
	uint32_t rrv;
	do
	{
		r = MCF.ReadReg32( dev, DMABSTRACTCS, &rrv );
		if( r ) return r;
	}
	while( rrv & (1<<12) );
	if( (rrv >> 8 ) & 7 )
	{
		fprintf( stderr, "Fault writing memory (DMABSTRACTS = %08x)\n", rrv );
		MCF.WriteReg32( dev, DMABSTRACTCS, 0x00000700 );
		return -9;
	}
	return 0;
}

int DefaultSetupInterface( void * dev )
{
	struct InternalState * iss = (struct InternalState*)(((struct ProgrammerStructBase*)dev)->internal);

	if( MCF.Control3v3 ) MCF.Control3v3( dev, 1 );
	if( MCF.DelayUS ) MCF.DelayUS( dev, 16000 );
	MCF.WriteReg32( dev, DMSHDWCFGR, 0x5aa50000 | (1<<10) ); // Shadow Config Reg
	MCF.WriteReg32( dev, DMCFGR, 0x5aa50000 | (1<<10) ); // CFGR (1<<10 == Allow output from slave)
	MCF.WriteReg32( dev, DMCFGR, 0x5aa50000 | (1<<10) ); // Bug in silicon?  If coming out of cold boot, and we don't do our little "song and dance" this has to be called.

	// Read back chip status.  This is really basic.
	uint32_t reg = 0;
	int r = MCF.ReadReg32( dev, DMSTATUS, &reg );
	if( r >= 0 )
	{
		// Valid R.
		if( reg == 0x00000000 || reg == 0xffffffff )
		{
			fprintf( stderr, "Error: Setup chip failed. Got code %08x\n", reg );
			return -9;
		}
	}
	else
	{
		fprintf( stderr, "Error: Could not read chip code.\n" );
		return r;
	}

	iss->statetag = STTAG( "STRT" );
	return 0;
}

static void StaticUpdatePROGBUFRegs( void * dev )
{
	MCF.WriteReg32( dev, DMDATA0, 0xe00000f4 );   // DATA0's location in memory.
	MCF.WriteReg32( dev, DMCOMMAND, 0x0023100a ); // Copy data to x10
	MCF.WriteReg32( dev, DMDATA0, 0xe00000f8 );   // DATA1's location in memory.
	MCF.WriteReg32( dev, DMCOMMAND, 0x0023100b ); // Copy data to x11
	MCF.WriteReg32( dev, DMDATA0, 0x40022010 ); //FLASH->CTLR
	MCF.WriteReg32( dev, DMCOMMAND, 0x0023100c ); // Copy data to x12
	MCF.WriteReg32( dev, DMDATA0, CR_PAGE_PG|CR_BUF_LOAD);
	MCF.WriteReg32( dev, DMCOMMAND, 0x0023100d ); // Copy data to x13
}

static int InternalUnlockBootloader( void * dev )
{
	if( !MCF.WriteWord ) return -99;
	int ret = 0;
	uint32_t OBTKEYR;
	ret |= MCF.WriteWord( dev, 0x40022028, 0x45670123 ); //(FLASH_BOOT_MODEKEYP)
	ret |= MCF.WriteWord( dev, 0x40022028, 0xCDEF89AB ); //(FLASH_BOOT_MODEKEYP)
	ret |= MCF.ReadWord( dev, 0x40022008, &OBTKEYR ); //(FLASH_OBTKEYR)
	if( ret )
	{
		fprintf( stderr, "Error operating with OBTKEYR\n" );
		return -1;
	}
	if( OBTKEYR & (1<<15) )
	{
		fprintf( stderr, "Error: Could not unlock boot section (%08x)\n", OBTKEYR );
	}
	OBTKEYR |= (1<<14); // Configure for boot-to-bootload.
	ret |= MCF.WriteWord( dev, 0x40022008, OBTKEYR );
	ret |= MCF.ReadWord( dev, 0x40022008, &OBTKEYR ); //(FLASH_OBTKEYR)
	printf( "FLASH_OBTKEYR = %08x (%d)\n", OBTKEYR, ret );
	return ret;
}



static int DefaultWriteHalfWord( void * dev, uint32_t address_to_write, uint32_t data )
{
	int ret = 0;
	struct InternalState * iss = (struct InternalState*)(((struct ProgrammerStructBase*)dev)->internal);
	if( MCF.VoidHighLevelState ) MCF.VoidHighLevelState( dev );
	iss->statetag = STTAG( "XXXX" );

	MCF.WriteReg32( dev, DMABSTRACTAUTO, 0x00000000 ); // Disable Autoexec.

	// Different address, so we don't need to re-write all the program regs.
	// sh x8,0(x9)  // Write to the address.
	MCF.WriteReg32( dev, DMPROGBUF0, 0x00849023 );
	MCF.WriteReg32( dev, DMPROGBUF1, 0x00100073 ); // c.ebreak

	MCF.WriteReg32( dev, DMDATA0, address_to_write );
	MCF.WriteReg32( dev, DMCOMMAND, 0x00231009 ); // Copy data to x9
	MCF.WriteReg32( dev, DMDATA0, data );
	MCF.WriteReg32( dev, DMCOMMAND, 0x00271008 ); // Copy data to x8, and execute program.

	ret |= MCF.WaitForDoneOp( dev );
	iss->currentstateval = -1;


	return ret;
}

static int DefaultReadHalfWord( void * dev, uint32_t address_to_write, uint32_t * data )
{
	int ret = 0;
	struct InternalState * iss = (struct InternalState*)(((struct ProgrammerStructBase*)dev)->internal);
	if( MCF.VoidHighLevelState ) MCF.VoidHighLevelState( dev );
	iss->statetag = STTAG( "XXXX" );

	MCF.WriteReg32( dev, DMABSTRACTAUTO, 0x00000000 ); // Disable Autoexec.

	// Different address, so we don't need to re-write all the program regs.
	// lh x8,0(x9)  // Write to the address.
	MCF.WriteReg32( dev, DMPROGBUF0, 0x00049403 );
	MCF.WriteReg32( dev, DMPROGBUF1, 0x00100073 ); // c.ebreak

	MCF.WriteReg32( dev, DMDATA0, address_to_write );
	MCF.WriteReg32( dev, DMCOMMAND, 0x00231009 ); // Copy data to x9
	MCF.WriteReg32( dev, DMCOMMAND, 0x00241000 ); // Only execute.
	MCF.WriteReg32( dev, DMCOMMAND, 0x00221008 ); // Read x8 into DATA0.

	ret |= MCF.WaitForDoneOp( dev );
	iss->currentstateval = -1;


	return ret | MCF.ReadReg32( dev, DMDATA0, data );
}


static int DefaultWriteWord( void * dev, uint32_t address_to_write, uint32_t data )
{
	struct InternalState * iss = (struct InternalState*)(((struct ProgrammerStructBase*)dev)->internal);
	int ret = 0;

	int is_flash = 0;
	if( ( address_to_write & 0xff000000 ) == 0x08000000 || ( address_to_write & 0x1FFFF800 ) == 0x1FFFF000 )
	{
		// Is flash.
		is_flash = 1;
	}

	if( iss->statetag != STTAG( "WRSQ" ) || is_flash != iss->lastwriteflags )
	{
		int did_disable_req = 0;
		if( iss->statetag != STTAG( "WRSQ" ) )
		{
			MCF.WriteReg32( dev, DMABSTRACTAUTO, 0x00000000 ); // Disable Autoexec.
			did_disable_req = 1;
			// Different address, so we don't need to re-write all the program regs.
			// c.lw x9,0(x11) // Get the address to write to. 
			// c.sw x8,0(x9)  // Write to the address.
			MCF.WriteReg32( dev, DMPROGBUF0, 0xc0804184 );
			// c.addi x9, 4
			// c.sw x9,0(x11)
			MCF.WriteReg32( dev, DMPROGBUF1, 0xc1840491 );

			if( iss->statetag != STTAG( "RDSQ" ) )
			{
				StaticUpdatePROGBUFRegs( dev );
			}
		}

		if( iss->lastwriteflags != is_flash || iss->statetag != STTAG( "WRSQ" ) )
		{
			// If we are doing flash, we have to ack, otherwise we don't want to ack.
			if( is_flash )
			{
				// After writing to memory, also hit up page load flag.
				// c.sw x13,0(x12) // Acknowledge the page write.
				// c.ebreak
				MCF.WriteReg32( dev, DMPROGBUF2, 0x9002c214 );
			}
			else
			{
				MCF.WriteReg32( dev, DMPROGBUF2, 0x00019002 ); // c.ebreak
			}
		}

		MCF.WriteReg32( dev, DMDATA1, address_to_write );
		MCF.WriteReg32( dev, DMDATA0, data );

		if( did_disable_req )
		{
			MCF.WriteReg32( dev, DMCOMMAND, 0x00271008 ); // Copy data to x8, and execute program.
			MCF.WriteReg32( dev, DMABSTRACTAUTO, 1 ); // Enable Autoexec.
		}
		iss->lastwriteflags = is_flash;


		iss->statetag = STTAG( "WRSQ" );
		iss->currentstateval = address_to_write;

		if( is_flash )
			ret |= MCF.WaitForDoneOp( dev );
	}
	else
	{
		if( address_to_write != iss->currentstateval )
		{
			MCF.WriteReg32( dev, DMABSTRACTAUTO, 0 ); // Disable Autoexec.
			MCF.WriteReg32( dev, DMDATA1, address_to_write );
			MCF.WriteReg32( dev, DMABSTRACTAUTO, 1 ); // Enable Autoexec.
		}
		MCF.WriteReg32( dev, DMDATA0, data );
		if( is_flash )
		{
			// XXX TODO: This likely can be a very short delay.
			// XXX POSSIBLE OPTIMIZATION REINVESTIGATE.
			ret |= MCF.WaitForDoneOp( dev );
		}
		else
		{
			ret |= MCF.WaitForDoneOp( dev );
		}
	}


	iss->currentstateval += 4;

	return 0;
}


int DefaultWriteBinaryBlob( void * dev, uint32_t address_to_write, uint32_t blob_size, uint8_t * blob )
{
	// NOTE IF YOU FIX SOMETHING IN THIS FUNCTION PLEASE ALSO UPDATE THE PROGRAMMERS.
	//  this is only fallback functionality for really realy basic programmers.

	uint32_t rw;
	struct InternalState * iss = (struct InternalState*)(((struct ProgrammerStructBase*)dev)->internal);
	int is_flash = 0;

	if( blob_size == 0 ) return 0;

	if( (address_to_write & 0xff000000) == 0x08000000 || (address_to_write & 0xff000000) == 0x00000000 || (address_to_write & 0x1FFFF800) == 0x1FFFF000 ) 
		is_flash = 1;

	if( is_flash && MCF.BlockWrite64 && ( address_to_write & 0x3f ) == 0 )
	{
		int i;
		for( i = 0; i < blob_size; i+= 64 )
		{
			int r = MCF.BlockWrite64( dev, address_to_write + i, blob + i );
			if( r )
			{
				fprintf( stderr, "Error writing block at memory %08x\n", address_to_write );
				return r;
			}
		}
		return 0;
	}

	if( is_flash ) 
	{
		// Need to unlock flash.
		// Flash reg base = 0x40022000,
		// FLASH_MODEKEYR => 0x40022024
		// FLASH_KEYR => 0x40022004

		if( !iss->flash_unlocked )
		{
			if( ( rw = StaticUnlockFlash( dev, iss ) ) )
				return rw;
		}

		is_flash = 1;

		printf( "Erasing TO %08x %08x\n", address_to_write, blob_size );
		MCF.Erase( dev, address_to_write, blob_size, 0 );
	}
	printf( "Done\n" );
	MCF.FlushLLCommands( dev );
	MCF.DelayUS( dev, 100 ); // Why do we need this?

	uint32_t wp = address_to_write;
	uint32_t ew = wp + blob_size;
	int group = -1;

	while( wp < ew )
	{
		if( is_flash )
		{
			group = (wp & 0xffffffc0);
			MCF.WriteWord( dev, 0x40022010, CR_PAGE_PG ); // THIS IS REQUIRED, (intptr_t)&FLASH->CTLR = 0x40022010
			MCF.WriteWord( dev, 0x40022010, CR_BUF_RST | CR_PAGE_PG );  // (intptr_t)&FLASH->CTLR = 0x40022010

			int j;
			for( j = 0; j < 16; j++ )
			{
				int index = (wp-address_to_write);
				uint32_t data = 0xffffffff;
				if( index + 3 < blob_size )
					data = ((uint32_t*)blob)[index/4];
				else if( (int32_t)(blob_size - index) > 0 )
				{
					printf( "%d %d\n", blob_size, index );
					memcpy( &data, &blob[index], blob_size - index );
				}
				MCF.WriteWord( dev, wp, data );
				wp += 4;
			}
			MCF.WriteWord( dev, 0x40022014, group );  //0x40022014 -> FLASH->ADDR
			MCF.WriteWord( dev, 0x40022010, CR_PAGE_PG|CR_STRT_Set ); // 0x40022010 -> FLASH->CTLR
			if( MCF.WaitForFlash ) MCF.WaitForFlash( dev );
		}
		else
		{
			int index = (wp-address_to_write);
			uint32_t data = 0xffffffff;
			if( index + 3 < blob_size )
				data = ((uint32_t*)blob)[index/4];
			else if( (int32_t)(blob_size - index) > 0 )
				memcpy( &data, &blob[index], blob_size - index );
			MCF.WriteWord( dev, wp, data );
			wp += 4;
		}
	}

	if( is_flash )
	{
		if( MCF.WaitForFlash && MCF.WaitForFlash( dev ) ) goto timedout;
	}
	return 0;
timedout:
	fprintf( stderr, "Timed out\n" );
	return -5;
}

static int DefaultReadWord( void * dev, uint32_t address_to_read, uint32_t * data )
{
	struct InternalState * iss = (struct InternalState*)(((struct ProgrammerStructBase*)dev)->internal);
	if( iss->statetag != STTAG( "RDSQ" ) || address_to_read != iss->currentstateval )
	{
		if( iss->statetag != STTAG( "RDSQ" ) )
		{
			MCF.WriteReg32( dev, DMABSTRACTAUTO, 0 ); // Disable Autoexec.
			// c.lw x8,0(x11) // Pull the address from DATA1
			// c.lw x9,0(x8)  // Read the data at that location.
			MCF.WriteReg32( dev, DMPROGBUF0, 0x40044180 );
			// c.addi x8, 4
			// c.sw x9, 0(x10) // Write back to DATA0
			MCF.WriteReg32( dev, DMPROGBUF1, 0xc1040411 );
			// c.sw x8, 0(x11) // Write addy to DATA1
			// c.ebreak
			MCF.WriteReg32( dev, DMPROGBUF2, 0x9002c180 );

			if( iss->statetag != STTAG( "WRSQ" ) )
			{
				StaticUpdatePROGBUFRegs( dev );
			}
			MCF.WriteReg32( dev, DMABSTRACTAUTO, 1 ); // Enable Autoexec.
		}

		MCF.WriteReg32( dev, DMDATA1, address_to_read );
		MCF.WriteReg32( dev, DMCOMMAND, 0x00241000 ); // Only execute.

		iss->statetag = STTAG( "RDSQ" );
		iss->currentstateval = address_to_read;

		MCF.WaitForDoneOp( dev );
	}

	iss->currentstateval += 4;

	return MCF.ReadReg32( dev, DMDATA0, data );
}

static int StaticUnlockFlash( void * dev, struct InternalState * iss )
{
	uint32_t rw;
	MCF.ReadWord( dev, 0x40022010, &rw );  // FLASH->CTLR = 0x40022010
	if( rw & 0x8080 ) 
	{

		MCF.WriteWord( dev, 0x40022004, 0x45670123 ); // FLASH->KEYR = 0x40022004
		MCF.WriteWord( dev, 0x40022004, 0xCDEF89AB );
		MCF.WriteWord( dev, 0x40022008, 0x45670123 ); // OBKEYR = 0x40022008
		MCF.WriteWord( dev, 0x40022008, 0xCDEF89AB );
		MCF.WriteWord( dev, 0x40022024, 0x45670123 ); // MODEKEYR = 0x40022024
		MCF.WriteWord( dev, 0x40022024, 0xCDEF89AB );

		MCF.ReadWord( dev, 0x40022010, &rw ); // FLASH->CTLR = 0x40022010
		if( rw & 0x8080 ) 
		{
			fprintf( stderr, "Error: Flash is not unlocked (CTLR = %08x)\n", rw );
			return -9;
		}
	}
	iss->flash_unlocked = 1;
	return 0;
}

int DefaultErase( void * dev, uint32_t address, uint32_t length, int type )
{
	struct InternalState * iss = (struct InternalState*)(((struct ProgrammerStructBase*)dev)->internal);
	uint32_t rw;

	if( !iss->flash_unlocked )
	{
		if( ( rw = StaticUnlockFlash( dev, iss ) ) )
			return rw;
	}

	if( type == 1 )
	{
		// Whole-chip flash
		iss->statetag = STTAG( "XXXX" );
		printf( "Whole-chip erase\n" );
		MCF.WriteWord( dev, (intptr_t)&FLASH->CTLR, 0 );
		MCF.WriteWord( dev, (intptr_t)&FLASH->CTLR, FLASH_CTLR_MER  );
		MCF.WriteWord( dev, (intptr_t)&FLASH->CTLR, CR_STRT_Set|FLASH_CTLR_MER );
		if( MCF.WaitForFlash && MCF.WaitForFlash( dev ) ) return -11;		
		MCF.WriteWord( dev, (intptr_t)&FLASH->CTLR, 0 );
	}
	else
	{
		// 16.4.7, Step 3: Check the BSY bit of the FLASH_STATR register to confirm that there are no other programming operations in progress.
		// skip (we make sure at the end)

		int chunk_to_erase = address;

		while( chunk_to_erase < address + length )
		{
			// Step 4:  set PAGE_ER of FLASH_CTLR(0x40022010)
			MCF.WriteWord( dev, (intptr_t)&FLASH->CTLR, CR_PAGE_ER ); // Actually FTER

			// Step 5: Write the first address of the fast erase page to the FLASH_ADDR register.
			MCF.WriteWord( dev, (intptr_t)&FLASH->ADDR, chunk_to_erase  );

			// Step 6: Set the STAT bit of FLASH_CTLR register to '1' to initiate a fast page erase (64 bytes) action.
			MCF.WriteWord( dev, (intptr_t)&FLASH->CTLR, CR_STRT_Set|CR_PAGE_ER );
			if( MCF.WaitForFlash && MCF.WaitForFlash( dev ) ) return -99;
			chunk_to_erase+=64;
		}
	}
	return 0;
}

int DefaultReadBinaryBlob( void * dev, uint32_t address_to_read_from, uint32_t read_size, uint8_t * blob )
{
	uint32_t rpos = address_to_read_from;
	uint32_t rend = address_to_read_from + read_size;
	while( rpos < rend )
	{
		uint32_t rw;
		int r = DefaultReadWord( dev, rpos, &rw );
		if( r ) return r;
		int remain = rend - rpos;
		if( remain > 3 ) remain = 4;
		memcpy( blob, &rw, remain );
		blob += 4;
		rpos += 4;
	}
	return 0;
}


static int DefaultHaltMode( void * dev, int mode )
{
	struct InternalState * iss = (struct InternalState*)(((struct ProgrammerStructBase*)dev)->internal);
	switch ( mode )
	{
	case 0:
		MCF.WriteReg32( dev, DMCONTROL, 0x80000001 ); // Make the debug module work properly.
		MCF.WriteReg32( dev, DMCONTROL, 0x80000001 ); // Initiate a halt request.
		MCF.WriteReg32( dev, DMCONTROL, 0x00000001 ); // Clear Halt Request.
		MCF.FlushLLCommands( dev );
		break;
	case 1:
		MCF.WriteReg32( dev, DMCONTROL, 0x80000001 ); // Make the debug module work properly.
		MCF.WriteReg32( dev, DMCONTROL, 0x80000001 ); // Initiate a halt request.
		MCF.WriteReg32( dev, DMCONTROL, 0x80000003 ); // Reboot.
		MCF.WriteReg32( dev, DMCONTROL, 0x40000001 ); // resumereq
		MCF.FlushLLCommands( dev );
		break;
	case 2:
		MCF.WriteReg32( dev, DMCONTROL, 0x40000001 ); // resumereq
		MCF.FlushLLCommands( dev );
		break;
	case 3:
		MCF.WriteReg32( dev, DMCONTROL, 0x80000001 ); // Make the debug module work properly.
		MCF.WriteReg32( dev, DMCONTROL, 0x80000001 ); // Initiate a halt request.

		MCF.WriteWord( dev, (intptr_t)&FLASH->KEYR, FLASH_KEY1 );
		MCF.WriteWord( dev, (intptr_t)&FLASH->KEYR, FLASH_KEY2 );
		MCF.WriteWord( dev, (intptr_t)&FLASH->BOOT_MODEKEYR, FLASH_KEY1 );
		MCF.WriteWord( dev, (intptr_t)&FLASH->BOOT_MODEKEYR, FLASH_KEY2 );
		MCF.WriteWord( dev, (intptr_t)&FLASH->STATR, 1<<14 );
		MCF.WriteWord( dev, (intptr_t)&FLASH->CTLR, CR_LOCK_Set );

		MCF.WriteReg32( dev, DMCONTROL, 0x80000003 ); // Reboot.
		MCF.WriteReg32( dev, DMCONTROL, 0x40000001 ); // resumereq
		MCF.FlushLLCommands( dev );
		break;
	}
	iss->processor_in_mode = mode;
	return 0;
}

// Returns positive if received text.
// Returns negative if error.
// Returns 0 if no text waiting.
// maxlen MUST be at least 8 characters.  We null terminate.
int DefaultPollTerminal( void * dev, uint8_t * buffer, int maxlen, uint32_t leaveflagA, int leaveflagB )
{
	struct InternalState * iss = (struct InternalState*)(((struct ProgrammerStructBase*)dev)->internal);

	int r;
	uint32_t rr;
	if( iss->statetag != STTAG( "TERM" ) )
	{
		MCF.WriteReg32( dev, DMABSTRACTAUTO, 0x00000000 ); // Disable Autoexec.
		iss->statetag = STTAG( "TERM" );
	}
	r = MCF.ReadReg32( dev, DMDATA0, &rr );
	if( r < 0 ) return r;

	if( maxlen < 8 ) return -9;

	// DMDATA1:
	//  bit  7 = host-acknowledge.
	if( rr & 0x80 )
	{
		int ret = 0;
		int num_printf_chars = (rr & 0xf)-4;

		if( num_printf_chars > 0 && num_printf_chars <= 7)
		{
			if( num_printf_chars > 3 )
			{
				uint32_t r2;
				r = MCF.ReadReg32( dev, DMDATA1, &r2 );
				memcpy( buffer+3, &r2, num_printf_chars - 3 );
			}
			int firstrem = num_printf_chars;
			if( firstrem > 3 ) firstrem = 3;
			memcpy( buffer, ((uint8_t*)&rr)+1, firstrem );
			buffer[num_printf_chars] = 0;
			ret = num_printf_chars;
		}
		if( leaveflagA ) MCF.WriteReg32( dev, DMDATA1, leaveflagB );
		MCF.WriteReg32( dev, DMDATA0, leaveflagA ); // Write that we acknowledge the data.
		return ret;
	}
	else
	{
		return 0;
	}
}

int DefaultUnbrick( void * dev )
{
	// TODO: Why doesn't this work on the ESP32S2?

	printf( "Entering Unbrick Mode\n" );
	MCF.Control3v3( dev, 0 );
	MCF.DelayUS( dev, 60000 );
	MCF.DelayUS( dev, 60000 );
	MCF.DelayUS( dev, 60000 );
	MCF.DelayUS( dev, 60000 );
	MCF.Control3v3( dev, 1 );
	MCF.DelayUS( dev, 100 );
	MCF.FlushLLCommands( dev );
	printf( "Connection starting\n" );
	int timeout = 0;
	int max_timeout = 500;
	uint32_t ds = 0;
	for( timeout = 0; timeout < max_timeout; timeout++ )
	{
		MCF.DelayUS( dev, 10 );
		MCF.WriteReg32( dev, DMSHDWCFGR, 0x5aa50000 | (1<<10) ); // Shadow Config Reg
		MCF.WriteReg32( dev, DMCFGR, 0x5aa50000 | (1<<10) ); // CFGR (1<<10 == Allow output from slave)
		MCF.WriteReg32( dev, DMCFGR, 0x5aa50000 | (1<<10) ); // Bug in silicon?  If coming out of cold boot, and we don't do our little "song and dance" this has to be called.
		MCF.FlushLLCommands( dev );
		int r = MCF.ReadReg32( dev, DMSTATUS, &ds );
		printf( "/%d/%08x\n", r, ds );
		MCF.FlushLLCommands( dev );
		if( ds != 0xffffffff && ds != 0x00000000 ) break;
	}

	// Make sure we are in halt.
	MCF.WriteReg32( dev, DMCONTROL, 0x80000001 ); // Make the debug module work properly.
	MCF.WriteReg32( dev, DMCONTROL, 0x80000001 ); // Initiate a halt request.
	MCF.WriteReg32( dev, DMCONTROL, 0x00000001 ); // Clear Halt Request.
	MCF.FlushLLCommands( dev );

	if( timeout == max_timeout ) 
	{
		fprintf( stderr, "Timed out trying to unbrick\n" );
		return -5;
	}
	MCF.Erase( dev, 0, 0, 1);
	MCF.FlushLLCommands( dev );
	return -5;
}

int DefaultConfigureNRSTAsGPIO( void * dev, int one_if_yes_gpio  )
{
	fprintf( stderr, "Error: DefaultConfigureNRSTAsGPIO does not work via the programmer here.  Please see the demo \"optionbytes\"\n" );
	return -5;
#if 0
	int ret = 0;
	uint32_t csw;


	if( MCF.ReadWord( dev, 0x1FFFF800, &csw ) )
	{
		fprintf( stderr, "Error: failed to get user word\n" );
		return -5;
	}

	printf( "CSW WAS : %08x\n", csw );

	MCF.WriteWord( dev, 0x40022008, 0x45670123 ); // OBKEYR = 0x40022008
	MCF.WriteWord( dev, 0x40022008, 0xCDEF89AB );
	MCF.WriteWord( dev, 0x40022004, 0x45670123 ); // FLASH->KEYR = 0x40022004
	MCF.WriteWord( dev, 0x40022004, 0xCDEF89AB );
	MCF.WriteWord( dev, 0x40022024, 0x45670123 ); // MODEKEYR = 0x40022024
	MCF.WriteWord( dev, 0x40022024, 0xCDEF89AB );

//XXXX THIS DOES NOT WORK IT CANNOT ERASE.
	uint32_t ctlr;
	if( MCF.ReadWord( dev, 0x40022010, &ctlr ) ) // FLASH->CTLR = 0x40022010
	{
		return -9;
	}
	ctlr |= CR_OPTER_Set | CR_STRT_Set; // OBER
	MCF.WriteWord( dev, 0x40022010, ctlr ); // FLASH->CTLR = 0x40022010
	ret |= MCF.WaitForDoneOp( dev );
	ret |= MCF.WaitForFlash( dev );

	MCF.WriteHalfWord( dev, (intptr_t)&OB->RDPR, RDP_Key );

    ctlr &=~CR_OPTER_Reset;
	MCF.WriteWord( dev, 0x40022010, ctlr ); // FLASH->CTLR = 0x40022010
	ret |= MCF.WaitForDoneOp( dev );
	ret |= MCF.WaitForFlash( dev );
    ctlr |= CR_OPTPG_Set;
	MCF.WriteWord( dev, 0x40022010, ctlr ); // FLASH->CTLR = 0x40022010
	ret |= MCF.WaitForDoneOp( dev );
	ret |= MCF.WaitForFlash( dev );
    ctlr &=~CR_OPTPG_Reset;
	MCF.WriteWord( dev, 0x40022010, ctlr ); // FLASH->CTLR = 0x40022010
	ret |= MCF.WaitForDoneOp( dev );
	ret |= MCF.WaitForFlash( dev );


// This does work to write the option bytes, but does NOT work to erase.

	if( MCF.ReadWord( dev, 0x40022010, &ctlr ) ) // FLASH->CTLR = 0x40022010
	{
		return -9;
	}
	ctlr |= CR_OPTPG_Set; //OBPG
	MCF.WriteWord( dev, 0x40022010, ctlr ); // FLASH->CTLR = 0x40022010
	ret |= MCF.WaitForDoneOp( dev );
	ret |= MCF.WaitForFlash( dev );

	uint32_t config = OB_IWDG_HW | OB_STOP_NoRST | OB_STDBY_NoRST | (one_if_yes_gpio?OB_RST_NoEN:OB_RST_EN_DT1ms) | (uint16_t)0xE0;
	printf( "Config (%08x): %08x\n", (intptr_t)&OB->USER, config );
	MCF.WriteHalfWord( dev,  (intptr_t)&OB->USER, config );

	ret |= MCF.WaitForDoneOp( dev );
	ret |= MCF.WaitForFlash( dev );

	ctlr &= CR_OPTPG_Reset;
	MCF.WriteWord( dev, 0x40022010, ctlr ); // FLASH->CTLR = 0x40022010


	if( MCF.ReadWord( dev, 0x1FFFF800, &csw ) )
	{
		fprintf( stderr, "Error: failed to get user word\n" );
		return -5;
	}

	//csw >>= 16; // Only want bottom part of word.
	printf( "CSW: %08x\n", csw );

#if 0
	uint32_t prevuser;
	if( MCF.ReadWord( dev, 0x1FFFF800, &prevuser ) )
	{
		fprintf( stderr, "Error: failed to get user word\n" );
		return -5;
	}

	ret |= MCF.WaitForFlash( dev );

	// Erase.
	MCF.ReadWord( dev, 0x40022010, &csw ); // FLASH->CTLR = 0x40022010
	csw |= 1<<5;//OBER;
	MCF.WriteWord( dev, 0x40022010, csw ); // FLASH->CTLR = 0x40022010
	MCF.WriteHalfWord( dev, 0x1FFFF802, 0xffff );
	ret |= MCF.WaitForDoneOp( dev );
	ret |= MCF.WaitForFlash( dev );

	MCF.ReadWord( dev, 0x40022010, &csw ); // FLASH->CTLR = 0x40022010
	printf( "CTLR: %08x\n", csw );
	csw |= 1<<9;//OBPG, OBWRE
	MCF.WriteWord( dev, 0x40022010, csw );

	int j;
	for( j = 0; j < 5; j++ )
	{
		if( MCF.ReadWord( dev, 0x1FFFF800, &prevuser ) )
		{
			fprintf( stderr, "Error: failed to get user word\n" );
			return -5;
		}

		//csw >>= 16; // Only want bottom part of word.
		printf( "CSW was: %08x\n", prevuser );
		csw = prevuser >> 16;
		csw = csw & 0xe7e7;
		csw |= (one_if_yes_gpio?0b11:0b00)<<(3+0);
		csw |= (one_if_yes_gpio?0b00:0b11)<<(3+8);
		printf( "CSW writing: %08x\n", csw );
		MCF.WriteHalfWord( dev, 0x1FFFF802, csw );
		ret |= MCF.WaitForDoneOp( dev );
		ret |= MCF.WaitForFlash( dev );
	}


	MCF.ReadWord( dev, 0x40022010, &csw ); // FLASH->CTLR = 0x40022010
	printf( "CTLR: %08x\n", csw );
	csw &= ~(1<<9);//OBPG, OBWRE
	MCF.WriteWord( dev, 0x40022010, csw );

#endif
	printf( "RET: %d\n", ret );
	return 0;
#endif
}

int DefaultPrintChipInfo( void * dev )
{
	uint32_t reg;
	MCF.HaltMode( dev, 0 );

	if( MCF.ReadWord( dev, 0x1FFFF800, &reg ) ) goto fail;	
	printf( "USER/RDPR: %08x\n", reg );
/*	if( MCF.ReadWord( dev, 0x1FFFF804, &reg ) ) goto fail;	
	printf( "NDATA: %08x\n", reg );
	if( MCF.ReadWord( dev, 0x1FFFF808, &reg ) ) goto fail;	
	printf( "WRPR01: %08x\n", reg );
	if( MCF.ReadWord( dev, 0x1FFFF80c, &reg ) ) goto fail;	
	printf( "WRPR23: %08x\n", reg );*/
	if( MCF.ReadWord( dev, 0x1FFFF7E0, &reg ) ) goto fail;
	printf( "Flash Size: %d kB\n", (reg&0xffff) );
	if( MCF.ReadWord( dev, 0x1FFFF7E8, &reg ) ) goto fail;	
	printf( "R32_ESIG_UNIID1: %08x\n", reg );
	if( MCF.ReadWord( dev, 0x1FFFF7EC, &reg ) ) goto fail;	
	printf( "R32_ESIG_UNIID2: %08x\n", reg );
	if( MCF.ReadWord( dev, 0x1FFFF7F0, &reg ) ) goto fail;	
	printf( "R32_ESIG_UNIID3: %08x\n", reg );
	return 0;
fail:
	fprintf( stderr, "Error: Failed to get chip details\n" );
	return -11;
}

int SetupAutomaticHighLevelFunctions( void * dev )
{
	// Will populate high-level functions from low-level functions.
	if( MCF.WriteReg32 == 0 || MCF.ReadReg32 == 0 ) return -5;

	// Else, TODO: Build the high level functions from low level functions.
	// If a high-level function alrady exists, don't override.
	
	if( !MCF.SetupInterface )
		MCF.SetupInterface = DefaultSetupInterface;
	if( !MCF.WriteBinaryBlob )
		MCF.WriteBinaryBlob = DefaultWriteBinaryBlob;
	if( !MCF.ReadBinaryBlob )
		MCF.ReadBinaryBlob = DefaultReadBinaryBlob;
	if( !MCF.WriteWord )
		MCF.WriteWord = DefaultWriteWord;
	if( !MCF.WriteHalfWord )
		MCF.WriteHalfWord = DefaultWriteHalfWord;
	if( !MCF.ReadWord )
		MCF.ReadWord = DefaultReadWord;
	if( !MCF.ReadHalfWord )
		MCF.ReadHalfWord = DefaultReadHalfWord;
	if( !MCF.Erase )
		MCF.Erase = DefaultErase;
	if( !MCF.HaltMode )
		MCF.HaltMode = DefaultHaltMode;
	if( !MCF.PollTerminal )
		MCF.PollTerminal = DefaultPollTerminal;
	if( !MCF.WaitForFlash )
		MCF.WaitForFlash = DefaultWaitForFlash;
	if( !MCF.WaitForDoneOp )
		MCF.WaitForDoneOp = DefaultWaitForDoneOp;
	if( !MCF.PrintChipInfo )
		MCF.PrintChipInfo = DefaultPrintChipInfo;
	if( !MCF.Unbrick )
		MCF.Unbrick = DefaultUnbrick;
	if( !MCF.ConfigureNRSTAsGPIO )
		MCF.ConfigureNRSTAsGPIO = DefaultConfigureNRSTAsGPIO;

	struct InternalState * iss = malloc( sizeof( struct InternalState ) );
	iss->statetag = 0;
	iss->currentstateval = 0;

	((struct ProgrammerStructBase*)dev)->internal = iss;
	return 0;
}




void TestFunction(void * dev )
{
	uint32_t rv;
	int r;
	MCF.WriteReg32( dev, DMCONTROL, 0x80000001 ); // Make the debug module work properly.
	MCF.WriteReg32( dev, DMCONTROL, 0x80000001 ); // Initiate a halt request.
	MCF.WriteReg32( dev, DMCONTROL, 0x00000001 ); // Clear Halt Request.

	r = MCF.WriteWord( dev, 0x20000100, 0xdeadbeef );
	r = MCF.WriteWord( dev, 0x20000104, 0xcafed0de );
	r = MCF.WriteWord( dev, 0x20000108, 0x12345678 );
	r = MCF.WriteWord( dev, 0x20000108, 0x00b00d00 );
	r = MCF.WriteWord( dev, 0x20000104, 0x33334444 );

	r = MCF.ReadWord( dev, 0x20000100, &rv );
	printf( "**>>> %d %08x\n", r, rv );
	r = MCF.ReadWord( dev, 0x20000104, &rv );
	printf( "**>>> %d %08x\n", r, rv );
	r = MCF.ReadWord( dev, 0x20000108, &rv );
	printf( "**>>> %d %08x\n", r, rv );


	r = MCF.ReadWord( dev, 0x00000300, &rv );
	printf( "F %d %08x\n", r, rv );
	r = MCF.ReadWord( dev, 0x00000304, &rv );
	printf( "F %d %08x\n", r, rv );
	r = MCF.ReadWord( dev, 0x00000308, &rv );
	printf( "F %d %08x\n", r, rv );

	uint8_t buffer[256];
	int i;
	for( i = 0; i < 256; i++ ) buffer[i] = 0;
	MCF.WriteBinaryBlob( dev, 0x08000300, 256, buffer );
	MCF.ReadBinaryBlob( dev, 0x08000300, 256, buffer );
	for( i = 0; i < 256; i++ )
	{
		printf( "%02x ", buffer[i] );
		if( (i & 0xf) == 0xf ) printf( "\n" );
	}

	for( i = 0; i < 256; i++ ) buffer[i] = i;
	MCF.WriteBinaryBlob( dev, 0x08000300, 256, buffer );
	MCF.ReadBinaryBlob( dev, 0x08000300, 256, buffer );
	for( i = 0; i < 256; i++ )
	{
		printf( "%02x ", buffer[i] );
		if( (i & 0xf) == 0xf ) printf( "\n" );
	}
}

