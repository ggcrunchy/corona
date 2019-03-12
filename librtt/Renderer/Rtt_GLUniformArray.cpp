//////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2018 Corona Labs Inc.
// Contact: support@coronalabs.com
//
// This file is part of the Corona game engine.
//
// Commercial License Usage
// Licensees holding valid commercial Corona licenses may use this file in
// accordance with the commercial license agreement between you and 
// Corona Labs Inc. For licensing terms and conditions please contact
// support@coronalabs.com or visit https://coronalabs.com/com-license
//
// GNU General Public License Usage
// Alternatively, this file may be used under the terms of the GNU General
// Public license version 3. The license is as published by the Free Software
// Foundation and appearing in the file LICENSE.GPL3 included in the packaging
// of this file. Please review the following information to ensure the GNU 
// General Public License requirements will
// be met: https://www.gnu.org/licenses/gpl-3.0.html
//
// For overview and more information on licensing please refer to README.md
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_GLUniformArray.h"
#include "Renderer/Rtt_UniformArray.h"
#include "Core/Rtt_Allocator.h"
#include "Core/Rtt_Assert.h"
#include "Core/Rtt_Math.h"

#include <algorithm>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

static void
ResetOffsets( U32& maxOffset, U32& minOffset )
{
	maxOffset = 0U;
	minOffset = ~0U;
}

static void
ResetInterval( GLUniformArray::Interval& interval )
{
	ResetOffsets( interval.count, interval.start );
}

GLUniformArray::GLUniformArray()
:	fData( NULL ),
	fPayloads( NULL ),
	fDirtyIndex( 0U ),
	fHistoryIndex( 0U ),
	fSummaryIndex( 0U )
{
	ResetOffsets( fMaxOffset, fMinOffset );
}

template<size_t N> void
ClearIntervals( GLUniformArray::Interval (&intervals)[N] )
{
	for (U32 i = 0; i < N; ++i)
	{
		ResetInterval( intervals[i] );
	}
}

void
GLUniformArray::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kUniformArray == resource->GetType() );

	UniformArray* uniformArray = (UniformArray*)resource;

//	if (should simulate)
	{
		fData = (U8 *)Rtt_MALLOC( NULL, uniformArray->GetSize() );

		ClearIntervals( fRecentDirties );
		ClearIntervals( fHistory );
	}

//	else
	{
		// TODO: uniform blocks
	}

	Update( resource );
}

void
GLUniformArray::Update( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kUniformArray == resource->GetType() );

	UniformArray* uniformArray = (UniformArray*)resource;

//	if (should simulate)
	{
		U32 size = uniformArray->GetLifetimeDirtySize();

		if (size)
		{
			memcpy( fData, uniformArray->GetData() + uniformArray->GetLifetimeMinDirtyOffset(), size );
		}
	}

//	else
	{
		// TODO: uniform blocks
	}
}

void
GLUniformArray::Destroy()
{
	Rtt_DELETE( fData );

	for (Payload *cur = fPayloads, *next = NULL; cur; cur = next)
	{
		next = cur->next; // get before deletion

		Rtt_FREE( cur );
	}
}

void
GLUniformArray::Append( Payload* payload )
{
	Payload* prev = NULL;

	for (Payload* cur = fPayloads; cur; cur = cur->next)
	{
		prev = cur;
	}

	if (prev)
	{
		prev->next = payload;
	}

	else
	{
		fPayloads = payload;
	}
}

U8 *
GLUniformArray::Payload::GetData( Payload* payload )
{
	return reinterpret_cast<U8*>(&payload[1]);
}

void
GLUniformArray::AddPayload( U8 *data, U32 start, U32 count )
{
	Payload* payload = (Payload*)Rtt_MALLOC( NULL, sizeof( Payload ) + count );

	payload->next = NULL;
	payload->start = start;
	payload->count = count;

	memcpy( Payload::GetData( payload ), data + payload->start, payload->count );

	Append( payload );
}

template<size_t N> void
AddToIntervalRing( GLUniformArray::Interval (&intervals)[N], U8& index, U32 start, U32 count )
{
	intervals[index].start = start;
	intervals[index].count = count;

	index = (index + 1U) % N;
}

static void
UpdateOffsets( const GLUniformArray::Interval& ref, U32& maxOffset, U32& minOffset )
{
	maxOffset = Max( maxOffset, ref.start + ref.count );
	minOffset = Min( minOffset, ref.start );
}

void
GLUniformArray::Commit( Payload* payload )
{
	/* Load the payload into shared uniform memory */
	memcpy( fData + payload->start, Payload::GetData( payload ), payload->count );

	/* Make the exact range available for "recently written" lookups... */
	AddToIntervalRing( fRecentDirties, fDirtyIndex, payload->start, payload->count );

	/* ...and begin or refine a summary of the same... */
	UpdateOffsets( *payload, fMaxOffset, fMinOffset );

	++fSummaryIndex;

	/* ...eventually submitting it to the history */
	if (kSummarizeCount == fSummaryIndex)
	{
		fSummaryIndex = 0U;

		AddToIntervalRing( fHistory, fHistoryIndex, fMinOffset, fMaxOffset - fMinOffset );
		ResetOffsets( fMaxOffset, fMinOffset );
	}
}

void
GLUniformArray::ConsumePayload()
{
	Payload* front = fPayloads;

	Rtt_ASSERT( front );

	Commit( front );
	
	fPayloads = front->next;

	Rtt_FREE( front );
}

void
GLUniformArray::PrepareRelevantIntervals( U32 n, Interval intervals[] )
{
	/* Lower part of dirty entries ring... */
	U32 count1 = Min( (U32)fDirtyIndex, n );

	for (U32 i = 1; i <= count1; ++i)
	{
		intervals[i - 1] = fRecentDirties[fDirtyIndex - i];
	}

	/* ...and upper part */
	U32 count2 = n - count1;

	for (U32 i = 1; i <= count2; ++i)
	{
		intervals[count1 + i - 1] = fRecentDirties[kDirtyCount - i];
	}

	/* Order them */
	ResetInterval( intervals[n] );

	std::sort(intervals, intervals + n + 1,
		[](const Interval& i1, const Interval& i2)
		{
			return i1.start < i2.start;
		}
	);
}

U32
GLUniformArray::GatherWithMerge( Interval out[], U32 nintervals, const Interval intervals[])
{
	U32 n = 0, next;

	for (U32 i = 0; i < nintervals; i = next)
	{
		Interval& cur = out[n];

		cur = intervals[i];
		next = i + 1U;

		while (
			next < nintervals &&
			cur.start + cur.count + kBridgeableGap >= intervals[next].start /* overlap? */
		)
		{
			cur.count = (intervals[next].start - cur.start) + intervals[next].count; /* merge */

			++next;
		}
		
		++n; /* commit interval */
	}

	return n;
}

U32
GLUniformArray::GetRecentIntervals( U32 lag, Interval out[] )
{
	Interval intervals[kDirtyCount + 1];

	PrepareRelevantIntervals( lag, intervals );

	return GatherWithMerge( out, lag, intervals );
}

bool
GLUniformArray::GetHistoryInterval( U32 lag, Interval& out )
{
	Rtt_ASSERT( lag > 0 );

	bool inHistory = lag < kHistoryCount * kSummarizeCount + fSummaryIndex;

	if (inHistory)
	{
		U32 maxOffset = fMaxOffset;
		U32 minOffset = fMinOffset;

		for (U32 i = 0; i < kHistoryCount; ++i)
		{
			UpdateOffsets( fHistory[i], maxOffset, minOffset );
		}

		out.start = minOffset;
		out.count = maxOffset - minOffset;
	}

	return inHistory;
}

U32
GLUniformArray::GetIntervals( U32 lag, Interval out[], U32 size )
{
	if (lag <= GLUniformArray::kDirtyCount)
	{
		return GetRecentIntervals( lag, out );
	}

	else if (!GetHistoryInterval( lag, out[0] ))
	{
		out[0].start = 0U;
		out[0].count = size;
	}

	return 1U;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

