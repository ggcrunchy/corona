//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_CommandBuffer.h"

#include "Core/Rtt_Allocator.h"
#include <stddef.h>

// STEVE CHANGE
#include "Rtt_Math.h"
// /STEVE CHANGE

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// STEVE CHANGE
CommandBufferlet::CommandBufferlet()
:	fBuffer( NULL ), 
	fOffset( NULL ), 
	fNumCommands( 0 ), 
	fBytesAllocated( 0 ), 
	fBytesUsed( 0 )
{
}

CommandBufferlet::~CommandBufferlet()
{
    if (fBuffer != NULL)
    {
        delete [] fBuffer;
    }
}

void
CommandBufferlet::ReadBytes( void * value, size_t size )
{
	Rtt_ASSERT( fOffset < fBuffer + fBytesAllocated );
	memcpy( value, fOffset, size );
	fOffset += size;
}

void
CommandBufferlet::WriteBytes( const void * value, size_t size )
{
	U32 bytesNeeded = fBytesUsed + size;
	if( bytesNeeded > fBytesAllocated )
	{
		U32 doubleSize = fBytesUsed ? 2 * fBytesUsed : 4;
		U32 newSize = Max( bytesNeeded, doubleSize );
		U8* newBuffer = new U8[newSize];

		memcpy( newBuffer, fBuffer, fBytesUsed );
		delete [] fBuffer;

		fBuffer = newBuffer;
		fBytesAllocated = newSize;
	}

	memcpy( fBuffer + fBytesUsed, value, size );
	fBytesUsed += size;
}

void
CommandBufferlet::Swap( CommandBufferlet & other )
{
	CommandBufferlet temp = other;

	other = *this;
	*this = temp;

	temp.fBuffer = NULL; // since destructor calls delete
}
// /STEVE CHANGE

CommandBuffer::CommandBuffer( Rtt_Allocator* allocator )
:	CommandBufferlet(), // <- STEVE CHANGE
	fAllocator( allocator )
// STEVE CHANGE
/*	fBuffer( NULL ), 
	fOffset( NULL ), 
	fNumCommands( 0 ), 
	fBytesAllocated( 0 ), 
	fBytesUsed( 0 )*/
// /STEVE CHANGE
{
}

CommandBuffer::~CommandBuffer()
{
// STEVE CHANGE
/*
    if (fBuffer != NULL)
    {
        delete [] fBuffer;
    }
*/
// /STEVE CHANGE
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
