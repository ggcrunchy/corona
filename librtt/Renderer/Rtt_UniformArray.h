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

#ifndef _Rtt_UniformArray_H__
#define _Rtt_UniformArray_H__

#include "Core/Rtt_Types.h"
#include "Core/Rtt_Real.h"
#include "Core/Rtt_SharedPtr.h"
#include "Renderer/Rtt_CPUResource.h"
#include "Renderer/Rtt_Uniform.h"

// ----------------------------------------------------------------------------

class Display;
class LuaUserdataProxy;

struct lua_State;
struct Rtt_Allocator;

namespace Rtt
{

// ----------------------------------------------------------------------------

class UniformArray : public CPUResource
{
	public:
		typedef CPUResource Super;
		typedef UniformArray Self;

	public:		
		UniformArray( Display &display, U32 count, Uniform::DataType dataType, bool compact );
		virtual ~UniformArray();

		virtual ResourceType GetType() const;
		virtual void Allocate();
		virtual void Deallocate();

	public:		
		void PushProxy( lua_State *L ) const;
		void DetachProxy();

		U32 GetMinDirtyOffset() const { return fMinDirtyOffset; }
		U32 GetDirtySize() const { return fMaxDirtyOffset ? (U32)(fMaxDirtyOffset - fMinDirtyOffset) : 0U; }
		U32 GetLifetimeMinDirtyOffset() const { return fLifetimeMinDirtyOffset; }
		U32 GetLifetimeDirtySize() const { return fLifetimeMaxDirtyOffset ? (U32)(fLifetimeMaxDirtyOffset - fLifetimeMinDirtyOffset) : 0U; }
		U32 GetSize() const { return fSize; }
		U32 GetSizeInVectors() const { return fSize / (4 * sizeof( Real )); }
		U32 GetTimestamp() const { return fTimestamp; }

		U8 *GetData() { return fData; }
		const U8 *GetData() const { return fData; }

		U32 Set( const U8 *bytes, U32 offset, U32 n );
		U32 Set( const Real *reals, U32 offset, U32 n );

		Uniform::DataType GetDataType() const { return fDataType; }
		bool GetIsCompact() const { return fCompact; }

		bool GetDirty() const { return fDirty; }
		void SetDirty( bool newValue );

		std::string Key() const;

	private:
		void ZeroPadExtrema();
		void ZeroRange( U32 from, U32 to );

	private:
		Display &fDisplay;
		U8 *fData;
		mutable LuaUserdataProxy *fProxy;
		Uniform::DataType fDataType;
		U32 fLifetimeMaxDirtyOffset;
		U32 fLifetimeMinDirtyOffset;
		U32 fMaxDirtyOffset;
		U32 fMinDirtyOffset;
		U32 fSize;
		U32 fTimestamp;
		bool fCompact;
		bool fDirty;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_UniformArray_H__
