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

struct SharedDepthClearData {
	CoronaObjectParams params;
	DepthEnvironment * env;
};

struct InstancedDepthClearData {
	SharedDepthClearData * shared;
	double clear;
	bool hasClear;
};

static CoronaObjectDrawParams
DrawParams()
{
	CoronaObjectDrawParams drawParams = {};

	drawParams.ignoreOriginal = true;
	drawParams.after = []( const CoronaDisplayObjectHandle, void * userData, CoronaRendererHandle rendererHandle )
	{
		InstancedDepthClearData * _this = static_cast< InstancedDepthClearData * >( userData );
		SharedDepthClearData * shared = _this->shared;

		if (_this->hasClear)
		{
			shared->env->current.clear = _this->clear;
		}

		CoronaRendererDo( rendererHandle, [](CoronaRendererHandle rendererHandle, void * userData) {
			DepthEnvironment * _this = static_cast< DepthEnvironment * >( userData );

			CoronaRendererIssueCommand( rendererHandle, _this->command, &_this->current.clear, sizeof( double ) ); // TODO: could pass in current and working, compare...

			_this->anySinceClear = false; // TODO: MIGHT be usable to avoid unnecessary clears
		}, shared->env );
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
			InstancedDepthClearData * _this = static_cast< InstancedDepthClearData * >( userData );

			if (lua_isnil( L, valueIndex ))
			{
				_this->hasClear = false;
			}

			else if (lua_isnumber( L, valueIndex ))
			{
				_this->clear = lua_tonumber( L, valueIndex ); // TODO: could validate
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
PopulateSharedData( lua_State * L, SharedDepthClearData * sharedData )
{
	DepthEnvironment * env = InitDepthEnvironment( L );

	sharedData->env = env;

	CoronaObjectParamsHeader paramsList = {};

	DisableCullAndHitTest( paramsList );

	CoronaObjectDrawParams drawParams = DrawParams();

	AddToParamsList( paramsList, &drawParams.header, kAugmentedMethod_Draw );

	CoronaObjectSetValueParams setValueParams = SetValueParams();

	AddToParamsList( paramsList, &setValueParams.header, kAugmentedMethod_SetValue );

	CoronaObjectLifetimeParams finalizeParams = {};

	finalizeParams.action = []( const CoronaDisplayObjectHandle, void * userData )
	{
		delete static_cast< InstancedDepthClearData * >( userData );
	};

	AddToParamsList( paramsList, &finalizeParams.header, kAugmentedMethod_OnFinalize );

	sharedData->params.useRef = true;
	sharedData->params.u.ref = CoronaObjectsBuildMethodStream( L, paramsList.next );
}

int
DepthClearObject( lua_State * L )
{
	DummyArgs( L ); // [group, ]x, y, w, h

	static int sNonce;

	auto sharedClearData = GetOrNew< SharedDepthClearData >(L, &sNonce );

	if (sharedClearData.isNew)
	{
		PopulateSharedData( L, sharedClearData.object );
	}

	InstancedDepthClearData * clearData = new InstancedDepthClearData;

	memset( clearData, 0, sizeof( InstancedDepthClearData ) );

	clearData->shared = sharedClearData.object;

	return CoronaObjectsPushRect( L, clearData, &sharedClearData.object->params );
}