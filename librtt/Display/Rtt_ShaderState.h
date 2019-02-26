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

#ifndef _Rtt_ShaderState_H__
#define _Rtt_ShaderState_H__

#include "Core/Rtt_SharedPtr.h"
#include "Core/Rtt_WeakPtr.h"
#include "Display/Rtt_ShaderResource.h"
#include "Display/Rtt_ShaderTypes.h"

#include "Rtt_LuaUserdataProxy.h"

#include <string>

// ----------------------------------------------------------------------------

struct lua_State;

namespace Rtt
{

class LuaUserdataProxy;
class UniformArray;

// ----------------------------------------------------------------------------

class ShaderState
{
	public:
		typedef ShaderState Self;

	public:
		ShaderState( Rtt_Allocator *allocator, const SharedPtr< ShaderResource >& resource );
		ShaderState();
		~ShaderState();

	public:
		void PushProxy( lua_State *L ) const;
		void DetachProxy();

		UniformArray *GetUniformArray() const;

		void SetShaderCategory( ShaderTypes::Category category ) { fCategory = category; }
		ShaderTypes::Category GetShaderCategory() const { return fCategory; }
		
		void SetShaderName( const std::string &name ) { fName = name; }
		std::string GetShaderName() const { return fName; }

	private:
		WeakPtr< ShaderResource > fResource;
		mutable LuaUserdataProxy *fProxy;
		ShaderTypes::Category fCategory;
		std::string fName;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_ShaderState_H__
