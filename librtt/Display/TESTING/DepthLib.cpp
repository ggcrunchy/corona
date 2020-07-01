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

struct TransformData {
	char * newString;
	const char ** stringList;
};

static std::string
UpdateSource( std::string str )
{
	FindAndReplace( str, "vec2 a_Position", "vec3 a_Position" );
	FindAndReplace( str, "vec2 position", "vec3 position" );

	for (int i = 0; i < 3; ++i) // masks
	{
		FindAndReplace( str, "position, 1.0", "position.xy, 1.0" );
	}

	FindAndReplace( str, "position, 0.0", "position" );

	return str;
}

static const char **
SourceTransformBegin( CoronaShaderSourceTransformParams * params, void * userData, void * )
{
	TransformData * transformData = static_cast< TransformData * >( userData );

	transformData->stringList = static_cast< const char ** >( malloc( params->nsources * sizeof( const char * ) ) );
	transformData->newString = NULL;

	for (size_t i = 0; i < params->nsources; ++i)
	{
		const char * source = params->sources[i];

		if (strcmp( params->hints[i], "vertexSource" ) == 0)
		{
			std::string updated = UpdateSource( source );

			source = transformData->newString = strdup( updated.c_str() );
		}

		transformData->stringList[i] = source;
	}

	return transformData->stringList;
}

static void
SourceTransformFinish( void * userData, void * )
{
	TransformData * transformData = static_cast< TransformData * >( userData );

	free( transformData->newString );
	free( transformData->stringList );
}

static int
Register3DCustomization( lua_State * L )
{
	CoronaShaderCallbacks callbacks = {};

	callbacks.size = sizeof( CoronaShaderCallbacks );
	callbacks.transform.begin = SourceTransformBegin;
	callbacks.transform.finish = SourceTransformFinish;
	callbacks.transform.extraSpace = sizeof( TransformData );

	lua_pushboolean( L, CoronaShaderRegisterCustomization( L, "instances", &callbacks ) ); // ..., ok?

	return 1;
}

struct SharedTransformableState {
	CoronaMatrix4x4 prevViewMatrix, prevProjectionMatrix;
	CoronaObjectParams params;
};

struct InstancedTransformableState {
	CoronaMatrix4x4 xform;
	SharedTransformableState * shared;
};

static CoronaObjectDrawParams
DrawParams()
{
	CoronaObjectDrawParams drawParams = {};

	drawParams.before = []( const CoronaDisplayObjectHandle, void * userData, CoronaRendererHandle renderer )
	{
		InstancedTransformableState * _this = static_cast< InstancedTransformableState * >( userData );

		CoronaMatrix4x4 result;

		CoronaRendererGetFrustum( renderer, _this->shared->prevViewMatrix, _this->shared->prevProjectionMatrix );
		CoronaMultiplyMatrix4x4( _this->shared->prevViewMatrix, _this->xform, result );
		CoronaRendererSetFrustum( renderer, result, _this->shared->prevProjectionMatrix );
	};

	drawParams.after = []( const CoronaDisplayObjectHandle, void * userData, CoronaRendererHandle renderer )
	{
		InstancedTransformableState * _this = static_cast< InstancedTransformableState * >( userData );

		CoronaRendererSetFrustum( renderer,_this->shared->prevViewMatrix, _this->shared->prevProjectionMatrix );
	};

	return drawParams;
}

static CoronaObjectSetValueParams
SetValueParams()
{
	CoronaObjectSetValueParams setValueParams = {};

	setValueParams.before = []( const CoronaDisplayObjectHandle, void * userData, lua_State * L, const char key[], int valueIndex, int * result )
	{
		InstancedTransformableState * _this = static_cast< InstancedTransformableState * >( userData );

		if (strcmp( key, "transform" ) == 0)
		{
			if (lua_istable( L, valueIndex ))
			{
				const char * badType = NULL;
				CoronaMatrix4x4 matrix;

				for (int i = 1; i <= 16 && !badType; ++i)
				{
					lua_rawgeti( L, valueIndex, i ); // ..., comp

					if (lua_isnumber( L, -1 ))
					{
						matrix[i - 1] = (float)lua_tonumber( L, -1 );
					}

					else
					{
						badType = luaL_typename( L, -1 );

						CoronaLuaWarning( L, "Invalid transform component #%i: expected number, got %s", i, badType );
					}

					lua_pop( L, 1 ); // ...
				}

				if (!badType)
				{
					memcpy( _this->xform, matrix, sizeof( CoronaMatrix4x4 ) );
				}
			}

			else
			{
				CoronaLuaWarning( L, "Expected a table for transform matrix" );
			}

			*result = true;
		}
	};

	return setValueParams;
}

static void
PopulateSharedState( lua_State * L, SharedTransformableState * shared )
{
	CoronaObjectParamsHeader paramsList = {};

	DisableCullAndHitTest( paramsList );

	CoronaObjectDrawParams drawParams = DrawParams();

	AddToParamsList( paramsList, &drawParams.header, kAugmentedMethod_Draw );

	CoronaObjectSetValueParams setValueParams = SetValueParams();

	AddToParamsList( paramsList, &setValueParams.header, kAugmentedMethod_SetValue );

	CoronaObjectLifetimeParams onFinalizeParams = {};

	onFinalizeParams.action = []( const CoronaDisplayObjectHandle, void * userData )
	{
		delete static_cast< InstancedTransformableState * >( userData );
	};

	AddToParamsList( paramsList, &onFinalizeParams.header, kAugmentedMethod_OnFinalize );

	shared->params.useRef = true;
	shared->params.u.ref = CoronaObjectsBuildMethodStream( L, paramsList.next );
}

static SharedTransformableState *
InitSharedTransformableState( lua_State * L )
{
	static int sNonce;

	auto sharedState = GetOrNew< SharedTransformableState >( L, &sNonce );

	if (sharedState.isNew)
	{
		PopulateSharedState( L, sharedState.object );
	}

	return sharedState.object;
}

static InstancedTransformableState *
NewTransformableState( lua_State * L, SharedTransformableState * shared )
{
	InstancedTransformableState * stateData = new InstancedTransformableState;

	memset( stateData, 0, sizeof( InstancedTransformableState ) );

	for (int i = 0; i < 16; i += 5) // identity xform
	{
		stateData->xform[i] = 1.f;
	}

	stateData->shared = shared;

	return stateData;
}

static int
TransformableMesh( lua_State * L )
{
	SharedTransformableState * shared = InitSharedTransformableState( L );
	InstancedTransformableState * stateData = NewTransformableState( L, shared );

	return CoronaObjectsPushMesh( L, NULL, &shared->params );
}

static int
TransformablePolygon( lua_State * L )
{
	SharedTransformableState * shared = InitSharedTransformableState( L );
	InstancedTransformableState * stateData = NewTransformableState( L, shared );

	return CoronaObjectsPushPolygon( L, NULL, &shared->params );
}

int DepthLib( lua_State * L )
{
	lua_newtable( L ); // depthLib

	luaL_Reg funcs[] = {
		{ "newDepthClearObject", DepthClearObject },
		{ "newDepthStateObject", DepthStateObject },
		{ "newTransformableMesh", TransformableMesh },
		{ "newTransformablePolygon", TransformablePolygon },
		{ "register3DCustomization", Register3DCustomization },
		{ NULL, NULL }
	};

	luaL_register( L, NULL, funcs );

	return 1;
}