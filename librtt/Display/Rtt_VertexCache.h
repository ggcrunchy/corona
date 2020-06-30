//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_VertexCache_H__
#define _Rtt_VertexCache_H__

#include "Display/Rtt_DisplayTypes.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class VertexCache
{
	public:
		VertexCache( Rtt_Allocator* pAllocator );
		// STEVE CHANGE
		~VertexCache();
		// /STEVE CHANGE

	public:
		void Invalidate();
		bool IsValid() const { return fVertices.Length() > 0; }

		// STEVE CHANGE
		bool AddExtraFloatArray();
		U32 ExtraFloatArrayCount() const { return fExtraFloatArrays.Length(); }

		bool AddExtraIndexArray();
		U32 ExtraIndexArrayCount() const { return fExtraIndexArrays.Length(); }
		// /STEVE CHANGE
	public:
		ArrayVertex2& Vertices() { return fVertices; }
		ArrayVertex2& TexVertices() { return fTexVertices; }
		ArrayS32& Counts() { return fCounts; }

		// STEVE CHANGE
		ArrayFloat * ExtraFloatArray( U32 index, bool addIfAbsent = false );
		const ArrayFloat * ExtraFloatArray( U32 index ) const;

		ArrayIndex * ExtraIndexArray( U32 index, bool addIfAbsent = false );
		const ArrayIndex * ExtraIndexArray( U32 index ) const;
		// /STEVE CHANGE

		const ArrayVertex2& Vertices() const { return fVertices; }
		const ArrayVertex2& TexVertices() const { return fTexVertices; }
		const ArrayS32& Counts() const { return fCounts; }

	private:
		ArrayVertex2 fVertices;
		ArrayVertex2 fTexVertices;
		ArrayS32 fCounts;
		// STEVE CHANGE
		LightPtrArray< ArrayFloat > fExtraFloatArrays;
		LightPtrArray< ArrayIndex > fExtraIndexArrays;
		// /STEVE CHANGE
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VertexCache_H__
