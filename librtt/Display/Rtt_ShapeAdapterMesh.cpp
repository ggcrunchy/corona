//////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2018 Corona Labs Inc.
// Contact: support@coronalabs.com
//
// This file is part of the Corona game engine.
//
// Commercial License Usage
// Licensees holding valid commercial Corona licenses may use this file in
// accordance with the commercial license agreement between you and 
// Corona Labs Inc. For licensing terms and conditions please contact
// support@coronalabs.com or visit https://coronalabs.com/com-license
//
// GNU General Public License Usage
// Alternatively, this file may be used under the terms of the GNU General
// Public license version 3. The license is as published by the Free Software
// Foundation and appearing in the file LICENSE.GPL3 included in the packaging
// of this file. Please review the following information to ensure the GNU 
// General Public License requirements will
// be met: https://www.gnu.org/licenses/gpl-3.0.html
//
// For overview and more information on licensing please refer to README.md
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_ShapeAdapterMesh.h"

#include "Core/Rtt_StringHash.h"
#include "Display/Rtt_ShapePath.h"
#include "Display/Rtt_ShapeObject.h"
#include "Display/Rtt_TesselatorMesh.h"
#include "Rtt_LuaContext.h"
#include "CoronaLua.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

Geometry::PrimitiveType
ShapeAdapterMesh::GetMeshMode(lua_State *L, int index)
{
	Geometry::PrimitiveType ret = Geometry::kTriangles;
	
	if (lua_istable( L, index ) )
	{
		lua_getfield( L, index, "mode" );
		if( lua_type( L, -1 ) == LUA_TSTRING )
		{
			const char* type = lua_tostring( L, -1);
			if ( strcmp(type, "triangles" ) == 0 )
			{
				ret = Geometry::kTriangles;
			}
			else if ( strcmp(type, "strip" ) == 0 )
			{
				ret = Geometry::kTriangleStrip;
			}
			else if ( strcmp(type, "fan" ) == 0 )
			{
				ret = Geometry::kTriangleFan;
			}
			else if ( strcmp(type, "indexed" ) == 0 )
			{
				ret = Geometry::kIndexedTriangles;
			}
			else if ( strcmp(type, "lines" ) == 0 )
			{
				ret = Geometry::kLines;
			}
			else if ( strcmp(type, "lineLoop" ) == 0 )
			{
				ret = Geometry::kLineLoop;
			}
		}
		lua_pop( L, 1);
	}
	
	return ret;
}
	
bool
ShapeAdapterMesh::InitializeMesh(lua_State *L, int index, TesselatorMesh& tesselator )
{
	if ( !lua_istable( L, index ) )
	{
		return false;
	}
	index = Lua::Normalize( L, index );

	ArrayVertex2& mesh = tesselator.GetMesh();
	Rtt_ASSERT( mesh.Length() == 0 );
	lua_getfield( L, index, "vertices" );
	if (lua_istable( L, -1))
	{
		Rtt_ASSERT (lua_objlen( L, -1 ) % 2 == 0);
		U32 numVertices = (U32)lua_objlen( L, -1 )/2;
		mesh.Reserve( numVertices );
		for(U32 i=0; i<numVertices; i++)
		{
			lua_rawgeti( L, -1, 2*i+1 );
			lua_rawgeti( L, -2, 2*i+2 );
			if ( lua_type( L, -2 ) == LUA_TNUMBER &&
			     lua_type( L, -1 ) == LUA_TNUMBER )
			{
				Vertex2 v = { Rtt_FloatToReal(lua_tonumber( L, -2)), Rtt_FloatToReal(lua_tonumber( L, -1))};
				mesh.Append(v);
			}
			lua_pop( L, 2 );
		}

		Rect r;
		numVertices = mesh.Length();
		for ( U32 i = 0; i < numVertices; i++ )
		{
			r.Union( mesh[i] );
		}

		Vertex2 vertexOffset = {0, 0};

		if (!r.IsEmpty())
		{
			r.GetCenter(vertexOffset);
			for ( U32 i = 0; i < numVertices; i++ )
			{
				mesh[i].x -= vertexOffset.x;
				mesh[i].y -= vertexOffset.y;
			}
		}

		tesselator.SetVertexOffset(vertexOffset);

	}
	lua_pop( L, 1);
	
	if (mesh.Length() < 3)
	{
		CoronaLuaError( L, "display.newMesh() at least 3 pairs of (x;y) coordinates must be provided in 'vertices' parameter" );
		return false;
	}
	
	
	ArrayVertex2& UVs = tesselator.GetUV();
	lua_getfield( L, index, "uvs" );
	if (lua_istable( L, -1))
	{
		U32 numUVs = (U32)lua_objlen( L, -1 )/2;
		if ( numUVs == (U32)mesh.Length() )
		{
			UVs.Reserve( numUVs );
			for(U32 i=0; i<numUVs; i++)
			{
				lua_rawgeti( L, -1, 2*i+1 );
				lua_rawgeti( L, -2, 2*i+2 );
				if ( lua_type( L, -2 ) == LUA_TNUMBER &&
					 lua_type( L, -1 ) == LUA_TNUMBER )
				{
					Vertex2 v = { Rtt_FloatToReal(lua_tonumber( L, -2)), Rtt_FloatToReal(lua_tonumber( L, -1))};
					UVs.Append(v);
				}
				lua_pop( L, 2 );
			}
		}		
	}
	lua_pop( L, 1);
	
	int indecesStart = 1;
	lua_getfield( L, index, "zeroBasedIndices" );
	if (lua_type( L, -1) == LUA_TBOOLEAN && lua_toboolean( L, -1)) // TODO: add parsing
	{
		indecesStart = 0;
	}
	lua_pop( L, 1);
	
	TesselatorMesh::ArrayIndex& indices = tesselator.GetIndices();
	lua_getfield( L, index, "indices" );
	if (lua_istable( L, -1))
	{
		U32 numIndices = (U32)lua_objlen( L, -1 );
		indices.Reserve( numIndices );
		for(U32 i=0; i<numIndices; i++)
		{
			lua_rawgeti( L, -1, i+1 );
			if ( lua_type( L, -1 ) == LUA_TNUMBER )
			{
				U16 index = lua_tointeger(L, -1) - indecesStart;
				indices.Append(index);
			}
			lua_pop( L, 1 );
		}
	}
	lua_pop( L, 1);

	tesselator.Invalidate();
	tesselator.Update();

	return true;
}
// ----------------------------------------------------------------------------

const ShapeAdapterMesh&
ShapeAdapterMesh::Constant()
{
	static const ShapeAdapterMesh sAdapter;
	return sAdapter;
}

// ----------------------------------------------------------------------------

ShapeAdapterMesh::ShapeAdapterMesh()
:	Super( kMeshType )
{
}

StringHash *
ShapeAdapterMesh::GetHash( lua_State *L ) const
{
	static const char *keys[] = 
	{
		"setVertex",       // 0
		"getVertex",       // 1
		"setUV",		   // 2
		"getUV",           // 3
		"getVertexOffset", // 4
	// STEVE CHANGE
		"setIndex",			  // 5
		"setIndexTriangle",	  // 6
		"setVertexFillColor", // 7
		"getVertexFillColor", // 8
		"setVertexStrokeColor", // 9
		"getVertexStrokeColor"  // 10
	// /STEVE CHANGE
	};
	static StringHash sHash( *LuaContext::GetAllocator( L ), keys, sizeof( keys ) / sizeof( const char * ), /* STEVE CHANGE 5, 14, 7 */11, 18, 12, __FILE__, __LINE__ );
	return &sHash;
}

int
ShapeAdapterMesh::ValueForKey(
	const LuaUserdataProxy& sender,
	lua_State *L,
	const char *key ) const
{
	int result = 0;

	Rtt_ASSERT( key ); // Caller should check at the top-most level

	const ShapePath *path = (const ShapePath *)sender.GetUserdata();
	if ( ! path ) { return result; }

	const TesselatorMesh *tesselator =
		static_cast< const TesselatorMesh * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }

	result = 1; // Assume 1 Lua value will be pushed on the stack

	int index = GetHash( L )->Lookup( key );
	switch ( index )
	{
		case 0:
			Lua::PushCachedFunction( L, setVertex );
			break;
		case 1:
			Lua::PushCachedFunction( L, getVertex );
			break;
		case 2:
			Lua::PushCachedFunction( L, setUV );
			break;
		case 3:
			Lua::PushCachedFunction( L, getUV );
			break;
		case 4:
			Lua::PushCachedFunction( L, getVertexOffset );
			break;
		// STEVE CHANGE
		case 5:
			Lua::PushCachedFunction( L, setIndex );
			break;
		case 6:
			Lua::PushCachedFunction( L, setIndexTriangle );
			break;
		case 7:
			Lua::PushCachedFunction( L, setVertexFillColor );
			break;
		case 8:
			Lua::PushCachedFunction( L, getVertexFillColor );
			break;
		case 9:
			Lua::PushCachedFunction( L, setVertexStrokeColor );
			break;
		case 10:
			Lua::PushCachedFunction( L, getVertexStrokeColor );
			break;
		// /STEVE CHANGE
		default:
			result = Super::ValueForKey( sender, L, key );
			break;
	}

	return result;
}

int ShapeAdapterMesh::setVertex( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }
	
	ShapePath *path = (ShapePath *)sender->GetUserdata();
	if ( ! path ) { return result; }
	
	TesselatorMesh *tesselator =
	static_cast< TesselatorMesh * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }

	int vertIndex = luaL_checkint(L, nextArg++) - 1;
	Real x = luaL_checkreal( L, nextArg++ );
	Real y = luaL_checkreal( L, nextArg++ );

	if (vertIndex >= tesselator->GetMesh().Length() || vertIndex < 0)
	{
		luaL_argerror( L, 1, "index is out of bounds");
	}

	const Vertex2 &offset = tesselator->GetVertexOffset();
	x -= offset.x;
	y -= offset.y;

	Vertex2& orig = tesselator->GetMesh().WriteAccess()[vertIndex];
	
	if( !Rtt_RealEqual(x, orig.x) || !Rtt_RealEqual(y, orig.y))
	{
		orig.x = x;
		orig.y = y;

		path->Invalidate( ClosedPath::kFillSource |
						 ClosedPath::kStrokeSource );
		
		path->GetObserver()->Invalidate( DisplayObject::kGeometryFlag |
										DisplayObject::kStageBoundsFlag |
										DisplayObject::kTransformFlag );
	}

	return 0;
}
	
int ShapeAdapterMesh::getVertex( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }
	
	ShapePath *path = (ShapePath *)sender->GetUserdata();
	if ( ! path ) { return result; }
	
	TesselatorMesh *tesselator =
	static_cast< TesselatorMesh * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }
	
	int vertIndex = luaL_checkint(L, nextArg++) - 1;
	if (vertIndex >= tesselator->GetMesh().Length() || vertIndex < 0)
	{
		CoronaLuaWarning( L, "mesh:getVertex() index is out of bounds");
	}
	else
	{
		const Vertex2 &vert = tesselator->GetMesh()[vertIndex];
		const Vertex2 &offset = tesselator->GetVertexOffset();
		lua_pushnumber( L, vert.x+offset.x );
		lua_pushnumber( L, vert.y+offset.y );
		result = 2;
	}
	
	return result;
}
	
int ShapeAdapterMesh::getUV( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }
	
	ShapePath *path = (ShapePath *)sender->GetUserdata();
	if ( ! path ) { return result; }
	
	TesselatorMesh *tesselator =
	static_cast< TesselatorMesh * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }
	
	int vertIndex = luaL_checkint(L, nextArg++) - 1;
	if (vertIndex >= tesselator->GetUV().Length() || vertIndex < 0)
	{
		CoronaLuaWarning( L, "mesh:getVertex() index is out of bounds");
	}
	else
	{
		const Vertex2 &vert = tesselator->GetUV()[vertIndex];
		lua_pushnumber( L, vert.x );
		lua_pushnumber( L, vert.y );
		result = 2;
	}
	
	return result;
}

int ShapeAdapterMesh::setUV( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }
	
	ShapePath *path = (ShapePath *)sender->GetUserdata();
	if ( ! path ) { return result; }
	
	TesselatorMesh *tesselator =
	static_cast< TesselatorMesh * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }
	
	int vertIndex = luaL_checkint(L, nextArg++) - 1;
	Real u = luaL_checkreal( L, nextArg++ );
	Real v = luaL_checkreal( L, nextArg++ );
	
	if (vertIndex >= tesselator->GetUV().Length() || vertIndex < 0)
	{
		luaL_argerror( L, 1, "index is out of bounds");
	}
	
	Vertex2& uv = tesselator->GetUV().WriteAccess()[vertIndex];
	if( !Rtt_RealEqual(u, uv.x) || !Rtt_RealEqual(v, uv.y))
	{
		uv.x = u;
		uv.y = v;
		
		path->Invalidate( ClosedPath::kFillSourceTexture );

		path->GetObserver()->Invalidate( DisplayObject::kGeometryFlag );
	}
	return 0;
}


int ShapeAdapterMesh::getVertexOffset( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }

	ShapePath *path = (ShapePath *)sender->GetUserdata();
	if ( ! path ) { return result; }

	TesselatorMesh *tesselator =
	static_cast< TesselatorMesh * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }

	const Vertex2 &vert = tesselator->GetVertexOffset();
	lua_pushnumber( L, vert.x );
	lua_pushnumber( L, vert.y );
	result = 2;

	
	return result;
}

// STEVE CHANGE
int ShapeAdapterMesh::setIndex( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }
	
	ShapePath *path = (ShapePath *)sender->GetUserdata();
	if ( ! path ) { return result; }
	
	TesselatorMesh *tesselator =
	static_cast< TesselatorMesh * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }
	
	int indexIndex = luaL_checkint(L, nextArg++) - 1;
	U16 index = luaL_checkinteger( L, nextArg++ ) - 1; // 1-based
	
	if (indexIndex >= tesselator->GetIndices().Length() || indexIndex < 0)
	{
		luaL_argerror( L, 1, "mesh:setIndex() index of index is out of bounds");
	}

	if (index >= tesselator->GetMesh().Length())
	{
		luaL_argerror( L, 1, "mesh:setIndex() index is out of bounds");
	}
	
	U16& windex = tesselator->GetIndices().WriteAccess()[indexIndex];
	if( windex != index)
	{
		windex = index;
		
		path->Invalidate( ClosedPath::kFillSourceIndices );

		path->GetObserver()->Invalidate( DisplayObject::kGeometryFlag |
										DisplayObject::kStageBoundsFlag |
										DisplayObject::kTransformFlag );
	}
	return 0;
}

int ShapeAdapterMesh::setIndexTriangle( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }
	
	ShapePath *path = (ShapePath *)sender->GetUserdata();
	if ( ! path ) { return result; }
	
	TesselatorMesh *tesselator =
	static_cast< TesselatorMesh * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }
	
	int triIndex = luaL_checkint(L, nextArg++) - 1;
	U16 i1 = luaL_checkinteger( L, nextArg++ ) - 1; // 1-based
	U16 i2 = luaL_checkinteger( L, nextArg++ ) - 1; // 1-based
	U16 i3 = luaL_checkinteger( L, nextArg++ ) - 1; // 1-based
	
	if (triIndex * 3 >= tesselator->GetIndices().Length() || triIndex < 0)
	{
		luaL_argerror( L, 1, "mesh:setIndexTriangle() index of triangle is out of bounds");
	}

	U16 count = (U16)tesselator->GetMesh().Length();
	if (i1 >= count || i2 >= count || i3 >= count)
	{
		luaL_argerror( L, 1, "mesh:setIndexTriangle() index is out of bounds");
	}
	
	U16 * tindices = &tesselator->GetIndices().WriteAccess()[triIndex * 3];
	if( tindices[0] != i1 || tindices[1] != i2 || tindices[2] != i3 )
	{
		tindices[0] = i1;
		tindices[1] = i2;
		tindices[2] = i3;
		
		path->Invalidate( ClosedPath::kFillSourceIndices );

		path->GetObserver()->Invalidate( DisplayObject::kGeometryFlag |
										DisplayObject::kStageBoundsFlag |
										DisplayObject::kTransformFlag );
	}
	return 0;
}

inline bool SetColor( lua_State *L, S32 n, Array<Color> &colors, int index, int nextArg )
{
	if (colors.Length() == 0)
	{
		ShapePath::InitColors( colors, n );
	}

	ColorUnion u;
	Color old = colors[index];

	u.rgba.r = (U8)(255. * luaL_optnumber( L, nextArg++, 1. ));
	u.rgba.g = (U8)(255. * luaL_optnumber( L, nextArg++, 1. ));
	u.rgba.b = (U8)(255. * luaL_optnumber( L, nextArg++, 1. ));
	u.rgba.a = (U8)(255. * luaL_optnumber( L, nextArg++, 1. ));

	bool changed = u.pixel != old;

	if (changed)
	{
		colors[index] = u.pixel;
	}

	return changed;
}

int ShapeAdapterMesh::setVertexFillColor( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }
	
	ShapePath *path = (ShapePath *)sender->GetUserdata();
	if ( ! path ) { return result; }
	
	TesselatorMesh *tesselator =
	static_cast< TesselatorMesh * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }
	
	int vertIndex = luaL_checkint(L, nextArg++) - 1;
	S32 n = tesselator->GetMesh().Length();
	
	if (vertIndex >= n || vertIndex < 0)
	{
		luaL_argerror( L, 1, "mesh:setVertexFillColor() index is out of bounds");
	}
	
	if (SetColor( L, n, path->GetFillColors(), vertIndex, nextArg ))
	{
	//	path->Invalidate( ClosedPath::kFillSourceTexture );

		path->GetObserver()->Invalidate( DisplayObject::kGeometryFlag | DisplayObject::kColorFlag );
	}
	return 0;
}

inline int GetColor( lua_State *L, const Array<Color> &colors, int index )
{
	ColorUnion u;
	u.pixel = colors.Length() > 0 ? colors[index] : ColorWhite();

	lua_createtable( L, 0, 4 );
	lua_pushnumber( L, (float)u.rgba.r / 255.f );
	lua_setfield( L, -2, "r" );
	lua_pushnumber( L, (float)u.rgba.g / 255.f );
	lua_setfield( L, -2, "g" );
	lua_pushnumber( L, (float)u.rgba.b / 255.f );
	lua_setfield( L, -2, "b" );
	lua_pushnumber( L, (float)u.rgba.a / 255.f );
	lua_setfield( L, -2, "a" );

	return 1;
}

int ShapeAdapterMesh::getVertexFillColor( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }
	
	ShapePath *path = (ShapePath *)sender->GetUserdata();
	if ( ! path ) { return result; }
	
	TesselatorMesh *tesselator =
	static_cast< TesselatorMesh * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }
	
	int vertIndex = luaL_checkint(L, nextArg++) - 1;
	if (vertIndex >= tesselator->GetMesh().Length() || vertIndex < 0)
	{
		CoronaLuaWarning( L, "mesh:getVertexFillColor() index is out of bounds");
	}
	else
	{
		result = GetColor( L, path->GetFillColors(), vertIndex );
	}
	
	return result;
}

int ShapeAdapterMesh::setVertexStrokeColor( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }
	
	ShapePath *path = (ShapePath *)sender->GetUserdata();
	if ( ! path ) { return result; }
	
	TesselatorMesh *tesselator =
	static_cast< TesselatorMesh * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }
	
	int vertIndex = luaL_checkint(L, nextArg++) - 1;
	S32 n = tesselator->GetMesh().Length();
	
	if (vertIndex >= n || vertIndex < 0)
	{
		luaL_argerror( L, 1, "mesh:setVertexStrokeColor() index is out of bounds");
	}
	
	if (SetColor( L, n, path->GetStrokeColors(), vertIndex, nextArg ))
	{
	//	path->Invalidate( ClosedPath::kFillSourceTexture );

		path->GetObserver()->Invalidate( DisplayObject::kGeometryFlag | DisplayObject::kColorFlag );
	}
	return 0;
}

int ShapeAdapterMesh::getVertexStrokeColor( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }
	
	ShapePath *path = (ShapePath *)sender->GetUserdata();
	if ( ! path ) { return result; }
	
	TesselatorMesh *tesselator =
	static_cast< TesselatorMesh * >( path->GetTesselator() );
	if ( ! tesselator ) { return result; }
	
	int vertIndex = luaL_checkint(L, nextArg++) - 1;
	if (vertIndex >= tesselator->GetMesh().Length() || vertIndex < 0)
	{
		CoronaLuaWarning( L, "mesh:getVertexStrokeColor() index is out of bounds");
	}
	else
	{
		result = GetColor( L, path->GetStrokeColors(), vertIndex );
	}
	
	return result;
}
// /STEVE CHANGE

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

