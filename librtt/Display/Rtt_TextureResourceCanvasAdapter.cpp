//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Rtt_TextureResourceCanvasAdapter.h"
#include "Rtt_TextureResourceCanvas.h"
#include "Rtt_LuaContext.h"
#include "Rtt_LuaProxyVTable.h"
#include "Rtt_LuaLibDisplay.h"
#include "Rtt_GroupObject.h"
// STEVE CHANGE
#include "Rtt_Display.h"
#include "Rtt_Runtime.h"

#include "Rtt_ObjectBoxList.h"
#include "Renderer/Rtt_Renderer.h"
#include "Renderer/Rtt_CommandBuffer.h"
#include "Renderer/Rtt_FrameBufferObject.h"

#include "CoronaObjects.h"
#include "CoronaGraphics.h"
// /STEVE CHANGE

namespace Rtt {
	
const TextureResourceCanvasAdapter&
TextureResourceCanvasAdapter::Constant()
{
	static const TextureResourceCanvasAdapter sAdapter;
	return sAdapter;
}

StringHash*
TextureResourceCanvasAdapter::GetHash( lua_State *L ) const
{
	static const char *keys[] =
	{
		"width",            //0
		"height",			//1
		"pixelWidth",	    //2
		"pixelHeight",		//3
		"setBackground",	//4
		"draw",				//5
		"invalidate",		//6
		"cache",            //7
		"anchorX",          //8
		"anchorY",          //9
	};
	
	static StringHash sHash( *LuaContext::GetAllocator( L ), keys, sizeof( keys ) / sizeof( const char * ), 10, 20, 5, __FILE__, __LINE__ );
	return &sHash;
}

int
TextureResourceCanvasAdapter::ValueForKey(
								const LuaUserdataProxy& sender,
								lua_State *L,
								const char *key ) const
{
	int results = 0;
	
	Rtt_ASSERT( key ); // Caller should check at the top-most level
	
	const TextureResourceCanvas *entry = (const TextureResourceCanvas *)sender.GetUserdata();
	if ( ! entry ) { return results; }
	
	int index = GetHash( L )->Lookup( key );
	
	if ( index >= 0 )
	{
		switch ( index )
		{
			case 0:
				lua_pushnumber( L, entry->GetContentWidth() );
				results = 1;
				break;
				
			case 1:
				lua_pushnumber( L, entry->GetContentHeight() );
				results = 1;
				break;

			case 2:
				lua_pushinteger( L, entry->GetTexWidth() );
				results = 1;
				break;
				
			case 3:
				lua_pushinteger( L, entry->GetTexHeight() );
				results = 1;
				break;
				
			case 4:
				Lua::PushCachedFunction( L, Self::setBackground );
				results = 1;
				break;

			case 5:
				Lua::PushCachedFunction( L, Self::draw );
				results = 1;
				break;

			case 6:
				Lua::PushCachedFunction( L, Self::invalidate );
				results = 1;
				break;
				
			case 7:
				if ( !entry->GetCacheGroup()->IsReachable() )
				{
					entry->GetCacheGroup()->InitProxy( L );
				}
				if( entry->GetCacheGroup()->GetProxy()->PushTable( L ) )
				{
					results = 1;
				}
				break;
			case 8:
				lua_pushnumber( L, entry->GetAnchorX() );
				results = 1;
				break;
			case 9:
				lua_pushnumber( L, entry->GetAnchorY() );
				results = 1;
				break;

			default:
				Rtt_ASSERT_NOT_REACHED();
				break;
		}
	}
	else
	{
		results = Super::Constant().ValueForKey( sender, L, key );
	}
	
	return results;
}

bool TextureResourceCanvasAdapter::SetValueForKey(LuaUserdataProxy& sender,
							lua_State *L,
							const char *key,
							int valueIndex ) const
{
	bool result = false;
	
	Rtt_ASSERT( key ); // Caller should check at the top-most level
	
	TextureResourceCanvas *entry = (TextureResourceCanvas *)sender.GetUserdata();
	if ( ! entry ) { return result; }
	
	int index = GetHash( L )->Lookup( key );
	
	switch ( index )
	{
		case 0:
			if(lua_type( L, valueIndex) == LUA_TNUMBER)
			{
				entry->SetContentWidth(lua_tonumber( L, valueIndex));
				result = true;
			}
			break;
		case 1:
			if(lua_type( L, valueIndex) == LUA_TNUMBER)
			{
				entry->SetContentHeight(lua_tonumber( L, valueIndex));
				result = true;
			}
			break;
		case 8:
			if(lua_type( L, valueIndex) == LUA_TNUMBER)
			{
				entry->SetAnchorX(lua_tonumber( L, valueIndex));
				result = true;
			}
			break;
		case 9:
			if(lua_type( L, valueIndex) == LUA_TNUMBER)
			{
				entry->SetAnchorY(lua_tonumber( L, valueIndex));
				result = true;
			}
			break;
			
		default:
			result = Super::Constant().SetValueForKey( sender, L, key, valueIndex );
			break;
	}
	return result;

}

	

int TextureResourceCanvasAdapter::setBackground( lua_State *L )
{
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, 1 );
	if (sender) {
		TextureResourceCanvas *entry = (TextureResourceCanvas *)sender->GetUserdata();
		if (entry) {
			Color c = ColorZero();
			c = LuaLibDisplay::toColor( L, 2, false );
			entry->SetClearColor( c );
		}
	}
	return 0;

}


int TextureResourceCanvasAdapter::draw( lua_State *L )
{
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, 1 );
	if (sender)
	{
		TextureResourceCanvas *entry = (TextureResourceCanvas *)sender->GetUserdata();
		if (entry)
		{
			// forwarding CanvasObject:draw to GroupObject:insert with group being Queue
			LuaGroupObjectProxyVTable::Insert( L, entry->GetQueueGroup() );
		}
	}
	
	return 0;
}

int TextureResourceCanvasAdapter::invalidate( lua_State *L )
{
	bool cache = false;
	bool clear = false;
	int index = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, index );
	index ++;
	
	if ( lua_istable( L, index ) )
	{
		lua_getfield( L, index, "source" );
		if( lua_isstring( L, -1 ) )
		{
			if(strcmp( lua_tostring( L, -1 ), "cache" ) == 0)
			{
				cache = true;
				clear = true;
			}
		}
		lua_pop( L, 1 );
		
		lua_getfield( L, index, "accumulate" );
		if( lua_isboolean( L, -1 ) )
		{
			clear = ! lua_toboolean( L, -1 );
		}
		lua_pop( L, 1 );
	}
	else if ( lua_isstring( L, index ) )
	{
		if(strcmp( lua_tostring( L, -1 ), "cache" ) == 0)
		{
			cache = true;
			clear = true;
		}
	}
	
	
	if (sender)
	{
		TextureResourceCanvas *entry = (TextureResourceCanvas *)sender->GetUserdata();
		if (entry)
		{
			entry->Invalidate( cache, clear );
		}
	}
	
	return 0;
}

// STEVE CHANGE
const TextureResourceCaptureAdapter&
TextureResourceCaptureAdapter::Constant()
{
	static const TextureResourceCaptureAdapter sAdapter;
	return sAdapter;
}

StringHash *
TextureResourceCaptureAdapter::GetHash( lua_State *L ) const
{
	static const char *keys[] =
	{
		"width",            //0
		"height",			//1
		"pixelWidth",	    //2
		"pixelHeight",		//3
		"newCaptureRect"	//4
	};
	
	static StringHash sHash( *LuaContext::GetAllocator( L ), keys, sizeof( keys ) / sizeof( const char * ), 5, 2, 1, __FILE__, __LINE__ );
	return &sHash;
}

int
TextureResourceCaptureAdapter::ValueForKey(
						const LuaUserdataProxy& sender,
						lua_State *L,
						const char *key ) const
{
	int results = 0;
	
	Rtt_ASSERT( key ); // Caller should check at the top-most level
	
	const TextureResourceCapture *entry = (const TextureResourceCapture *)sender.GetUserdata();
	if ( ! entry ) { return results; }
	
	int index = GetHash( L )->Lookup( key );
	
	if ( index >= 0 )
	{
		switch ( index )
		{
			case 0:
				lua_pushnumber( L, entry->GetContentWidth() );
				results = 1;
				break;
				
			case 1:
				lua_pushnumber( L, entry->GetContentHeight() );
				results = 1;
				break;

			case 2:
				lua_pushinteger( L, entry->GetTexWidth() );
				results = 1;
				break;
				
			case 3:
				lua_pushinteger( L, entry->GetTexHeight() );
				results = 1;
				break;
				
			case 4:
				Lua::PushCachedFunction( L, Self::newCaptureRect );
				results = 1;
				break;
				
			default:
				Rtt_ASSERT_NOT_REACHED();
				break;
		}
	}
	else
	{
		results = Super::Constant().ValueForKey( sender, L, key );
	}
	
	return results;
}

bool
TextureResourceCaptureAdapter::SetValueForKey(LuaUserdataProxy& sender,
							lua_State *L,
							const char *key,
							int valueIndex ) const
{
	bool result = false;
	
	Rtt_ASSERT( key ); // Caller should check at the top-most level
	
	TextureResourceCapture *entry = (TextureResourceCapture *)sender.GetUserdata();
	if ( ! entry ) { return result; }
	
	int index = GetHash( L )->Lookup( key );
	
	switch ( index )
	{
		default:
			result = Super::Constant().SetValueForKey( sender, L, key, valueIndex );
			break;
	}
	return result;
}

int
TextureResourceCaptureAdapter::newCaptureRect( lua_State *L )
{
	static bool sInitialized;
	static CoronaObjectParams sParams;
	static unsigned long sStateBlockID;
	
	struct InputDrawData {
		TextureResourceCapture * capture;
		Display * display;
	};
	
	if (!sInitialized)
	{
		sInitialized = true;

		struct CaptureState {
			U32 counter;
			TextureResourceCapture * capture;
			Rect rect, rawRect;
			S32 height;
		};
		
		CoronaStateBlock captureState = {};
		
		captureState.blockSize = sizeof(CaptureState);
		captureState.stateDirty = []( const CoronaCommandBuffer * commandBuffer, const CoronaRenderer * renderer, const void * newContents, const void *, unsigned int, int restore, void * ) {
			CaptureState state = *static_cast< const CaptureState * >( newContents );
		
			if (!restore)
			{
				Renderer * rendererObject = OBJECT_BOX_LOAD( Renderer, renderer );
				
				Rtt_ASSERT( rendererObject );
				
				FrameBufferObject * fbo = state.capture->GetFBO(), * oldFBO = NULL;

				CommandBuffer * commandBufferObject = OBJECT_BOX_LOAD( CommandBuffer, commandBuffer );
				
				Rtt_ASSERT( commandBufferObject );
				
				if (fbo)
				{
					oldFBO = rendererObject->GetFrameBufferObject();
					
					commandBufferObject->BindFrameBufferObject( fbo, true );
				}
				
				commandBufferObject->CaptureRect( fbo, state.capture->GetTexture(), state.rect, state.rawRect, state.height );

				if (fbo)
				{
					commandBufferObject->BindFrameBufferObject( oldFBO, false );
				}
			}
		};
		
		CoronaRendererRegisterStateBlock( L, &captureState, &sStateBlockID );
		
		CoronaObjectParamsHeader paramsList = {};
		
		CoronaObjectBooleanResultParams canHitTest = {};
		
		canHitTest.header.method = kAugmentedMethod_CanHitTest;
		canHitTest.before = []( const CoronaDisplayObject *, void *, int * result )
		{
			*result = false;
		};
		
		struct DrawData {
			DrawData( TextureResourceCapture * capture, Display * display )
			:	fResource( capture->GetWeakResource() ),
				fDisplay( display )
			{
			}
			
			WeakPtr<TextureResource> fResource;
			Display * fDisplay;
		};
		
		CoronaObjectDrawParams drawParams = {};

		drawParams.header.method = kAugmentedMethod_Draw;
		drawParams.ignoreOriginal = true;
		drawParams.after = []( const CoronaDisplayObject * object, void * userData, const CoronaRenderer * renderer )
		{
			static U32 sCounter;
			
			DrawData * drawData = static_cast< DrawData * >( userData );
			SharedPtr<TextureResource> tex( drawData->fResource );
			
			if (!tex.IsNull())
			{				
				DisplayObject * displayObject = OBJECT_BOX_LOAD( DisplayObject, object );
				
				Rtt_ASSERT( displayObject );

				TextureResourceCapture & capture = static_cast< TextureResourceCapture & >( *tex );
				CaptureState state = {};
				
				state.capture = &capture;
				S32 screenWidth = drawData->fDisplay->ScreenWidth();
				S32 screenHeight = drawData->fDisplay->ScreenHeight();
				S32 windowWidth = drawData->fDisplay->WindowWidth();
				S32 windowHeight = drawData->fDisplay->WindowHeight();
				
				float screenToWindowX = (float)windowWidth / screenWidth;
				float screenToWindowY = (float)windowHeight / screenHeight;
				
				Vertex2 center;
				
				displayObject->StageBounds().GetCenter( center );
				
				S32 x = center.x;
				S32 y = center.y;
				
				drawData->fDisplay->ContentToScreen( x, y );
				
				Rect texBounds;
				
				texBounds.xMin = -state.capture->GetTexWidth() / 2;
				texBounds.yMin = -state.capture->GetTexHeight() / 2;
				texBounds.xMax = state.capture->GetTexWidth() / 2;
				texBounds.yMax = state.capture->GetTexHeight() / 2;
				
				texBounds.Translate( x * screenToWindowX, y * screenToWindowY );
								
				texBounds.xMin = floorf( texBounds.xMin );
				texBounds.yMin = floorf( texBounds.yMin );
				texBounds.xMax = ceilf( texBounds.xMax - Rtt_REAL_HALF );
				texBounds.yMax = ceilf( texBounds.yMax - Rtt_REAL_HALF );

				state.rawRect = texBounds;				

				Rect bounds;
				
				bounds.xMin = bounds.yMin = 0;
				bounds.xMax = windowWidth;
				bounds.yMax = windowHeight;
				
				texBounds.Intersect( bounds );

				if (texBounds.NotEmpty())
				{
					state.counter = sCounter++;
					state.rect = texBounds;
					state.height = windowHeight;
					
					Renderer * rendererObject = OBJECT_BOX_LOAD( Renderer, renderer );
					
					Rtt_ASSERT( rendererObject );
					
					FrameBufferObject * fbo = capture.GetFBO();
										
					if (NULL == state.capture->GetTexture().GetGPUResource())
					{
						rendererObject->QueueCreate( &state.capture->GetTexture() );
					}
					
					if (fbo && NULL == fbo->GetGPUResource())
					{
						rendererObject->QueueCreate( fbo );
					}
					
					CoronaRendererWriteStateBlock( renderer, sStateBlockID, &state, sizeof(CaptureState) );
				}
			}
		};
   
		CoronaObjectOnCreateParams onCreateParams = {};
		
		onCreateParams.header.method = kAugmentedMethod_OnCreate;
		onCreateParams.action = []( const CoronaDisplayObject *, void ** userData ) {
			InputDrawData * input = static_cast< InputDrawData * >( *userData );

			*userData = Rtt_NEW( NULL, DrawData( input->capture, input->display ) );
		};
		
		CoronaObjectOnFinalizeParams onFinalizeParams = {};

		onFinalizeParams.header.method = kAugmentedMethod_OnFinalize;
		onFinalizeParams.action = []( const CoronaDisplayObject *, void * userData )
		{
			delete static_cast< DrawData * >( userData );
		};
		
		paramsList.next = &canHitTest.header;
		canHitTest.header.next = &drawParams.header;
		drawParams.header.next = &onCreateParams.header;
		onCreateParams.header.next = &onFinalizeParams.header;
		
		sParams.useRef = true;
		sParams.u.ref = CoronaObjectsBuildMethodStream( L, paramsList.next );
	}
	
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, 1 );
	
	if (sender)
	{
		TextureResourceCapture *entry = (TextureResourceCapture *)sender->GetUserdata();

		lua_remove( L, 1 ); // ...
		
		InputDrawData input;
		
		input.capture = entry;
		input.display = &Rtt::LuaContext::GetRuntime( L )->GetDisplay();
		
		if ( !( entry && CoronaObjectsPushRect( L, &input, &sParams ) ) ) // ...[, rect]
		{
			lua_pushnil( L ); // ..., nil
		}
	}
	
	return 1;
}
// /STEVE CHANGE
	
} // namespace Rtt
