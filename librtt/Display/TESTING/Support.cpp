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
#include "Renderer/Rtt_GL.h"

static int
ScopeGroupObject( lua_State * L )
{
	static U32 sScopeDrawSessionID;

	auto params = GetOrNew< CoronaObjectParams >( L, &sScopeDrawSessionID );

	if (params.isNew)
	{
	//	params->afterDidInsert = [](void * groupObject, void * userData, int childParentChanged ) {}
	//	params->afterDidRemove = [](void * groupObject, void * userData ) {}
	// ^^ TODO: double-check these
		CoronaObjectDrawParams drawParams = {};

		drawParams.before = [](const CoronaDisplayObjectHandle object, void * userData, CoronaRendererHandle rendererHandle)
		{
			const CoronaGroupObjectHandle groupObject = reinterpret_cast< CoronaGroupObjectHandle >( object );

			ScopeMessagePayload payload = { rendererHandle, sScopeDrawSessionID };

			for (int i = 0, n = CoronaGroupObjectGetNumChildren( groupObject ); i < n; ++i)
			{
				CoronaObjectSendMessage( CoronaGroupObjectGetChild( groupObject, i ), "willDraw", &payload, sizeof( ScopeMessagePayload ) );
			}
		};

		drawParams.after = [](const CoronaDisplayObjectHandle object, void * userData, CoronaRendererHandle rendererHandle)
		{
			const CoronaGroupObjectHandle groupObject = reinterpret_cast< CoronaGroupObjectHandle >( object );

			ScopeMessagePayload payload = { rendererHandle, sScopeDrawSessionID++ };

			for (int i = CoronaGroupObjectGetNumChildren( groupObject ); i; --i)
			{
				CoronaObjectSendMessage( CoronaGroupObjectGetChild( groupObject, i - 1 ), "didDraw", &payload, sizeof( ScopeMessagePayload ) );
			}
		};

		drawParams.header.method = kAugmentedMethod_Draw;

		params.object->useRef = true;
		params.object->u.ref = CoronaObjectsBuildMethodStream( L, &drawParams.header );
	}

	return CoronaObjectsPushGroup( L, nullptr, params.object ); // ...[, scopeGroup]
}

void
EarlyOutPredicate( const CoronaDisplayObjectHandle, void *, int * result )
{
	*result = false;
}

void
AddToParamsList( CoronaObjectParamsHeader & head, CoronaObjectParamsHeader * params, unsigned short method )
{
	params->next = head.next;
	params->method = method;
	head.next = params;
}

void
DisableCullAndHitTest( CoronaObjectParamsHeader & head )
{
	static CoronaObjectBooleanResultParams canCull, canHitTest;

	canCull.before = canHitTest.before = EarlyOutPredicate;

	AddToParamsList( head, &canCull.header, kAugmentedMethod_CanCull );
	AddToParamsList( head, &canHitTest.header, kAugmentedMethod_CanHitTest );
}

void
CopyWriter( U8 * out, const void * data, U32 size )
{
	memcpy( out, data, size );
}

void
DummyWriter( U8 *, const void *, U32 )
{
}

void
DummyArgs( lua_State * L )
{
	lua_settop( L, lua_istable( L, 1 ) ); // [group]
	lua_pushinteger( L, 0 ); // [group, ]x
	lua_pushinteger( L, 0 ); // [group, ]x, y
	lua_pushinteger( L, 1 ); // [group, ]x, y, w
	lua_pushinteger( L, 1 ); // [group, ]x, y, w, h
}

int
FindName( lua_State * L, int valueIndex, const char * list[] )
{
	const char * name = lua_tostring( L, valueIndex );
	int index = 0;

	while (list[index] && strcmp( list[index], name ) != 0 )
	{
		++index;
	}

	return index;
}

bool FindFunc( lua_State * L, int valueIndex, int * func )
{
	const char * names[] = { "never", "less", "equal", "greater", "greaterThanOrEqual", "lessThanOrEqual", "notEqual", "always", NULL };
	int index = FindName( L, valueIndex, names );

	if (names[index])
	{
		const GLenum funcs[] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_GREATER, GL_GEQUAL, GL_LEQUAL, GL_NOTEQUAL, GL_ALWAYS };

		*func = funcs[index];

		return true;
	}

	return false;
}

int
SupportLib( lua_State * L)
{
	lua_newtable( L ); // supportLib

	luaL_Reg funcs[] = {
		{ "newScopeGroupObject", ScopeGroupObject },
		{ NULL, NULL }
	};

	luaL_register( L, NULL, funcs );

	return 1;
}