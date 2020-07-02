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

	for (int i = 0; i < 3; ++i)
	{
		FindAndReplace( str, "vec2 position", "vec3 position" );
	}

	for (int i = 0; i < 2; ++i)
	{
		FindAndReplace( str, "vec2 VertexKernel", "vec3 VertexKernel" );
	}

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
			CoronaLog( source );
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

	lua_pushboolean( L, CoronaShaderRegisterCustomization( L, "3D", &callbacks ) ); // ..., ok?

	return 1;
}

struct SharedTransformableState {
	CoronaMatrix4x4 prevViewMatrix, prevProjectionMatrix;
	CoronaObjectParams params;
};

struct MatrixBox { CoronaMatrix4x4 m; };
struct VectorBox { CoronaVector3 v; };

struct InstancedTransformableState {
	// ordered to usefully default when zeroed out:
	enum EulerAnglesConvention { kZYX, kXYZ, kXZY, kYXZ, kYZX, kZXY };
	enum Mode { kIdentity, kEulerAngles, kQuaternion, kTransform };

	MatrixBox * xform;
	// TODO: quaternion...
	VectorBox * eulerAngles; // yaw, pitch, roll
	SharedTransformableState * shared;
	EulerAnglesConvention convention;
	Mode mode;
};

static void
ResolveEulerAngles( InstancedTransformableState::Mode mode, CoronaVector3 eulerAngles, CoronaMatrix4x4 result )
{
	CoronaMatrix4x4 rotations[3] = {}, & X = rotations[0], & Y = rotations[1], & Z = rotations[2];
	float c1 = cos( eulerAngles[2] ), s1 = sin( eulerAngles[2] );
	float c2 = cos( eulerAngles[0] ), s2 = sin( eulerAngles[0] );
	float c3 = cos( eulerAngles[1] ), s3 = sin( eulerAngles[1] );
	int i1, i2, i3;

/*
 |  0  1  2  3 |
 |  4  5  6  7 |
 |  8  9 10 11 |
 | 12 13 14 15 |
*/

	X[15] = Y[15] = Z[15] = 1.f;
	X[0] = Y[5] = Z[10] = 1.f;

	X[5] = X[10] = c1;
	X[6] = -s1;
	X[9] = +s1;

	Y[0] = Y[10] = c2;
	Y[2] = +s2;
	Y[8] = -s2;

	Z[0] = Z[5] = c3;
	Z[1] = -s3;
	Z[4] = +s3;

	switch (mode)
	{
	case InstancedTransformableState::kXYZ:
		i1 = 0; i2 = 1; i3 = 2;

		break;
	case InstancedTransformableState::kXZY:
		i1 = 0; i2 = 2; i3 = 1;

		break;
	case InstancedTransformableState::kYXZ:
		i1 = 1; i2 = 0; i3 = 2;

		break;
	case InstancedTransformableState::kYZX:
		i1 = 1; i2 = 2; i3 = 0;

		break;
	case InstancedTransformableState::kZXY:
		i1 = 2; i2 = 0; i3 = 1;

		break;
	case InstancedTransformableState::kZYX:
		i1 = 2; i2 = 1; i3 = 0;

		break;
	}

	CoronaMatrix4x4 m23;

	CoronaMultiplyMatrix4x4( rotations[i2], rotations[i3], m23 );
	CoronaMultiplyMatrix4x4( rotations[i1], m23, result );
}

static CoronaObjectDrawParams
DrawParams()
{
	CoronaObjectDrawParams drawParams = {};

	drawParams.before = []( const CoronaDisplayObjectHandle, void * userData, CoronaRendererHandle renderer )
	{
		InstancedTransformableState * _this = static_cast< InstancedTransformableState * >( userData );

		CoronaMatrix4x4 result, temp, * pmatrix;

		switch (_this->mode)
		{
		case InstancedTransformableState::kIdentity:
			return;
		case InstancedTransformableState::kEulerAngles:
			ResolveEulerAngles( _this->mode, _this->eulerAngles->v, temp );

			pmatrix = &temp;

			break;
		case InstancedTransformableState::kQuaternion:
			// TODO!
			pmatrix = &temp;

			break;
		case InstancedTransformableState::kTransform:
			pmatrix = &_this->xform->m;

			break;
		}

		CoronaRendererGetFrustum( renderer, _this->shared->prevViewMatrix, _this->shared->prevProjectionMatrix );
		CoronaMultiplyMatrix4x4( _this->shared->prevViewMatrix, *pmatrix, result );
		CoronaRendererSetFrustum( renderer, result, _this->shared->prevProjectionMatrix );
	};

	drawParams.after = []( const CoronaDisplayObjectHandle, void * userData, CoronaRendererHandle renderer )
	{
		InstancedTransformableState * _this = static_cast< InstancedTransformableState * >( userData );

		if (_this->mode != InstancedTransformableState::kIdentity)
		{
			CoronaRendererSetFrustum( renderer, _this->shared->prevViewMatrix, _this->shared->prevProjectionMatrix );
		}
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
				if (!_this->xform)
				{
					_this->xform = new MatrixBox;
				}

				FillMatrixFromArray( L, valueIndex, "transform", _this->xform->m );

				_this->mode = InstancedTransformableState::kTransform;
			}

			else
			{
				CoronaLuaWarning( L, "Expected a table for transform matrix, got a %s", luaL_typename( L, valueIndex ) );
			}

			*result = true;
		}

		else if (strcmp( key, "yaw" ) == 0 || strcmp( key, "pitch" ) == 0 || strcmp( key, "roll" ) == 0)
		{
			if (lua_isnumber( L, valueIndex ))
			{
				if (!_this->eulerAngles)
				{
					_this->eulerAngles = new VectorBox;

					memset( _this->eulerAngles, 0, sizeof( VectorBox ) );
				}

				float angle = (float)lua_tonumber( L, valueIndex );

				switch (*key)
				{
				case 'y':
					_this->eulerAngles->v[0] = angle;

					break;
				case 'p':
					_this->eulerAngles->v[1] = angle;

					break;
				case 'r':
					_this->eulerAngles->v[2] = angle;

					break;
				}

				_this->mode = InstancedTransformableState::kEulerAngles;
			}

			else
			{
				CoronaLuaWarning( L, "Expected a number for %s Euler angle, got a %s", key, luaL_typename( L, valueIndex ) );
			}

			*result = true;
		}
	};

	return setValueParams;
}

static CoronaObjectValueParams
ValueParams()
{
	CoronaObjectValueParams valueParams = {};

	valueParams.before = []( const CoronaDisplayObjectHandle, void * userData, lua_State * L, const char key[], int * result )
	{
		InstancedTransformableState * _this = static_cast< InstancedTransformableState * >( userData );

		if (strcmp( key, "transform" ) == 0)
		{
			lua_createtable( L, 16, 0 ); // ..., transform

			if (_this->xform)
			{
				FillArrayFromMatrix( L, -1, _this->xform->m );
			}

			else
			{
				CoronaMatrix4x4 identity = {};

				identity[0] = identity[5] = identity[10] = identity[15] = 1.f;

				FillArrayFromMatrix( L, -1, identity );
			}

			*result = 1;
		}

		else if (strcmp( key, "yaw" ) == 0 || strcmp( key, "pitch" ) == 0 || strcmp( key, "roll" ) == 0)
		{
			if (_this->eulerAngles)
			{
				switch (*key)
				{
				case 'y':
					lua_pushnumber( L, _this->eulerAngles->v[0] ); // ..., yaw

					break;
				case 'p':
					lua_pushnumber( L, _this->eulerAngles->v[1] ); // ..., pitch

					break;
				case 'r':
					lua_pushnumber( L, _this->eulerAngles->v[2] ); // ..., roll

					break;
				}
			}

			else
			{
				lua_pushnumber( L, 0. ); // ..., 0
			}

			*result = 1;
		}
	};

	return valueParams;
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

	CoronaObjectValueParams valueParams = ValueParams();

	AddToParamsList( paramsList, &valueParams.header, kAugmentedMethod_Value );

	CoronaObjectLifetimeParams onFinalizeParams = {};

	onFinalizeParams.action = []( const CoronaDisplayObjectHandle, void * userData )
	{
		InstancedTransformableState * _this = static_cast< InstancedTransformableState * >( userData );

		delete _this->eulerAngles;
		delete _this->xform;
		delete _this;
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

	stateData->shared = shared;

	return stateData;
}

static int
TransformableMesh( lua_State * L )
{
	SharedTransformableState * shared = InitSharedTransformableState( L );
	InstancedTransformableState * stateData = NewTransformableState( L, shared );

	return CoronaObjectsPushMesh( L, stateData, &shared->params );
}

static int
TransformablePolygon( lua_State * L )
{
	SharedTransformableState * shared = InitSharedTransformableState( L );
	InstancedTransformableState * stateData = NewTransformableState( L, shared );

	return CoronaObjectsPushPolygon( L, stateData, &shared->params );
}

static int
PopulatePerspectiveMatrix( lua_State * L )
{
	luaL_argcheck( L, lua_istable( L, 1 ), 1, "Expected params table" );
	lua_getfield( L, 1, "result" ); // params, resultTable?

	if (!lua_istable( L, -1 ))
	{
		lua_createtable( L, 16, 0 ); // params, nil, resultTable
	}

	const char * names[] = { "fovy", "aspectRatio", "zNear", "zFar" }, * badType = NULL;
	float args[4] = {};

	for (int i = 0; i < 4 && !badType; ++i)
	{
		lua_getfield( L, 1, names[i] ); // params[, nil], resultTable, param

		if (lua_isnumber( L, -1 ))
		{
			args[i] = (float)lua_tonumber( L, -1 );
		}

		else
		{
			badType = luaL_typename( L, -1 );

			CoronaLuaWarning( L, "Expected number for projection matrix component '%s', got %s", names[i], badType );
		}

		lua_pop( L, 1 ); // params[, nil], resultTable
	}

	if (!badType)
	{
		CoronaMatrix4x4 result;

		CoronaCreatePerspectiveMatrix( args[0], args[1], args[2], args[3], result );
		FillArrayFromMatrix( L, -1, result );
	}

	else
	{
		lua_pushnil( L ); // params, ...[, nil], resultTable, nil
	}

	return 1;
}

static int
PopulateViewMatrix( lua_State * L )
{
	luaL_argcheck( L, lua_istable( L, 1 ), 1, "Expected params table" );
	lua_getfield( L, 1, "result" ); // params, resultTable?

	if (!lua_istable( L, -1 ))
	{
		lua_createtable( L, 16, 0 ); // params, nil, resultTable
	}

	const char * names[] = { "eye", "center", "up" }, * badType = NULL;
	CoronaVector3 vecs[3];

	for (int i = 0; i < 3 && !badType; ++i)
	{
		lua_getfield( L, 1, names[i] ); // params[, nil], resultTable, vec

		if (lua_istable( L, -1 ))
		{
			for (int j = 1; j <= 3 && !badType; ++j)
			{
				lua_rawgeti( L, -1, j ); // params[, nil], resultTable, vec, comp

				if (lua_isnumber( L, -1 ))
				{
					vecs[i][j - 1] = (float)lua_tonumber( L, -1 );
				}

				else
				{
					badType = luaL_typename( L, -1 );

					CoronaLuaWarning( L, "Expected number for view matrix %s component #%i, got %s", names[i], j, badType );
				}

				lua_pop( L, 1 ); // params[, nil], resultTable, vec
			}
		}

		else
		{
			badType = luaL_typename( L, -1 );

			CoronaLuaWarning( L, "Expected table for view matrix %s vector, got %s", names[i], badType );
		}

		lua_pop( L, 1 ); // params[, nil], resultTable
	}

	if (!badType)
	{
		CoronaMatrix4x4 result;

		CoronaCreateViewMatrix( vecs[0], vecs[1], vecs[2], result );
		FillArrayFromMatrix( L, -1, result );
	}

	else
	{
		lua_pushnil( L ); // params[, nil], resultTable, nil
	}

	return 1;
}

const char * FillMatrixFromArray( lua_State * L, int index, const char * what, CoronaMatrix4x4 matrix )
{
	const char * badType = NULL;
	CoronaMatrix4x4 temp;

	for (int i = 1; i <= 16 && !badType; ++i)
	{
		lua_rawgeti( L, index, i ); // ..., comp

		if (lua_isnumber( L, -1 ))
		{
			temp[i - 1] = (float)lua_tonumber( L, -1 );
		}

		else
		{
			badType = luaL_typename( L, -1 );

			CoronaLuaWarning( L, "Invalid %s component #%i: expected number, got %s", what, i, badType );
		}

		lua_pop( L, 1 ); // ...
	}

	if (!badType)
	{
		memcpy( matrix, temp, sizeof( CoronaMatrix4x4 ) );
	}

	return badType;
}

void FillArrayFromMatrix( lua_State * L, int index, const CoronaMatrix4x4 matrix )
{
	index = CoronaLuaNormalize( L, index );

	for (int i = 1; i <= 16; ++i)
	{
		lua_pushnumber( L, matrix[i - 1] ); // ..., matrixTable, ..., comp
		lua_rawseti( L, index, i ); // ..., matrixTable = { ..., comp }, ...
	}
}

int DepthLib( lua_State * L )
{
	lua_newtable( L ); // depthLib

	luaL_Reg funcs[] = {
		{ "newDepthClearObject", DepthClearObject },
		{ "newDepthStateObject", DepthStateObject },
		{ "newTransformableMesh", TransformableMesh },
		{ "newTransformablePolygon", TransformablePolygon },
		{ "populatePerspectiveMatrix", PopulatePerspectiveMatrix },
		{ "populateViewMatrix", PopulateViewMatrix },
		{ "register3DCustomization", Register3DCustomization },
		{ NULL, NULL }
	};

	luaL_register( L, NULL, funcs );

	return 1;
}