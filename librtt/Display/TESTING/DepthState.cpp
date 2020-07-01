//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#include "Depth.h"

struct SharedDepthStateData {
	CoronaBeginFrameOpHandle beginFrameOp = {};
	CoronaCommandHandle command = {};
	CoronaStateOpHandle stateOp = {};
	CoronaObjectParams params;
	DepthEnvironment * env;
};

struct InstancedDepthStateData {
	SharedDepthStateData * shared;
	DepthSettings settings;
	CoronaMatrix4x4 projectionMatrix, viewMatrix;
	U16 hasFunc : 1;
	U16 hasCullFace : 1;
	U16 hasFrontFace : 1;
	U16 hasNear : 1;
	U16 hasFar : 1;
	U16 hasProjectionMatrix : 1;
	U16 hasViewMatrix : 1;
	U16 hasMask : 1;
	U16 hasEnabled : 1;
};

static void
RegisterRendererLogic( lua_State * L, SharedDepthStateData * sharedData )
{
	CoronaCommand command = {
		[](const U8 * data, unsigned int size) {
			DepthSettings settings[2];

			Rtt_ASSERT( size >= sizeof( settings ) );

			memcpy( settings, data, sizeof( settings ) );

			const DepthSettings & current = settings[0], & working = settings[1];

			if (current.func != working.func)
			{
				glDepthFunc( working.func );
			}

			if (current.cullFace != working.cullFace)
			{
				glCullFace( working.cullFace );
			}

			if (current.frontFace != working.frontFace)
			{
				glFrontFace( working.frontFace );
			}

			if (current.near != working.near || current.far != working.far)
			{
				Rtt_glDepthRange( working.near, working.far );
			}

			if (current.mask != working.mask)
			{
				glDepthMask( working.mask );
			}

			if (current.enabled != working.enabled)
			{
				(working.enabled ? glEnable : glDisable)( GL_DEPTH_TEST );
			}
		}, CopyWriter
	};

	CoronaRendererRegisterCommand( L, &sharedData->command, &command );
	CoronaRendererRegisterBeginFrameOp( L, &sharedData->beginFrameOp, [](CoronaRendererHandle rendererHandle, void * userData) {
		SharedDepthStateData * _this = static_cast< SharedDepthStateData * >( userData );
		DepthEnvironment * env = _this->env;
		DepthSettings settings[] = { env->current.settings, DepthSettings{} };

		CoronaRendererIssueCommand( rendererHandle, _this->command, &settings, sizeof( settings ) );

		env->current.settings = env->working.settings = DepthSettings{};
	}, sharedData );
	CoronaRendererRegisterStateOp( L, &sharedData->stateOp, [](CoronaRendererHandle rendererHandle, void * userData) {
		SharedDepthStateData * _this = static_cast< SharedDepthStateData * >( userData );
		DepthEnvironment * env = _this->env;

		DepthSettings settings[] = { env->current.settings, env->working.settings };

		CoronaRendererIssueCommand( rendererHandle, _this->command, settings, sizeof( settings ) );
		CoronaRendererEnableClear( rendererHandle, env->clearOp, true );
		CoronaRendererScheduleForNextFrame( rendererHandle, env->beginFrameOp, kBeginFrame_Schedule );
		CoronaRendererScheduleForNextFrame( rendererHandle, _this->beginFrameOp, kBeginFrame_Schedule );

		env->current = env->working;
		env->anySinceClear = true;
	}, sharedData );
}

static bool
MatricesDiffer( const CoronaMatrix4x4 m1, const CoronaMatrix4x4 m2 )
{
	return memcmp( m1, m2, sizeof( CoronaMatrix4x4 ) ) != 0;
}

static bool
HasDirtyMatrices( const DepthEnvironment * env )
{
	return env->matricesValid && (MatricesDiffer( env->current.projectionMatrix, env->working.projectionMatrix ) || MatricesDiffer( env->current.viewMatrix, env->working.viewMatrix ) );
}

static CoronaObjectDrawParams
DrawParams()
{
	CoronaObjectDrawParams drawParams = {};

	drawParams.ignoreOriginal = true;
	drawParams.after = [](const CoronaDisplayObjectHandle, void * userData, CoronaRendererHandle rendererHandle)
	{
		InstancedDepthStateData * _this = static_cast< InstancedDepthStateData * >( userData );
		DepthEnvironment * env = _this->shared->env;

		if (!env->matricesValid)
		{
			CoronaRendererGetFrustum( rendererHandle, env->current.viewMatrix, env->current.projectionMatrix );

			memcpy( env->working.projectionMatrix, env->current.projectionMatrix, sizeof( CoronaMatrix4x4 ) );
			memcpy( env->working.viewMatrix, env->current.viewMatrix, sizeof( CoronaMatrix4x4 ) );

			for (size_t i = 0; i < env->stack.size(); ++i)
			{
				memcpy( env->stack[i].projectionMatrix, env->current.projectionMatrix, sizeof( CoronaMatrix4x4 ) );
				memcpy( env->stack[i].viewMatrix, env->current.viewMatrix, sizeof( CoronaMatrix4x4 ) );
			}

			env->matricesValid = true;
		}

		if (_this->hasFunc)
		{
			env->working.settings.func = _this->settings.func;
		}

		if (_this->hasCullFace)
		{
			env->working.settings.cullFace = _this->settings.cullFace;
		}

		if (_this->hasFrontFace)
		{
			env->working.settings.frontFace = _this->settings.frontFace;
		}

		if (_this->hasNear)
		{
			env->working.settings.near = _this->settings.near;
		}

		if (_this->hasFar)
		{
			env->working.settings.far = _this->settings.far;
		}

		if (_this->hasProjectionMatrix)
		{
			memcpy( env->working.projectionMatrix, _this->projectionMatrix, sizeof( CoronaMatrix4x4 ) );
		}

		if (_this->hasViewMatrix)
		{
			memcpy( env->working.viewMatrix, _this->viewMatrix, sizeof( CoronaMatrix4x4 ) );
		}

		if (_this->hasMask)
		{
			env->working.settings.mask = _this->settings.mask;
		}

		if (_this->hasEnabled)
		{
			env->working.settings.enabled = _this->settings.enabled;
		}

		if (memcmp( &env->current.settings, &env->working.settings, sizeof( DepthSettings ) ) != 0)
		{
			CoronaRendererSetOperationStateDirty( rendererHandle, _this->shared->stateOp );
		}

		if (HasDirtyMatrices( env ))
		{
			CoronaRendererSetFrustum( rendererHandle, env->working.viewMatrix, env->working.projectionMatrix );
			
			memcpy( env->current.projectionMatrix, env->working.projectionMatrix, sizeof( CoronaMatrix4x4 ) );
			memcpy( env->current.viewMatrix, env->working.viewMatrix, sizeof( CoronaMatrix4x4 ) );
		}
	};

	return drawParams;
}

static void
UpdateEnabled( lua_State * L, InstancedDepthStateData * _this, int valueIndex )
{
	_this->hasEnabled = !lua_isnil( L, valueIndex );
	
	if (_this->hasEnabled)
	{
		_this->settings.enabled = lua_toboolean( L, valueIndex );
	}
}

static const char *
UpdateFunc( lua_State * L, InstancedDepthStateData * _this, int valueIndex )
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
			CoronaLuaWarning( L, "'%s' is not a supported depth function", lua_tostring( L, valueIndex ) );
		}
	}

	else
	{
		return "string";
	}

	return NULL;
}

static void
ClearFaceValue( InstancedDepthStateData * _this, int opIndex )
{
	switch (opIndex)
	{
	case 0:
		_this->hasCullFace = false;

		break;
	case 1:
		_this->hasFrontFace = false;

		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}
}

static void
SetFaceValue( lua_State * L, InstancedDepthStateData * _this, int opIndex, int valueIndex )
{
	const char * cullFaceNames[] = { "front", "back", "frontAndBack", NULL }, * frontFaceNames[] = { "cw", "ccw", NULL }, ** names = NULL;

	switch (opIndex)
	{
	case 0:
		names = cullFaceNames;

		break;
	case 1:
		names = frontFaceNames;

		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}

	int resultIndex = FindName( L, valueIndex, names );

	if (names[resultIndex])
	{
		switch (opIndex)
		{
		case 0:
			{
				const GLenum cullFaceValues[] = { GL_FRONT, GL_BACK, GL_FRONT_AND_BACK };

				_this->settings.cullFace = cullFaceValues[resultIndex];
			}

			_this->hasCullFace = true;

			break;
		case 1:
			{
				const GLenum frontFaceValues[] = { GL_CW, GL_CCW };

				_this->settings.frontFace = frontFaceValues[resultIndex];
			}

			_this->hasFrontFace = true;

			break;
		default:
			Rtt_ASSERT_NOT_REACHED();
		}
	}

	else
	{
		CoronaLuaWarning( L, "'%s' is not a supported face value", lua_tostring( L, valueIndex ) );
	}
}

static const char *
UpdateFaceOp( lua_State * L, InstancedDepthStateData * _this, const char key[], int valueIndex )
{
	int opIndex = 'f' == key[0];

	if (lua_isnil( L, valueIndex ))
	{
		ClearFaceValue( _this, opIndex );
	}

	else if (lua_isstring( L, valueIndex ))
	{
		SetFaceValue( L, _this, opIndex, valueIndex );
	}

	else
	{
		return "string";
	}

	return NULL;
}

static void
ClearConstantValue( InstancedDepthStateData * _this, int index )
{
	switch (index)
	{
	case 0:
		_this->hasNear = false;

		break;
	case 1:
		_this->hasFar = false;

		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}
}

static void
SetConstantValue( lua_State * L, InstancedDepthStateData * _this, int index, int valueIndex )
{
	switch (index)
	{
	case 0:
		_this->settings.near = lua_tonumber( L, valueIndex );
		_this->hasNear = true;

		break;
	case 1:
		_this->settings.far = lua_tonumber( L, valueIndex );
		_this->hasFar = true;

		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}
}

static const char *
UpdateConstant( lua_State * L, InstancedDepthStateData * _this, const char key[], int valueIndex )
{
	int index = 'f' == key[0];

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

static void
ClearMatrix( InstancedDepthStateData * _this, int index )
{
	switch (index)
	{
	case 0:
		_this->hasProjectionMatrix = false;

		break;
	case 1:
		_this->hasViewMatrix = false;

		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}
}

static void
SetMatrix( lua_State * L, InstancedDepthStateData * _this, int index, int valueIndex )
{
	if (lua_istable( L, valueIndex ))
	{
		switch (index)
		{
		case 0:
		{
			const char * names[] = { "fovy", "aspectRatio", "zNear", "zFar" }, * badType = NULL;
			float args[4] = {};

			for (int i = 0; i < 4 && !badType; ++i)
			{
				lua_getfield( L, valueIndex, names[i] ); // ..., value

				if (lua_isnumber( L, -1 ))
				{
					args[i] = (float)lua_tonumber( L, -1 );
				}

				else
				{
					badType = luaL_typename( L, -1 );

					CoronaLuaWarning( L, "Expected number for projection matrix component '%s', got %s", names[i], badType );
				}

				lua_pop( L, 1 ); // ...
			}

			if (!badType)
			{
				CoronaCreatePerspectiveMatrix( args[0], args[1], args[2], args[3], _this->projectionMatrix );

				_this->hasProjectionMatrix = true;
			}
		}

			break;
		case 1:
		{
			const char * names[] = { "eye", "center", "up" }, * badType = NULL;
			CoronaVector3 vecs[3];

			for (int i = 0; i < 3 && !badType; ++i)
			{
				lua_getfield( L, valueIndex, names[i] ); // ..., vec

				if (lua_istable( L, -1 ))
				{
					for (int j = 1; j <= 3 && !badType; ++j)
					{
						lua_rawgeti( L, -1, j ); // ..., vec, comp

						if (lua_isnumber( L, -1 ))
						{
							vecs[i][j - 1] = (float)lua_tonumber( L, -1 );
						}

						else
						{
							badType = luaL_typename( L, -1 );

							CoronaLuaWarning( L, "Expected number for view matrix vector component #%i, got %s", j, badType );
						}

						lua_pop( L, 1 ); // ..., vec
					}
				}

				else
				{
					badType = luaL_typename( L, -1 );

					CoronaLuaWarning( L, "Expected table for view matrix vector %s, got %s", names[i], badType );
				}

				lua_pop( L, 1 ); // ...
			}

			if (!badType)
			{
				CoronaCreateViewMatrix( vecs[0], vecs[1], vecs[2], _this->viewMatrix );

				_this->hasViewMatrix = true;
			}
		}

			break;
		default:
			Rtt_ASSERT_NOT_REACHED();
		}
	}

	else
	{
		CoronaLuaWarning( L, "Expected matrix provided as table " );
	}
}

static const char *
UpdateMatrix( lua_State * L, InstancedDepthStateData * _this, const char key[], int valueIndex )
{
	int index = 'v' == key[0];

	if (lua_isnil( L, valueIndex ))
	{
		ClearMatrix( _this, index );
	}

	else if (lua_isnumber( L, valueIndex ))
	{
		SetMatrix( L, _this, index, valueIndex );
	}

	else
	{
		return "table";
	}

	return NULL;
}

static CoronaObjectSetValueParams
SetValueParams()
{
	CoronaObjectSetValueParams setValueParams = {};

	setValueParams.before = [](const CoronaDisplayObjectHandle, void * userData, lua_State * L, const char key[], int valueIndex, int * result)
	{
		InstancedDepthStateData * _this = static_cast< InstancedDepthStateData * >( userData );
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

		else if (strcmp( key, "cullFace" ) == 0 || strcmp( key, "frontFace" ) == 0)
		{
			expected = UpdateFaceOp( L, _this, key, valueIndex );
		}

		else if (strcmp( key, "near" ) == 0 || strcmp( key, "far" ) == 0)
		{
			expected = UpdateConstant( L, _this, key, valueIndex );
		}

		else if (strcmp( key, "projectionMatrix" ) == 0 || strcmp( key, "viewMatrix" ) == 0)
		{
			expected = UpdateMatrix( L, _this, key, valueIndex );
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
PushDepthState( DepthEnvironment * env, const ScopeMessagePayload & payload )
{
	env->stack.push_back( env->working );

	env->id = payload.drawSessionID;
	env->hasSetID = true;
}

static void
PopDepthState( InstancedDepthStateData * _this, DepthEnvironment * env, const ScopeMessagePayload & payload )
{
	env->hasSetID = false;

	if (!env->stack.empty())
	{
		env->working = env->stack.back();

		env->stack.pop_back();

		if (memcmp( &env->working.settings, &env->current.settings, sizeof( DepthSettings ) ) != 0)
		{
			CoronaRendererSetOperationStateDirty( payload.rendererHandle, _this->shared->stateOp );
		}

		if (HasDirtyMatrices( env ))
		{
			CoronaRendererSetFrustum( payload.rendererHandle, env->working.viewMatrix, env->working.projectionMatrix );
			
			memcpy( env->current.projectionMatrix, env->working.projectionMatrix, sizeof( CoronaMatrix4x4 ) );
			memcpy( env->current.viewMatrix, env->working.viewMatrix, sizeof( CoronaMatrix4x4 ) );
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

	onMessageParams.action = [](const CoronaDisplayObjectHandle, void * userData, const char * message, const void * data, U32 size)
	{
		InstancedDepthStateData * _this = static_cast< InstancedDepthStateData * >( userData );
		DepthEnvironment * env = _this->shared->env;

		if (strcmp( message, "willDraw" ) == 0 || strcmp( message, "didDraw" ) == 0)
		{
			if (size >= sizeof( ScopeMessagePayload ) )
			{
				ScopeMessagePayload payload = *static_cast< const ScopeMessagePayload * >( data );

				if ('w' == message[0] && !env->hasSetID)
				{
					PushDepthState( env, payload );
				}

				else if (env->hasSetID && payload.drawSessionID == env->id)
				{
					PopDepthState( _this, env, payload );
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
PopulateSharedData( lua_State * L, SharedDepthStateData * sharedData )
{
	DepthEnvironment * env = InitDepthEnvironment( L );

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

	onFinalizeParams.action = [](const CoronaDisplayObjectHandle, void * userData)
	{
		delete static_cast< InstancedDepthStateData * >( userData );
	};

	AddToParamsList( paramsList, &onFinalizeParams.header, kAugmentedMethod_OnFinalize );

	sharedData->params.useRef = true;
	sharedData->params.u.ref = CoronaObjectsBuildMethodStream( L, paramsList.next );
}

int
DepthStateObject( lua_State * L )
{
	DummyArgs( L ); // [group, ]x, y, w, h

	static int sNonce;

	auto sharedStateData = GetOrNew< SharedDepthStateData >(L, &sNonce );

	if (sharedStateData.isNew)
	{
		PopulateSharedData( L, sharedStateData.object );
	}

	InstancedDepthStateData * stateData = new InstancedDepthStateData;

	memset( stateData, 0, sizeof( InstancedDepthStateData ) );

	stateData->shared = sharedStateData.object;

	return CoronaObjectsPushRect( L, stateData, &sharedStateData.object->params );
}