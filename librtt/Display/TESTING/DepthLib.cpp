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

int DepthLib( lua_State * L )
{
	lua_newtable( L ); // depthLib

	luaL_Reg funcs[] = {
		{ "newDepthClearObject", DepthClearObject },
		{ "newDepthStateObject", DepthStateObject },
		{ NULL, NULL }
	};

	luaL_register( L, NULL, funcs );

	return 1;
}