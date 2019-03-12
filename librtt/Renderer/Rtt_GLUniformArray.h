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

#ifndef _Rtt_GLUniformArray_H__
#define _Rtt_GLUniformArray_H__

#include "Renderer/Rtt_GL.h"
#include "Renderer/Rtt_GPUResource.h"
#include "Renderer/Rtt_UniformArray.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class GLUniformArray : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef GLUniformArray Self;

		enum : U32 {
			kDirtyCount = 8U, // Remember the (min, max) range for this many recent writes... (More accurate)
			kSummarizeCount = 16U, // ...every time we do this many steps, commit the (min, max) to the history...
			kHistoryCount = 8U, // ...keep this many history entries around (Less accurate)
			kBridgeableGap = 4U * sizeof( Real ) * 5U // too close to warrant separate uniforms upload?
		};

	public:
		GLUniformArray();

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();

	public:
		struct Interval {
			U32 start;
			U32 count;
		};

		void AddPayload( U8 *data, U32 start, U32 count );
		void ConsumePayload();
		U32 GetIntervals( U32 lag, Interval out[], U32 size );

		U8 *GetData() const { return fData; }

	private:		
		struct Payload : public Interval
		{
			Payload* next;

			static U8 *GetData( Payload* payload );
		};

		void Append( Payload* payload );
		void Commit( Payload* payload );
		void PrepareRelevantIntervals( U32 n, Interval intervals[] );
		U32 GatherWithMerge( Interval out[], U32 nintervals, const Interval intervals[]);
		U32 GetRecentIntervals( U32 lag, Interval out[] );
		bool GetHistoryInterval( U32 lag, Interval& out );

	private:
		U8 *fData;
		Payload *fPayloads;
		Interval fRecentDirties[kDirtyCount];
		Interval fHistory[kHistoryCount];
		U32 fMaxOffset;
		U32 fMinOffset;
		U8 fDirtyIndex;
		U8 fHistoryIndex;
		U8 fSummaryIndex;

		// or

	//	GLuint fBlockBinding;
	//	GLuint fBlockIndex;
		// probably distinct enough to call for own class
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_GLUniformArray_H__
