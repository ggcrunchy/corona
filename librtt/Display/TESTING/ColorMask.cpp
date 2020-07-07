//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#include "TESTING.h"
#include "Renderer/Rtt_GL.h"
#include <vector>
	
struct ColorMaskSettings {
	bool red{true}, green{true}, blue{true}, alpha{true};
};

struct SharedColorMaskData {
	CoronaBeginFrameOpHandle beginFrameOp = {};
	CoronaCommandHandle command = {};
	CoronaRendererOp op;
	CoronaObjectParams params;
	ColorMaskSettings current, working;
	std::vector< ColorMaskSettings > stack;
	U32 id;
	bool hasSetID{false};
};

struct InstancedColorMaskData {
	SharedColorMaskData * shared;
	ColorMaskSettings settings;
	U8 hasRed : 1;
	U8 hasGreen : 1;
	U8 hasBlue : 1;
	U8 hasAlpha : 1;
};

static void
RegisterRendererLogic( lua_State * L, SharedColorMaskData * sharedData )
{
	CoronaCommand command = {
		[](const U8 * data, unsigned int size) {
			ColorMaskSettings mask;

			Rtt_ASSERT( size >= sizeof( ColorMaskSettings ) );

			memcpy( &mask, data, sizeof( ColorMaskSettings ) );

			glColorMask( mask.red, mask.green, mask.blue, mask.alpha );
		}, CopyWriter
	};

	CoronaRendererRegisterCommand( L, &sharedData->command, &command );
	CoronaRendererRegisterBeginFrameOp( L, &sharedData->beginFrameOp, [](CoronaRendererHandle rendererHandle, void * userData) {
		SharedColorMaskData * _this = static_cast< SharedColorMaskData * >( userData );
		ColorMaskSettings defSettings; // TODO: configurable?

		CoronaRendererIssueCommand( rendererHandle, _this->command, &defSettings, sizeof( ColorMaskSettings ) );

		_this->current = _this->working = defSettings;
	}, sharedData );
}

static void
UpdateColorMask( CoronaRendererHandle rendererHandle, void * userData )
{
	SharedColorMaskData * _this = static_cast< SharedColorMaskData * >( userData );

	CoronaRendererIssueCommand( rendererHandle, _this->command, &_this->working, sizeof( ColorMaskSettings ) );
	CoronaRendererScheduleForNextFrame( rendererHandle, _this->beginFrameOp, kBeginFrame_Schedule );
			
	_this->current = _this->working;
}

static CoronaObjectDrawParams
DrawParams()
{
	CoronaObjectDrawParams drawParams = {};

	drawParams.ignoreOriginal = true;
	drawParams.after = []( const CoronaDisplayObjectHandle, void * userData, CoronaRendererHandle rendererHandle )
	{
		InstancedColorMaskData * _this = static_cast< InstancedColorMaskData * >( userData );
		SharedColorMaskData * shared = _this->shared;

		if (_this->hasRed)
		{
			shared->working.red = _this->settings.red;
		}

		if (_this->hasGreen)
		{
			shared->working.green = _this->settings.green;
		}

		if (_this->hasBlue)
		{
			shared->working.blue = _this->settings.blue;
		}

		if (_this->hasAlpha)
		{
			shared->working.alpha = _this->settings.alpha;
		}

		if (memcmp( &shared->current, &shared->working, sizeof( ColorMaskSettings ) ) != 0)
		{
			CoronaRendererDo( rendererHandle, UpdateColorMask, shared );
		}
	};

	return drawParams;
}

static void
ClearValue( InstancedColorMaskData * _this, int index )
{
	switch (index)
	{
	case 0:
		_this->hasRed = false;

		break;
	case 1:
		_this->hasGreen = false;

		break;
	case 2:
		_this->hasBlue = false;

		break;
	case 3:
		_this->hasAlpha = false;

		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}
}

static void
SetValue( InstancedColorMaskData * _this, int index, bool enable )
{
	switch (index)
	{
	case 0:
		_this->settings.red = enable;
		_this->hasRed = true;

		break;
	case 1:
		_this->settings.green = enable;
		_this->hasGreen = true;

		break;
	case 2:
		_this->settings.blue = enable;
		_this->hasBlue = true;

		break;
	case 3:
		_this->settings.alpha = enable;
		_this->hasAlpha = true;

		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}
}

static CoronaObjectSetValueParams
SetValueParams()
{
	CoronaObjectSetValueParams setValueParams = {};

	setValueParams.before = []( const CoronaDisplayObjectHandle, void * userData, lua_State * L, const char key[], int valueIndex, int * result )
	{
		InstancedColorMaskData * _this = static_cast< InstancedColorMaskData * >( userData );

		if (strcmp( key, "red" ) == 0 || strcmp( key, "green" ) == 0 || strcmp( key, "blue" ) == 0 || strcmp( key, "alpha" ) == 0)
		{
			int index = key[1] != 'l' ? key[0] < 'r' : 2 + (key[0] < 'b');

			*result = true;

			if (lua_isnil( L, valueIndex ))
			{
				ClearValue( _this, index );
			}

			else
			{
				SetValue( _this, index, lua_toboolean( L, valueIndex ) );
			}
		}
	};

	return setValueParams;
}

static void
PushColorMask( SharedColorMaskData * shared, const ScopeMessagePayload & payload )
{
	shared->stack.push_back( shared->working );

	shared->id = payload.drawSessionID;
	shared->hasSetID = true;
}

static void
PopColorMask( SharedColorMaskData * shared, const ScopeMessagePayload & payload )
{
	shared->hasSetID = false;

	if (!shared->stack.empty())
	{
		shared->working = shared->stack.back();

		shared->stack.pop_back();

		if (memcmp( &shared->working, &shared->current, sizeof( ColorMaskSettings ) ) != 0)
		{
			CoronaRendererDo( payload.rendererHandle, UpdateColorMask, shared );
		}
	}

	else
	{
		CoronaLog( "Unbalanced 'didDraw' " );
	}
}

static CoronaObjectOnMessageParams
OnMessageParams()
{
	CoronaObjectOnMessageParams onMessageParams = {};

	onMessageParams.action = []( const CoronaDisplayObjectHandle, void * userData, const char * message, const void * data, U32 size )
	{
		InstancedColorMaskData * _this = static_cast< InstancedColorMaskData * >( userData );
		SharedColorMaskData * shared = _this->shared;

		if (strcmp( message, "willDraw" ) == 0 || strcmp( message, "didDraw" ) == 0)
		{
			if (size >= sizeof( ScopeMessagePayload ) )
			{
				ScopeMessagePayload payload = *static_cast< const ScopeMessagePayload * >( data );

				if ('w' == message[0] && !shared->hasSetID)
				{
					PushColorMask( shared, payload );
				}

				else if (shared->hasSetID && payload.drawSessionID == shared->id)
				{
					PopColorMask( shared, payload );
				}
			}
				
			else
			{
				CoronaLog( "'%s' message's payload too small", message );
			}
		}
	};

	return onMessageParams;
}

static void
PopulateSharedData( lua_State * L, SharedColorMaskData * sharedData )
{
	RegisterRendererLogic( L, sharedData );

	CoronaObjectParamsHeader paramsList = {};

	DisableCullAndHitTest( paramsList );

	CoronaObjectDrawParams drawParams = DrawParams();

	AddToParamsList( paramsList, &drawParams.header, kAugmentedMethod_Draw );

	CoronaObjectSetValueParams setValueParams = SetValueParams();

	AddToParamsList( paramsList, &setValueParams.header, kAugmentedMethod_SetValue );

	CoronaObjectOnMessageParams onMessageParams = OnMessageParams();

	AddToParamsList( paramsList, &onMessageParams.header, kAugmentedMethod_OnMessage );

	CoronaObjectLifetimeParams onFinalizeParams = {};

	onFinalizeParams.action = []( const CoronaDisplayObjectHandle, void * userData )
	{
		delete static_cast< InstancedColorMaskData * >( userData );
	};

	AddToParamsList( paramsList, &onFinalizeParams.header, kAugmentedMethod_OnFinalize );

	sharedData->params.useRef = true;
	sharedData->params.u.ref = CoronaObjectsBuildMethodStream( L, paramsList.next );
}

static int
New( lua_State * L )
{
	DummyArgs( L ); // [group, ]x, y, w, h

	static int sNonce;

	auto sharedColorMaskData = GetOrNew< SharedColorMaskData >( L, &sNonce, true );

	if (sharedColorMaskData.isNew)
	{
		PopulateSharedData( L, sharedColorMaskData.object );
	}

	InstancedColorMaskData * colorMaskData = new InstancedColorMaskData;

	memset( colorMaskData, 0, sizeof( InstancedColorMaskData ) );

	colorMaskData->shared = sharedColorMaskData.object;

	return CoronaObjectsPushRect( L, colorMaskData, &sharedColorMaskData.object->params );
}

int
ColorMaskLib( lua_State * L )
{
	lua_newtable( L ); // colorMaskLib

	luaL_Reg funcs[] = {
		{ "newColorMaskObject", New },
		{ NULL, NULL }
	};

	luaL_register( L, NULL, funcs );

	return 1;
}
