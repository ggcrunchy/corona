//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#include "Stencil.h"

struct SharedStencilStateData {
	CoronaBeginFrameOpHandle beginFrameOp = {};
	CoronaCommandHandle command = {};
	CoronaStateOpHandle stateOp = {};
	CoronaObjectParams params;
	StencilEnvironment * env;
};

struct InstancedStencilStateData {
	SharedStencilStateData * shared;
	StencilSettings settings;
	U8 hasFunc : 1;
	U8 hasFuncRef : 1;
	U8 hasFail : 1;
	U8 hasZFail : 1;
	U8 hasZPass : 1;
	U8 hasFuncMask : 1;
	U8 hasMask : 1;
	U8 hasEnabled : 1;
};

static void
RegisterRendererLogic( lua_State * L, SharedStencilStateData * sharedData )
{
	CoronaCommand command = {
		[](const U8 * data, unsigned int size) {
			StencilSettings settings[2];

			Rtt_ASSERT( size >= sizeof( settings ) );

			memcpy( settings, data, sizeof( settings ) );

			const StencilSettings & current = settings[0], & working = settings[1];

			if (current.func != working.func || current.funcRef != working.funcRef || current.funcMask != working.funcMask)
			{
				glStencilFunc( working.func, working.funcRef, working.funcMask );
			}

			if (current.mask != working.mask)
			{
				glStencilMask( working.mask );
			}

			if (current.fail != working.fail || current.zfail != working.zfail || current.zpass != working.zpass)
			{
				glStencilOp( working.fail, working.zfail, working.zpass );
			}

			if (current.enabled != working.enabled)
			{
				(working.enabled ? glEnable : glDisable)( GL_STENCIL_TEST );
			}
		}, CopyWriter
	};

	CoronaRendererRegisterCommand( L, &sharedData->command, &command );
	CoronaRendererRegisterBeginFrameOp( L, &sharedData->beginFrameOp, [](CoronaRendererHandle rendererHandle, void * userData) {
		SharedStencilStateData * _this = static_cast< SharedStencilStateData * >( userData );
		StencilEnvironment * env = _this->env;
		StencilSettings settings[] = { env->current.settings, StencilSettings{} };

		CoronaRendererIssueCommand( rendererHandle, _this->command, &settings, sizeof( settings ) );

		env->current.settings = env->working.settings = StencilSettings{};
	}, sharedData );
	CoronaRendererRegisterStateOp( L, &sharedData->stateOp, [](CoronaRendererHandle rendererHandle, void * userData) {
		SharedStencilStateData * _this = static_cast< SharedStencilStateData * >( userData );
		StencilEnvironment * env = _this->env;

		StencilSettings settings[] = { env->current.settings, env->working.settings };

		CoronaRendererIssueCommand( rendererHandle, _this->command, settings, sizeof( settings ) );
		CoronaRendererEnableClear( rendererHandle, env->clearOp, true );
		CoronaRendererScheduleForNextFrame( rendererHandle, env->beginFrameOp, kBeginFrame_Schedule );
		CoronaRendererScheduleForNextFrame( rendererHandle, _this->beginFrameOp, kBeginFrame_Schedule );

		env->current = env->working;
		env->anySinceClear = true;
	}, sharedData );
}

static CoronaObjectDrawParams
DrawParams()
{
	CoronaObjectDrawParams drawParams = {};

	drawParams.ignoreOriginal = true;
	drawParams.after = []( const CoronaDisplayObjectHandle, void * userData, CoronaRendererHandle rendererHandle )
	{
		InstancedStencilStateData * _this = static_cast< InstancedStencilStateData * >( userData );
		StencilEnvironment * env = _this->shared->env;

		if (_this->hasFunc)
		{
			env->working.settings.func = _this->settings.func;
		}

		if (_this->hasFuncRef)
		{
			env->working.settings.funcRef = _this->settings.funcRef;
		}

		if (_this->hasFuncMask)
		{
			env->working.settings.funcMask = _this->settings.funcMask;
		}

		if (_this->hasMask)
		{
			env->working.settings.mask = _this->settings.mask;
		}

		if (_this->hasFail)
		{
			env->working.settings.fail = _this->settings.fail;
		}

		if (_this->hasZFail)
		{
			env->working.settings.zfail = _this->settings.zfail;
		}

		if (_this->hasZPass)
		{
			env->working.settings.zpass = _this->settings.zpass;
		}

		if (_this->hasEnabled)
		{
			env->working.settings.enabled = _this->settings.enabled;
		}

		if (memcmp( &env->current.settings, &env->working.settings, sizeof( StencilSettings ) ) != 0)
		{
			CoronaRendererSetOperationStateDirty( rendererHandle, _this->shared->stateOp );
		}
	};

	return drawParams;
}

static void
UpdateEnabled( lua_State * L, InstancedStencilStateData * _this, int valueIndex )
{
	_this->hasEnabled = !lua_isnil( L, valueIndex );
	
	if (_this->hasEnabled)
	{
		_this->settings.enabled = lua_toboolean( L, valueIndex );
	}
}

static const char *
UpdateFunc( lua_State * L, InstancedStencilStateData * _this, int valueIndex )
{
	if (lua_isnil( L, valueIndex ))
	{
		_this->hasFunc = false;
	}

	else if (lua_isstring( L, valueIndex ))
	{
		if (FindFunc( L, valueIndex, &_this->settings.func ))
		{
			_this->hasFunc = true;
		}

		else
		{
			CoronaLuaWarning( L, "'%s' is not a supported stencil function", lua_tostring( L, valueIndex ) );
		}
	}

	else
	{
		return "string";
	}

	return NULL;
}

static void
ClearOpValue( InstancedStencilStateData * _this, int eventIndex )
{
	switch (eventIndex)
	{
	case 0:
		_this->hasFail = false;

		break;
	case 1:
		_this->hasZFail = false;

		break;
	case 2:
		_this->hasZPass = false;

		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}
}

static void
SetOpValue( lua_State * L, InstancedStencilStateData * _this, int eventIndex, int valueIndex )
{
	const char * names[] = { "keep", "zero", "replace", "increment", "incrementWrap", "decrement", "decrementWrap", "invert", nullptr };
	int opIndex = FindName( L, valueIndex, names );

	if (names[opIndex])
	{
		const GLenum ops[] = { GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_INCR_WRAP, GL_DECR, GL_DECR_WRAP, GL_INVERT };

		switch (eventIndex)
		{
		case 0:							
			_this->settings.fail = ops[opIndex];
			_this->hasFail = true;

			break;
		case 1:
			_this->settings.zfail = ops[opIndex];
			_this->hasZFail = true;

			break;
		case 2:
			_this->settings.zpass = ops[opIndex];
			_this->hasZPass = true;

			break;
		default:
			Rtt_ASSERT_NOT_REACHED();
		}
	}

	else
	{
		CoronaLuaWarning( L, "'%s' is not a supported stencil op", lua_tostring( L, valueIndex ) );
	}
}

static const char *
UpdateOp( lua_State * L, InstancedStencilStateData * _this, const char key[], int valueIndex )
{
	int eventIndex = ('z' == key[0]) + ('p' == key[1]);

	if (lua_isnil( L, valueIndex ))
	{
		ClearOpValue( _this, eventIndex );
	}

	else if (lua_isstring( L, valueIndex ))
	{
		SetOpValue( L, _this, eventIndex, valueIndex );
	}

	else
	{
		return "string";
	}

	return NULL;
}

static void
ClearConstantValue( InstancedStencilStateData * _this, int index )
{
	switch (index)
	{
	case 0:
		_this->hasFuncRef = false;

		break;
	case 1:
		_this->hasFuncMask = false;

		break;
	case 2:
		_this->hasMask = false;

		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}
}

static void
SetConstantValue( lua_State * L, InstancedStencilStateData * _this, int index, int valueIndex )
{
	switch (index)
	{
	case 0:
		_this->settings.funcRef = (int)lua_tointeger( L, valueIndex );
		_this->hasFuncRef = true;

		break;
	case 1:
		_this->settings.funcMask = (unsigned int)lua_tonumber( L, valueIndex );
		_this->hasFuncMask = true;

		break;
	case 2:
		_this->settings.mask = (unsigned int)lua_tonumber( L, valueIndex );
		_this->hasMask = true;

		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}
}

static const char *
UpdateConstant( lua_State * L, InstancedStencilStateData * _this, const char key[], int valueIndex )
{
	int index = 'f' == key[0] ? 'M' == key[4] : 2;

	if (lua_isnil( L, valueIndex ))
	{
		ClearConstantValue( _this, index );
	}

	else if (lua_isnumber( L, valueIndex ))
	{
		SetConstantValue( L, _this, index, valueIndex );
	}

	else
	{
		return "number";
	}

	return NULL;
}

static CoronaObjectSetValueParams
SetValueParams()
{
	CoronaObjectSetValueParams setValueParams = {};

	setValueParams.before = []( const CoronaDisplayObjectHandle, void * userData, lua_State * L, const char key[], int valueIndex, int * result )
	{
		InstancedStencilStateData * _this = static_cast< InstancedStencilStateData * >( userData );
		const char * expected = NULL;

		*result = true;

		if (strcmp( key, "enabled" ) == 0)
		{
			UpdateEnabled( L, _this, valueIndex );
		}

		else if (strcmp( key, "func" ) == 0)
		{
			expected = UpdateFunc( L, _this, valueIndex );
		}

		else if (strcmp( key, "fail" ) == 0 || strcmp( key, "zfail" ) == 0 || strcmp( key, "zpass" ) == 0)
		{
			expected = UpdateOp( L, _this, key, valueIndex );
		}

		else if (strcmp( key, "funcRef" ) == 0 || strcmp( key, "funcMask" ) == 0 || strcmp( key, "mask" ) == 0)
		{
			expected = UpdateConstant( L, _this, key, valueIndex );
		}

		else
		{
			*result = false;
		}

		if (expected)
		{
			CoronaLuaWarning( L, "Expected %s or nil for '%s', got %s", expected, key, luaL_typename( L, valueIndex ) );
		}
	};

	return setValueParams;
}

static void
PushStencilState( StencilEnvironment * env, const ScopeMessagePayload & payload )
{
	env->stack.push_back( env->working );

	env->id = payload.drawSessionID;
	env->hasSetID = true;
}

static void
PopStencilState( InstancedStencilStateData * _this, StencilEnvironment * env, const ScopeMessagePayload & payload )
{
	env->hasSetID = false;

	if (!env->stack.empty())
	{
		env->working = env->stack.back();

		env->stack.pop_back();

		if (memcmp( &env->working.settings, &env->current.settings, sizeof( StencilSettings ) ) != 0)
		{
			CoronaRendererSetOperationStateDirty( payload.rendererHandle, _this->shared->stateOp );
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
		InstancedStencilStateData * _this = static_cast< InstancedStencilStateData * >( userData );
		StencilEnvironment * env = _this->shared->env;

		if (strcmp( message, "willDraw" ) == 0 || strcmp( message, "didDraw" ) == 0)
		{
			if (size >= sizeof( ScopeMessagePayload ) )
			{
				ScopeMessagePayload payload = *static_cast< const ScopeMessagePayload * >( data );

				if ('w' == message[0] && !env->hasSetID)
				{
					PushStencilState( env, payload );
				}

				else if (env->hasSetID && payload.drawSessionID == env->id)
				{
					PopStencilState( _this, env, payload );
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
PopulateSharedData( lua_State * L, SharedStencilStateData * sharedData )
{
	StencilEnvironment * env = InitStencilEnvironment( L );

	sharedData->env = env;

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
		delete static_cast< InstancedStencilStateData * >( userData );
	};

	AddToParamsList( paramsList, &onFinalizeParams.header, kAugmentedMethod_OnFinalize );

	sharedData->params.useRef = true;
	sharedData->params.u.ref = CoronaObjectsBuildMethodStream( L, paramsList.next );
}

int
StencilStateObject( lua_State * L )
{
	DummyArgs( L ); // [group, ]x, y, w, h

	static int sNonce;

	auto sharedStateData = GetOrNew< SharedStencilStateData >(L, &sNonce );

	if (sharedStateData.isNew)
	{
		PopulateSharedData( L, sharedStateData.object );
	}

	InstancedStencilStateData * stateData = new InstancedStencilStateData;

	memset( stateData, 0, sizeof( InstancedStencilStateData ) );

	stateData->shared = sharedStateData.object;

	return CoronaObjectsPushRect( L, stateData, &sharedStateData.object->params );
}