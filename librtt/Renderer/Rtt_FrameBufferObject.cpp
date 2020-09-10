//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_FrameBufferObject.h"
// STEVE CHANGE
#include "Renderer/Rtt_Texture.h"
// /STEVE CHANGE

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

FrameBufferObject::FrameBufferObject( Rtt_Allocator* allocator, Texture* texture )
:	CPUResource( allocator ),
	fTexture( texture )
{
	// STEVE CHANGE
	fTexture->SetTarget( true );
	// /STEVE CHANGE
}

CPUResource::ResourceType FrameBufferObject::GetType() const
{
	return CPUResource::kFrameBufferObject;
}

Texture* FrameBufferObject::GetTexture() const
{
	return fTexture;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
