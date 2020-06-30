//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Rtt_VertexCache.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

VertexCache::VertexCache( Rtt_Allocator* pAllocator )
:	fVertices( pAllocator ),
	fTexVertices( pAllocator ),
	fCounts( pAllocator ),
// STEVE CHANGE
	fExtraFloatArrays( pAllocator ),
	fExtraIndexArrays( pAllocator )
// /STEVE CHANGE
{
}

// STEVE CHANGE
VertexCache::~VertexCache()
{
	for (S32 i = 0; i < fExtraFloatArrays.Length(); ++i)
	{
		Rtt_DELETE( fExtraFloatArrays[i] );
	}

	for (S32 i = 0; i < fExtraIndexArrays.Length(); ++i)
	{
		Rtt_DELETE( fExtraIndexArrays[i] );
	}
}
// /STEVE CHANGE

void
VertexCache::Invalidate()
{
	fVertices.Empty();
	fTexVertices.Empty();
	fCounts.Empty();
}

// STEVE CHANGE
bool
VertexCache::AddExtraFloatArray()
{
	fExtraFloatArrays.Append( Rtt_NEW( fVertices.Allocator(), ArrayFloat( fVertices.Allocator() ) ) );

	return true;
}

bool
VertexCache::AddExtraIndexArray()
{
	fExtraIndexArrays.Append( Rtt_NEW( fVertices.Allocator(), ArrayIndex( fVertices.Allocator() ) ) );

	return true;
}

template<typename AT> bool
EnsureExists( Rtt_Allocator * allocator, LightPtrArray< AT > & ptrArray, U32 index, bool addIfAbsent )
{
	bool slotExists = index < ptrArray.Length();

	if (slotExists && ptrArray[index])
	{
		return true;
	}

	else if (!addIfAbsent || index >= 20U) // arbitrary limit, but larger than necessary?
	{
		return false;
	}

	if (!slotExists)
	{
		U32 extra = index - ptrArray.Length() + 1;

		for (U32 i = 0; i < extra; ++i)
		{
			ptrArray.Append( NULL );
		}
	}

	ptrArray[index] = Rtt_NEW( allocator, AT( allocator ) );

	return true;
}

ArrayFloat *
VertexCache::ExtraFloatArray( U32 index, bool addIfAbsent )
{
	return EnsureExists( fVertices.Allocator(), fExtraFloatArrays, index, addIfAbsent ) ? fExtraFloatArrays[index] : NULL;
}

const ArrayFloat *
VertexCache::ExtraFloatArray( U32 index ) const
{
	return (index >= 0 && index < fExtraFloatArrays.Length()) ? fExtraFloatArrays[index] : NULL;
}

ArrayIndex *
VertexCache::ExtraIndexArray( U32 index, bool addIfAbsent )
{
	return EnsureExists( fVertices.Allocator(), fExtraIndexArrays, index, addIfAbsent ) ? fExtraIndexArrays[index] : NULL;
}

const ArrayIndex *
VertexCache::ExtraIndexArray( U32 index ) const
{
	return (index >= 0 && index < fExtraIndexArrays.Length()) ? fExtraIndexArrays[index] : NULL;
}
// /STEVE CHANGE

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

