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

int StencilLib( lua_State * L )
{
	lua_newtable( L ); // stencilLib

	luaL_Reg funcs[] = {
		{ "newStencilClearObject", StencilClearObject },
		{ "newStencilStateObject", StencilStateObject },
		{ NULL, NULL }
	};

	luaL_register( L, NULL, funcs );

	return 1;
}
