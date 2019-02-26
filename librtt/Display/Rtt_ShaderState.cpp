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

#include "Display/Rtt_ShaderState.h"
#include "Display/Rtt_ShaderStateAdapter.h"
#include "Renderer/Rtt_UniformArray.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

ShaderState::ShaderState( Rtt_Allocator *allocator, const SharedPtr< ShaderResource >& resource )
:	fResource( resource ),
	fCategory( ShaderTypes::kCategoryDefault ),
	fProxy( NULL )
{
	ShaderResource &res = *resource;

	res.SetShaderState( this );
}

ShaderState::ShaderState()
:	fCategory( ShaderTypes::kCategoryDefault ),
	fProxy( NULL )
{
}

ShaderState::~ShaderState()
{
	if ( fProxy )
	{
	//	GetObserver()->QueueRelease( fProxy ); // Release native ref to Lua-side proxy
		fProxy->DetachUserdata(); // Notify proxy that object is invalid
	}
}

void
ShaderState::PushProxy( lua_State *L ) const
{
	if ( ! fProxy )
	{
		fProxy = LuaUserdataProxy::New( L, const_cast< Self * >( this ) );
		fProxy->SetAdapter( & ShaderStateAdapter::Constant() );
	}

	fProxy->Push( L );
}

void
ShaderState::DetachProxy()
{
	fProxy = NULL;
}

UniformArray *
ShaderState::GetUniformArray() const
{
	if (fResource.NotNull())
	{
		SharedPtr<ShaderResource> resource( fResource );

		return resource->GetUniformArray();
	}

	else
	{
		return NULL;
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

