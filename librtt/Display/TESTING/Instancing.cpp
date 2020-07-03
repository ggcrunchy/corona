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
#include <vector>

struct InstancingData {
	CoronaShaderMappingLayout dstLayout;
	void * out;
	int count;
};

static int
GetDataIndex( const char * name )
{
	if (strcmp( name, "instanceCount" ) == 0)
	{
		return 0;
	}

	return -1;
}

static int
GetData( lua_State * L, int dataIndex, void * userData, int * pushedError )
{
	if (0 == dataIndex)
	{
		InstancingData * _this = static_cast< InstancingData * >( userData );

		lua_pushinteger( L, _this->count ); // ..., count

		return 1;
	}

	else
	{
		lua_pushliteral( L, "getData given bad index: %i", dataIndex ); // ..., err

		*pushedError = true;

		return 0;
	}
}

static int
SetData( lua_State * L, int dataIndex, int valueIndex, void * userData, int * pushedError )
{
	int instanceCount = lua_tointeger( L, valueIndex );

	if (0 == dataIndex && instanceCount >= 0)
	{
		InstancingData * _this = static_cast< InstancingData * >( userData );

		_this->count = instanceCount;

		return 1;
	}

	else
	{
		if (dataIndex)
		{
			lua_pushfstring( L, "setData given bad index: %i", dataIndex ); // ..., err
		}

		else
		{
			lua_pushfstring( L, "Invalid instance count: %i", instanceCount ); // ..., err
		}

		*pushedError = true;

		return 0;
	}
}

static CoronaShaderDrawParams
DrawParams()
{
	CoronaShaderDrawParams drawParams = {};

	drawParams.ignoreOriginal = true;
	drawParams.after = []( const CoronaShaderHandle shader, void * userData, CoronaRendererHandle renderer, const CoronaRenderDataHandle renderData )
	{	
		InstancingData * _this = static_cast< InstancingData * >( userData );

		CoronaShaderRawDraw( shader, renderData, renderer ); // first / only instance

		if (_this->out && _this->count > 1U)
		{
			CoronaShaderMappingLayout srcLayout;

			srcLayout.data.count = 1U;
			srcLayout.data.offset = 0U;
			srcLayout.data.stride = 0U;
			srcLayout.data.type = kAttributeType_Float;
			srcLayout.size = sizeof( float );

			for (size_t i = 1; i < _this->count; ++i)
			{
				const float instance = float( i );

				CoronaGeometryCopyData( _this->out, &_this->dstLayout, &instance, &srcLayout ); // update instance index
				CoronaShaderRawDraw( shader, renderData, renderer ); // instance #2 and up
			}

			const float zero = 0.f;

			CoronaGeometryCopyData( _this->out, &_this->dstLayout, &zero, &srcLayout ); // restore instance index to 0
		}
	};

	return drawParams;
}

static void
Prepare( const CoronaShaderHandle shader, void * userData, CoronaRenderDataHandle renderData, int w, int h, int mod )
{
	InstancingData * _this = static_cast< InstancingData * >( userData );

	_this->out = NULL;

	CoronaShaderSourceTransformDetail detail;
		
	for (int i = 0; CoronaShaderGetSourceTransformDetail( shader, i, &detail ); ++i)
	{
		if (strcmp( detail.name, "supportsInstancing" ) == 0)
		{
			_this->out = CoronaGeometryGetMappingFromRenderData( renderData, detail.value, &_this->dstLayout );
		}
	}
}

struct TransformData {
	char * newString;
	const char ** stringList;
};

static void
UpdateVertexSpecificSource( std::string & str )
{
	FindAndReplace( str, "vec2 a_Position", "vec3 a_Position" );

	FindAndInsertAfter( str,
		// Find this...
		"v_UserData = a_UserData;",

		// ...then insert this after:
		"\n"
		"\tv_InstanceIndex = a_Position.z;\n"

	);

	FindAndReplace( str, "( a_Position )", "( a_Position.xy )" );
}

static std::string
UpdateSource( std::string str, bool isVertexSource )
{
	FindAndReplace( str,
		// Find this...
		"vec2 v_Position;",

		// ...and replace with:
		"float v_InstanceIndex;\n"
		"\n"
		"#define CoronaInstanceIndex int( v_InstanceIndex )\n"
		"#define CoronaInstanceFloat v_InstanceIndex\n"

	);

	if (isVertexSource)
	{
		UpdateVertexSpecificSource( str );
	}

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
		bool isVertexSource = strcmp( params->hints[i], "vertexSource" ) == 0;

		if (isVertexSource || strcmp( params->hints[i], "fragmentSource" ) == 0)
		{
			std::string updated = UpdateSource( source, isVertexSource );

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
RegisterCustomization( lua_State * L)
{
	CoronaShaderCallbacks callbacks = {};

	callbacks.size = sizeof( CoronaShaderCallbacks );
	callbacks.extraSpace = sizeof( InstancingData );
	callbacks.getDataIndex = GetDataIndex;
	callbacks.getData = GetData;
	callbacks.setData = SetData;
	callbacks.drawParams = DrawParams();
	callbacks.prepare = Prepare;
	callbacks.transform.begin = SourceTransformBegin;
	callbacks.transform.finish = SourceTransformFinish;
	callbacks.transform.extraSpace = sizeof( TransformData );

	lua_pushboolean( L, CoronaShaderRegisterCustomization( L, "instances", &callbacks ) ); // ..., ok?

	return 1;
}

int InstancingLib( lua_State * L )
{
	lua_newtable( L ); // instancingLib

	luaL_Reg funcs[] = {
		{ "registerCustomization", RegisterCustomization },
		{ NULL, NULL }
	};

	luaL_register( L, NULL, funcs );

	return 1;
}