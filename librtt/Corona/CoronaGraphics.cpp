//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "CoronaGraphics.h"
#include "CoronaLua.h"

#include "Rtt_LuaContext.h"

#include "Display/Rtt_TextureResource.h"
#include "Core/Rtt_SharedPtr.h"
#include "Display/Rtt_TextureFactory.h"
#include "Rtt_Runtime.h"
#include "Display/Rtt_Display.h"

#include "Display/Rtt_TextureResourceExternalAdapter.h"


CORONA_API
int CoronaExternalPushTexture( lua_State *L, const CoronaExternalTextureCallbacks *callbacks, void* context)
{
	if ( callbacks->size != sizeof(CoronaExternalTextureCallbacks) )
	{
		CoronaLuaError(L, "TextureResourceExternal - invalid binary version for callback structure; size value isn't valid");
		return 0;
	}
	
	if (callbacks == NULL || callbacks->onRequestBitmap == NULL || callbacks->getWidth == NULL || callbacks->getHeight == NULL )
	{
		CoronaLuaError(L, "TextureResourceExternal - bitmap, width and height callbacks are required");
		return 0;
	}
	
	static unsigned int sNextExternalTextureId = 1;
	char filename[30];
	snprintf(filename, 30, "corona://exTex_%u", sNextExternalTextureId++);
	
	Rtt::TextureFactory& factory = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetTextureFactory();
	
	Rtt::SharedPtr< Rtt::TextureResource > ret = factory.FindOrCreateExternal(filename, callbacks, context);
	factory.Retain(ret);
	
	if (ret.NotNull())
	{
		ret->PushProxy( L );
		return 1;
	}
	else
	{
		return 0;
	}
}


CORONA_API
void* CoronaExternalGetUserData( lua_State *L, int index )
{
	return Rtt::TextureResourceExternalAdapter::GetUserData( L, index );
}


CORONA_API
int CoronaExternalFormatBPP(CoronaExternalBitmapFormat format)
{
	switch (format)
	{
		case kExternalBitmapFormat_Mask:
			return 1;
		case kExternalBitmapFormat_RGB:
			return 3;
		default:
			return 4;
	}
}

CORONA_API
int CoronaShaderRegisterAttributes( CoronaGraphicsToken * token, const CoronaShaderAttribute * attributes, unsigned int attributeCount )
{
	Rtt_ASSERT_NOT_IMPLEMENTED();

	// Add attributes for later lookup, using the token

	return 0;
}

CORONA_API
int CoronaShaderRegisterPolicy( lua_State * L, const char * name, const CoronaShaderCallbacks * callbacks, void * userData )
{
	// Add callbacks for later lookup using the name
	// This is meant for `graphics.defineEffect()`, via say a `policy` field (better name?)

	return 0;
}

CORONA_API
CoronaRendererBackend CoronaRendererGetBackend()
{
	// TODO: Vulkan, etc.

	#ifdef Rtt_OPENGLES
		return kBackend_OpenGLES;
	#else
		return kBackend_OpenGL;
	#endif
}

// Many of these will have operations:
//		{ count; flags; functions[MAX]; }
// Initially:
//			count = flags = 0
// Registration:
//		if count < MAX
//			index = count++
//			token = Token(index), either index itself or 1 << index
//			return true
//		else return false

CORONA_API
int CoronaRendererRegisterBeginFrameOp( CoronaGraphicsToken * token, void (*onBeginFrame)(void *), void * userData )
{
	// At the beginning of the frame, any begin-frame op whose flag is set gets called
	// These are meant to clean up, on demand, something we did last frame
	// All flags are cleared afterward

	return 0;
}

CORONA_API
int CoronaRendererScheduleForNextFrame( const CoronaGraphicsToken * token, CoronaRenderBeginFrame action )
{
	// Enable or disable (schedule / cancel) a begin-frame op
	// We cancel if we never ended up doing anything to need the op
	// However, we might do the action multiple times per frame, so we might need a "successful" cancel too
	// Else a later cancel could undo a previous need for the begin-frame op
	// This will depend on whether our state merely needs to not dangle, or whether some resources need to be cleaned up

	return 0;
}

CORONA_API
int CoronaRendererRegisterClearOp( CoronaGraphicsToken * token, void (*onClear)(void *), void * userData )
{
	// When calling `Renderer.Clear()`, every clear op whose flag is set gets called

	return 0;
}

CORONA_API
int CoronaRendererEnableClear( const CoronaGraphicsToken * token, int enable )
{
	// Enable or disable clear op according to flag

	return 0;
}

CORONA_API
int CoronaRendererRegisterStateOp( CoronaGraphicsToken * token, void (*onState)(void *), void * userData )
{
	// If any state op flag is set, `Renderer.Insert()` considers the state dirty
	// Furthermore, every state op whose flag is set gets called
	// All flags are cleared afterward

	return 0;
}

CORONA_API
int CoronaRendererSetOperationStateDirty( const CoronaGraphicsToken * token )
{
	// Set the state op's flag, making it dirty

	return 0;
}

CORONA_API
int CoronaRendererRegisterCommand( CoronaGraphicsToken * token, void (*read)(uint8_t * read), int (*write)(uint8_t * bytes, const void * data, uint32_t size) )
{
	// New command handlers are added
	// Handlers for all commands are called, in order
	// The list is emptied afterward

	return 0;
}

CORONA_API
int CoronaRendererIssueCommand( const CoronaGraphicsToken * token, void * data, uint32_t size )
{
	// Sequence a registered command at the end of the list

	return 0;
}

CORONA_API
int CoronaRendererSetFrustum( const float * viewMatrix, const float * projectionMatrix )
{
	return 0;
}

// ----------------------------------------------------------------------------

