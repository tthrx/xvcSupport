//-----------------------------------------------------------------------------
// Title      : JTAG Support
//-----------------------------------------------------------------------------
// File       : xvcDrvAxisFifo.cc
// Author     : Till Straumann <strauman@slac.stanford.edu>
// Company    : SLAC National Accelerator Laboratory
// Created    : 2017-12-05
// Last update: 2017-12-05
// Platform   : 
// Standard   : VHDL'93/02
//-----------------------------------------------------------------------------
// Description: 
//-----------------------------------------------------------------------------
// This file is part of 'SLAC Firmware Standard Library'.
// It is subject to the license terms in the LICENSE.txt file found in the 
// top-level directory of this distribution and at: 
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
// No part of 'SLAC Firmware Standard Library', including this file, 
// may be copied, modified, propagated, or distributed except according to 
// the terms contained in the LICENSE.txt file.
//-----------------------------------------------------------------------------

#include <xvcDrvAxisTmem.h>
#include <unistd.h>
#include <stdlib.h>
#include <toscaApi.h>
#include <fcntl.h>

JtagDriverTmemFifo::JtagDriverTmemFifo(int argc, char *const argv[], const char *devnam)
: JtagDriverAxisToJtag( argc, argv ),
  toscaSpace_( TOSCA_USER2 ),
  toscaBase_ ( 0x200000    ),
  irqFd_     ( -1          )
{
uint32_t      csrVal;
unsigned long maxBytes;
unsigned long maxWords;
int           opt;
const char   *basp;
char         *endp;
const char   *irqfn = 0;

	toscaSpace_ = toscaStrToAddrSpace(devnam, &basp);
	toscaBase_  = strtoul(basp, &endp, 0);
	if ( ! toscaSpace_ || (endp == basp)  ) {
		fprintf(stderr, "Invalid <target>: must be '<tosca_addr_space>:<numerical_base_address>'\n");
		throw std::runtime_error("Invalid <target>");
	}

	while ( (opt = getopt(argc, argv, "i:")) > 0 ) {
		switch ( opt ) {
			case 'i': irqfn = optarg; break;
			default:
				fprintf( stderr,"Unknown driver option -%c\n", opt );
				throw std::runtime_error("Unknown driver option");
		}
	}

	reset();

	if ( MAGIC != i32( FIFO_MAGIC_IDX ) ) {
		fprintf( stderr, "No magic firmware ID found; please verify address-space and base-address\n" );
		throw std::runtime_error("TMEM Device not found");
	}

	csrVal  = i32( FIFO_CSR_IDX );

	if ( ((csrVal & FIFO_CSR_VERSM) >> FIFO_CSR_VERSS) != SUPPORTED_VERS ) {
		fprintf( stderr, "Firmware interface version not supported by this driver\n");
		throw std::runtime_error("TMEM Device wrong FW version");
	}

	wrdSiz_ = 4;

	maxWords = (((csrVal & FIFO_CSR_MAXWM) >> FIFO_CSR_MAXWS) << 10 ) / wrdSiz_;

	// one header word; two vectors must fit
	maxBytes = (maxWords - 1) * wrdSiz_;

	maxVec_ = maxBytes/2;

	if ( irqfn && ( (irqFd_ = open(irqfn, O_RDWR)) < 0 ) ) {
		perror("WARNING: Interrupt descriptor not found -- using polled mode");
	}
}

JtagDriverTmemFifo::~JtagDriverTmemFifo()
{
	if ( irqFd_ >= 0 ) {
		close( irqFd_ );
	}
}

void
JtagDriverTmemFifo::o32(unsigned idx, uint32_t v)
{
	if ( debug_ > 2 ) {
		fprintf(stderr, "r[%d]:=0x%08x\n", idx, v);
	}
	toscaWrite( toscaSpace_, toscaBase_ + (idx << 2), v );
}

uint32_t
JtagDriverTmemFifo::i32(unsigned idx)
{
	uint32_t v = toscaRead( toscaSpace_, toscaBase_ + (idx << 2) );
	if ( debug_ > 2 ) {
		fprintf(stderr, "r[%d]=>0x%08x\n", idx, v);
	}
	return v;
}

uint32_t
JtagDriverTmemFifo::wait()
{
uint32_t evs = 0;
	if ( irqFd_ >= 0 ) {
		evs = 1;
		if ( sizeof(evs) != write( irqFd_, &evs, sizeof(evs) ) ) {
			throw SysErr("Unable to write to IRQ descriptor");
		}
		if ( sizeof(evs) != read( irqFd_, &evs, sizeof(evs) ) ) {
			throw SysErr("Unable to read from IRQ descriptor");
		}
	} // else busy wait
	return evs;
}

void
JtagDriverTmemFifo::reset()
{
int set = 0;

	o32( FIFO_CSR_IDX, FIFO_CSR_RST );
	o32( FIFO_CSR_IDX, 0              );
	if ( irqFd_ >= 0 ) {
		o32( FIFO_CSR_IDX, FIFO_CSR_IENI );
	}
}

void
JtagDriverTmemFifo::init()
{
	reset();
	JtagDriverAxisToJtag::init();
	// have now the target word size -- verify:
	if ( getWordSize() != wrdSiz_ ) {
		throw std::runtime_error("ERROR: firmware misconfigured -- FIFO word size /= JTAG stream word size");
	}
}

unsigned long
JtagDriverTmemFifo::getMaxVectorSize()
{
	return maxVec_;
}

int
JtagDriverTmemFifo::xfer( uint8_t *txb, unsigned txBytes, uint8_t *hdbuf, unsigned hsize, uint8_t *rxb, unsigned size )
{
unsigned txWords   = (txBytes + 3)/4;
uint32_t lastBytes = txBytes - 4*(txWords - 1);
unsigned i;
unsigned got, min, minw, rem;
uint32_t w;
uint32_t csr;

	if ( hsize % 4 != 0 ) {
		throw std::runtime_error("AXIS2TMEM FIFO only supports word-lengths that are a multiple of 4");
	}

	if ( lastBytes ) {
		txWords--;
	}
	for ( i=0; i<txWords; i++ ) {
		memcpy( &w, &txb[4*i], 4 );
		o32( FIFO_DAT_IDX, __builtin_bswap32( w ) );
	}
	if ( lastBytes ) {
		w = 0;
		memcpy( &w, &txb[4*i], lastBytes );
		o32( FIFO_DAT_IDX, __builtin_bswap32( w ) );
	}

	o32( FIFO_CSR_IDX, i32( FIFO_CSR_IDX ) | FIFO_CSR_EOFO );

	while ( ( (csr = i32( FIFO_CSR_IDX )) & FIFO_CSR_EMPI ) ) {
		wait();
	}

	got = ( (csr >> FIFO_CSR_NWRDS) & FIFO_CSR_NWRDM ) * wrdSiz_;

	if ( 0 == got ) {
		throw ProtoErr("Didn't receive enough data for header");
	}

	for ( i=0; i<hsize; i+= 4 ) {
		w = i32( FIFO_DAT_IDX );
		if ( got < 4 ) {
			throw ProtoErr("Didn't receive enough data for header");
		}
		w = __builtin_bswap32( w );
		memcpy( hdbuf + i, &w, 4 );
		got   -= 4;
	}
	min  = got;

	if ( size < min ) {
		min = size;
	}

	minw = min/4;

	for ( i=0; i<4*minw; i+=4 ) {
		w = i32( FIFO_DAT_IDX );
		w = __builtin_bswap32( w );
		memcpy( &rxb[i], &w, 4 );
	}

	if ( (rem = (min - i)) ) {
		w = i32( FIFO_DAT_IDX );
		w = __builtin_bswap32( w );
		memcpy( &rxb[i], &w, rem );
		i += 4;
	}

	/* Discard excess */
	while ( i < got ) {
		i32( FIFO_DAT_IDX );
		i += 4;
	}

	if ( drEn_ && 0 == ((++drop_) & 0xff) ) {
		fprintf(stderr, "Drop\n");
		fflush(stderr);
		throw TimeoutErr();
	}

	return min;
}

void
JtagDriverTmemFifo::usage()
{
	printf("  Axi Stream <-> TMEM Fifo Driver options: [-i]\n");
	printf("  -t <aspace>:<base_address>, e.g., -t USER2:0x200000\n");
	printf("  -i <irq_file_name>        , e.g., -i /dev/toscauserevent1.13 (defaults to polled mode)\n");
}

static DriverRegistrar<JtagDriverTmemFifo> r("tmem");
