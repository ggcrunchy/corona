//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_GLFrameBufferObject_H__
#define _Rtt_GLFrameBufferObject_H__

#include "Renderer/Rtt_GL.h"
#include "Renderer/Rtt_GPUResource.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class GLFrameBufferObject : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef GLFrameBufferObject Self;

	public:
		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();
		virtual void Bind( bool asDrawBuffer ); // <- STEVE CHANGE

		virtual GLuint GetName();
		virtual GLuint GetTextureName();

	// STEVE CHANGE
	public:
		static bool HasFramebufferBlit();
		static void Blit( int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2, GLbitfield mask, GLenum filter );
	// /STEVE CHANGE
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_GLFrameBufferObject_H__
