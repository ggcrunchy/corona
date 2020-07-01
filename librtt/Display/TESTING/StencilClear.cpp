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

struct SharedStencilClearData {
	CoronaStateOpHandle stateOp = {};
	CoronaObjectParams params;
	StencilEnvironment * env;
};

struct InstancedStencilClearData {
	SharedStencilClearData * shared;
	int clear;
	bool hasClear;
};

static void
RegisterRendererLogic( lua_State * L, SharedStencilClearData * sharedData )
{
	CoronaRendererRegisterStateOp( L, &sharedData->stateOp, [](CoronaRendererHandle rendererHandle, void * userData) {
		StencilEnvironment * _this = static_cast< StencilEnvironment * >( userData );

		CoronaRendererIssueCommand( rendererHandle, _this->command, &_this->current.clear, sizeof( int ) ); // TODO: could pass in current and working, compare...

		_this->anySinceClear = false; // TODO: MIGHT be usable to avoid unnecessary clears
	}, sharedData->env );
}

static CoronaObjectDrawParams
DrawParams()
{
	CoronaObjectDrawParams drawParams = {};

	drawParams.ignoreOriginal = true;
	drawParams.after = []( const CoronaDisplayObjectHandle, void * userData, CoronaRendererHandle rendererHandle )
	{
		InstancedStencilClearData * _this = static_cast< InstancedStencilClearData * >( userData );
		SharedStencilClearData * shared = _this->shared;

		if (_this->hasClear)
		{
			shared->env->current.clear = _this->clear;
		}

		CoronaRendererSetOperationStateDirty( rendererHandle, shared->stateOp );
	};

	return drawParams;
}

static CoronaObjectSetValueParams
SetValueParams()
{
	CoronaObjectSetValueParams setValueParams = {};

	setValueParams.before = []( const CoronaDisplayObjectHandle, void * userData, lua_State * L, const char key[], int valueIndex, int * result )
	{
		if (strcmp( key, "value" ) == 0)
		{
			InstancedStencilClearData * _this = static_cast< InstancedStencilClearData * >( userData );

			if (lua_isnil( L, valueIndex ))
			{
				_this->hasClear = false;
			}

			else if (lua_isnumber( L, valueIndex ))
			{
				_this->clear = lua_tointeger( L, valueIndex ); // TODO: could validate
				_this->hasClear = true;
			}

			else
			{
				CoronaLuaWarning( L, "Expected number or nil for 'value', got %s", luaL_typename( L, valueIndex ) );
			}

			*result = true;
		}
	};

	return setValueParams;
}

static void
PopulateSharedData( lua_State * L, SharedStencilClearData * sharedData )
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

	CoronaObjectLifetimeParams finalizeParams = {};

	finalizeParams.action = []( const CoronaDisplayObjectHandle, void * userData )
	{
		delete static_cast< InstancedStencilClearData * >( userData );
	};

	AddToParamsList( paramsList, &finalizeParams.header, kAugmentedMethod_OnFinalize );

	sharedData->params.useRef = true;
	sharedData->params.u.ref = CoronaObjectsBuildMethodStream( L, paramsList.next );
}

int
StencilClearObject( lua_State * L )
{
	DummyArgs( L ); // [group, ]x, y, w, h

	static int sNonce;

	auto sharedClearData = GetOrNew< SharedStencilClearData >(L, &sNonce );

	if (sharedClearData.isNew)
	{
		PopulateSharedData( L, sharedClearData.object );
	}

	InstancedStencilClearData * clearData = new InstancedStencilClearData;

	memset( clearData, 0, sizeof( InstancedStencilClearData ) );

	clearData->shared = sharedClearData.object;

	return CoronaObjectsPushRect( L, clearData, &sharedClearData.object->params );
}