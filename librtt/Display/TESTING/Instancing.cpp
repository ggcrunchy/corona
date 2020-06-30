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
#include <string>
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
	drawParams.after = [](const CoronaShaderHandle shader, void * userData, CoronaRendererHandle renderer, const CoronaRenderDataHandle renderData)
	{	
		InstancingData * _this = static_cast< InstancingData * >( userData );

		if (_this->out && _this->count > 1U)
		{
			CoronaShaderMappingLayout srcLayout;

			srcLayout.data.count = 1U;
			srcLayout.data.offset = 0U;
			srcLayout.data.stride = 0U;
			srcLayout.data.type = kAttributeType_Float;
			srcLayout.size = sizeof( float );

			CoronaShaderRawDraw( shader, renderData, renderer );

			for (size_t i = 1; i < _this->count; ++i)
			{
				const float instance = float( i );

				CoronaGeometryCopyData( _this->out, &_this->dstLayout, &instance, &srcLayout );
				CoronaShaderRawDraw( shader, renderData, renderer );
			}

			const float zero = 0.f;

			CoronaGeometryCopyData( _this->out, &_this->dstLayout, &zero, &srcLayout );
		}

		else
		{
			CoronaShaderRawDraw( shader, renderData, renderer );
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

static std::vector< const char * > sStrings;
static std::string sUpdated;

static void
FindAndReplace( const char * original, const char * replacement )
{
	const std::string asCppString = original;
	size_t pos = sUpdated.find( asCppString );

	sUpdated.replace( pos, asCppString.size(), replacement );
}

static void
FindAndInsertAfter( const char * what, const char * toInsert )
{
	const std::string asCppString = what;
	size_t pos = sUpdated.find( asCppString );

	sUpdated.insert( pos + asCppString.size(), toInsert );
}

static void
UpdateVertexParts()
{
	FindAndReplace( "vec2 a_Position", "vec3 a_Position" );

	FindAndInsertAfter( "v_UserData = a_UserData;",

		"\n"
		"\tv_InstanceIndex = a_Position.z;\n"

	);

	FindAndReplace( "( a_Position )", "( a_Position.xy )" );
}

static void
UpdateSnippet( const char * source, bool isVertexSource )
{
	sUpdated.assign( source );

	FindAndReplace( "vec2 v_Position;",

		"float v_InstanceIndex;\n"
		"\n"
		"#define CoronaInstanceIndex int( v_InstanceIndex )\n"
		"#define CoronaInstanceFloat v_InstanceIndex\n"

	);

	if (isVertexSource)
	{
		UpdateVertexParts();
	}
}

static const char **
SourceTransformBegin( CoronaShaderSourceTransformParams * params, void * )
{
	for (size_t i = 0; i < params->nsources; ++i)
	{
		bool isVertexSource = strcmp( params->hints[i], "vertexSource" ) == 0;

		if (isVertexSource || strcmp( params->hints[i], "fragmentSource" ) == 0)
		{
			UpdateSnippet( params->sources[i], isVertexSource );

			sStrings.push_back( sUpdated.c_str() );
		}

		else
		{
			sStrings.push_back( params->sources[i] );
		}
	}

	return sStrings.data();
}

static void
ClearStrings()
{
	sStrings.clear();
	sUpdated.clear();
}

static void
SourceTransformFinish( const char * transformed[], unsigned int n, void * )
{
	ClearStrings();
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

	lua_pushboolean( L, CoronaShaderRegisterCustomization( L, "instances", &callbacks ) ); // ..., ok?

	return 1;
}

int InstancingLib( lua_State * L )
{
	ClearStrings();

	lua_newtable( L ); // instancingLib

	luaL_Reg funcs[] = {
		{ "registerCustomization", RegisterCustomization },
		{ NULL, NULL }
	};

	luaL_register( L, NULL, funcs );

	return 1;
}