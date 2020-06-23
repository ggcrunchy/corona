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
// STEVE CHANGE
#include "Display/Rtt_ShaderFactory.h"
#include "Renderer/Rtt_CommandBuffer.h"
#include "Renderer/Rtt_Renderer.h"
// /STEVE CHANGE

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
// STEVE CHANGE
CORONA_API
int CoronaShaderRegisterAttributes( lua_State * L, CoronaGraphicsToken * token, const CoronaShaderAttribute * attributes, unsigned int attributeCount )
{
	Rtt_ASSERT_NOT_IMPLEMENTED();

	// Add attributes for later lookup, using the token

	return 0;
}

CORONA_API
int CoronaShaderRegisterCustomization( lua_State * L, const char * name, const CoronaShaderCallbacks * callbacks )
{
	return Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetShaderFactory().RegisterCustomization( name, *callbacks );
}

CORONA_API
CoronaRendererBackend CoronaRendererGetBackend( lua_State * )
{
	// TODO: Vulkan, etc.

	#ifdef Rtt_OPENGLES
		return kBackend_OpenGLES;
	#else
		return kBackend_OpenGL;
	#endif
}

enum TokenType : unsigned char { kTokenType_None, kTokenType_Attribute, kTokenType_BeginFrameOp, kTokenType_ClearOp, kTokenType_Command, kTokenType_StateOp, kTokenType_Renderer = 0xFF };

void CoronaGraphicsTokenWrite( CoronaGraphicsToken * tokens, unsigned char type, const void * data, unsigned int size )
{
	tokens->bytes[0] = type;

	if (size)
	{
		memcpy( tokens->bytes + 1, data, size );
	}
}

void CoronaGraphicsTokenRead( void * buffer, const CoronaGraphicsToken * tokens, unsigned int size )
{
	if (size)
	{
		memcpy( buffer, tokens->bytes + 1, size );
	}
}

U8 CoronaGraphicsGetTokenType( const CoronaGraphicsToken * tokens )
{
	return tokens->bytes[0];
}

static U32 sIndex = ~0U;

static const size_t MixedSize = sizeof( const void * ) + sizeof( int );

static_assert( MixedSize <= sizeof( CoronaGraphicsToken ), "Mixed size too large" );

void CoronaGraphicsEncodeAsTokens ( CoronaGraphicsToken tokens[], unsigned char type, const void * data )
{
	if (kTokenType_Renderer == type && data)
	{
		unsigned char mixed[MixedSize];

		++sIndex; // invalidate last use

		memcpy( mixed, &data, sizeof( data ) );
		memcpy( mixed + sizeof( data ), &sIndex, sizeof( U32 ) );

		CoronaGraphicsTokenWrite( tokens, type, mixed, MixedSize );
	}

	else
	{
		CoronaGraphicsTokenWrite( tokens, kTokenType_None, NULL, 0U );
	}
}

template<typename T> int
SetFlagStyleToken( CoronaGraphicsToken * token, TokenType type, U16 index )
{
	T flag = 1U << (index - 1U);

	CoronaGraphicsTokenWrite( token, type, &flag, sizeof( T ) );

	return 1;
}

CORONA_API
int CoronaRendererRegisterBeginFrameOp( lua_State * L, CoronaGraphicsToken * token, CoronaRendererOp onBeginFrame, void * userData )
{
	U16 index = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddBeginFrameOp( onBeginFrame, userData );

	if (index)
	{
		return SetFlagStyleToken< U32 >( token, kTokenType_BeginFrameOp, index );
	}

	return 0;
}

template<typename T> T
ExtractFromToken( const CoronaGraphicsToken * token )
{
	T result;

	CoronaGraphicsTokenRead( &result, token, sizeof( T ) );

	return result;
}

static Rtt::Renderer *
GetRenderer( const CoronaGraphicsToken * tokens )
{
	if (kTokenType_Renderer == CoronaGraphicsGetTokenType( tokens ))
	{
		unsigned char mixed[MixedSize];

		CoronaGraphicsTokenRead( mixed, tokens, MixedSize );

		void * data;
		int index;

		memcpy( &data, mixed, sizeof( data ) );
		memcpy( &index, mixed + sizeof( data ), sizeof( U32 ) );

		if (index == sIndex) // still the same "session"?
		{
			return static_cast< Rtt::Renderer *>( data );
		}
	}

	return NULL;
}

CORONA_API
int CoronaRendererScheduleForNextFrame( const CoronaGraphicsToken * rendererToken, const CoronaGraphicsToken * token, CoronaRenderBeginFrame action )
{
	if (kTokenType_BeginFrameOp == CoronaGraphicsGetTokenType( token ))
	{
		Rtt::Renderer * renderer = GetRenderer( rendererToken );

		if (renderer)
		{
			U32 flag = ExtractFromToken< U32 >( token );

			switch (action)
			{
			case kBeginFrame_Schedule:
				renderer->SetBeginFrameFlags( renderer->GetBeginFrameFlags() | flag );

				break;
			case kBeginFrame_Cancel:
				flag &= ~renderer->GetDoNotCancelFlags();

				renderer->SetBeginFrameFlags( renderer->GetBeginFrameFlags() & ~flag );

				break;
			case kBeginFrame_Establish:
				renderer->SetDoNotCancelFlags( renderer->GetDoNotCancelFlags() | flag );

				break;
			}

			return 1;
		}
	}

	return 0;
}

CORONA_API
int CoronaRendererRegisterClearOp( lua_State * L, CoronaGraphicsToken * token, CoronaRendererOp onClear, void * userData )
{
	U16 index = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddClearOp( onClear, userData );

	if (index)
	{
		return SetFlagStyleToken< U32 >( token, kTokenType_ClearOp, index );
	}

	return 0;
}

CORONA_API
int CoronaRendererEnableClear( const CoronaGraphicsToken * rendererToken, const CoronaGraphicsToken * token, int enable )
{
	if (kTokenType_ClearOp == CoronaGraphicsGetTokenType( token ))
	{
		Rtt::Renderer * renderer = GetRenderer( rendererToken );

		if (renderer)
		{
			U32 flag = ExtractFromToken< U32 >( token ), clearFlags = renderer->GetClearFlags();

			renderer->SetClearFlags( enable ? (clearFlags | flag) : (clearFlags & ~flag) );

			return 1;
		}
	}

	return 0;
}

CORONA_API
int CoronaRendererRegisterStateOp( lua_State * L, CoronaGraphicsToken * token, CoronaRendererOp onState, void * userData )
{
	U16 index = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddStateOp( onState, userData );

	if (index)
	{
		return SetFlagStyleToken< U64 >( token, kTokenType_StateOp, index );
	}

	return 0;
}

CORONA_API
int CoronaRendererSetOperationStateDirty( const CoronaGraphicsToken * rendererToken, const CoronaGraphicsToken * token )
{
	if (kTokenType_StateOp == CoronaGraphicsGetTokenType( token ))
	{
		Rtt::Renderer * renderer = GetRenderer( rendererToken );

		if (renderer)
		{
			renderer->SetStateFlags( renderer->GetStateFlags() | ExtractFromToken< U64 >( token ) );

			return 1;
		}
	}

	return 0;
}

CORONA_API
int CoronaRendererRegisterCommand( lua_State * L, CoronaGraphicsToken * token, const CoronaCommand * command )
{
	U16 index = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddCustomCommand( *command );

	if (index)
	{
		--index;

		CoronaGraphicsTokenWrite( token, kTokenType_Command, &index, sizeof( U16 ) );

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaRendererIssueCommand( const CoronaGraphicsToken * rendererToken, const CoronaGraphicsToken * token, void * data, unsigned int size )
{
	if (kTokenType_Command == CoronaGraphicsGetTokenType( token ))
	{
		Rtt::Renderer * renderer = GetRenderer( rendererToken );
		
		if (renderer)
		{
			return renderer->IssueCustomCommand( ExtractFromToken< U16 >( token ), data, size );
		}
	}

	return 0;
}

CORONA_API
int CoronaRendererSetFrustum( const CoronaGraphicsToken * rendererToken, const float * viewMatrix, const float * projectionMatrix )
{
	return 0;
}
// /STEVE CHANGE
// ----------------------------------------------------------------------------

