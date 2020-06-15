//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "CoronaObjects.h"
#include "CoronaLua.h"

#include "Rtt_LuaContext.h"
#include "Rtt_Runtime.h"

#include "Display/Rtt_Display.h"
#include "Display/Rtt_GroupObject.h"
#include "Display/Rtt_RectObject.h"
#include "Display/Rtt_SnapshotObject.h"
#include "Display/Rtt_StageObject.h"

#include <stddef.h>

/*
	Flags canCullNotDefault : 1; // default to true or false; n.b. it is convenient to initialize structures like these with 0, but `true` is a more common default, thus the "Not"
	Flags canCullAllowEarlyOut : 1; // if this is true and we have a `beforeCanCull`, then early-out if its result is...
	Flags canCullEarlyOutResult : 1; // ...true or false, as specified here
	Flags ignoreOriginalCanCull : 1;
	Flags canCullConditionallyOverwrite : 1; // if this is true and we haven `afterCanCull`, only replace the current value if its result is...
	Flags canCullOverwriteResult : 1; // ...true or false, as specified here

	Flags canHitTestNotDefault : 1; // see canCull notes
	Flags canHitTestAllowEarlyOut : 1;
	Flags canHitTestEarlyOutResult : 1;
	Flags ignoreOriginalCanHitTest : 1;
	Flags canHitTestConditionallyOverwrite : 1;
	Flags canHitTestOverwriteResult : 1;

	Flags ignoreOriginalDidUpdateTransform : 1;
	Flags ignoreOriginalDraw : 1;
	Flags ignoreOriginalGetSelfBounds : 1;
	Flags ignoreOriginalGetSelfBoundsForAnchor : 1;

	Flags hitTestDefault : 1; // see canCull notes
	Flags hitTestAllowEarlyOut : 1;
	Flags hitTestEarlyOutResult : 1;
	Flags ignoreOriginalHitTest : 1;
	Flags hitTestConditionallyOverwrite : 1;
	Flags hitTestOverwriteResult : 1;

	Flags ignoreOriginalPrepare : 1;
	Flags ignoreOriginalRemovedFromParent : 1;
	Flags ignoreOriginalRotate : 1;
	Flags ignoreOriginalScale : 1;

	Flags setValueDisallowEarlyOut : 1; // see canCull notes
	Flags ignoreOriginalSetValue : 1;

	Flags ignoreOriginalTranslate : 1;

	Flags updateTransformDefault : 1; // see canCull notes
	Flags updateTransformAllowEarlyOut : 1;
	Flags updateTransformEarlyOutResult : 1;
	Flags updateTransformConditionallyOverwrite : 1;
	Flags updateTransformOverwriteResult : 1;

	Flags valueDisallowEarlyOut : 1; // see canCull notes
	Flags valueNotEarlyOutResult : 1;
	Flags ignoreOriginalValue : 1;
	Flags valueConditionallyOverwrite : 1;
	Flags valueOverwriteResult : 1;

	Flags ignoreOriginalWillMoveOnscreen : 1;
*/

/*
virtual void AddedToParent(lua_State * L, GroupObject * parent);
virtual bool CanCull() const;
virtual bool CanHitTest() const;
virtual void DidMoveOffscreen();
virtual void Draw(Renderer& renderer) const;
virtual void DidUpdateTransform(Matrix& srcToDst);
virtual void FinalizeSelf(lua_State *L);
virtual void GetSelfBounds(Rect& rect) const;
virtual bool HitTest(Real contentX, Real contentY);
virtual void GetSelfBoundsForAnchor(Rect& rect) const;
virtual void Prepare(const Display& display);
virtual void RemovedFromParent(lua_State * L, GroupObject * parent);
virtual void Rotate(Real deltaTheta);
virtual void Scale(Real sx, Real sy, bool isNewValue);
virtual void Translate(Real deltaX, Real deltaY);
virtual bool UpdateTransform(const Matrix& parentToDstSpace);
virtual void WillMoveOnscreen();

virtual const LuaProxyVTable& ProxyVTable() const;
*/
static bool
PushFactory( lua_State * L, const char * name )
{
    Rtt::Display & display = Rtt::LuaContext::GetRuntime( L )->GetDisplay();

    if (!display.PushObjectFactories()) // ...[, factories]
    {
        return false;
    }

    lua_getfield( L, -1, name ); // ..., factories, factory

    return true;
}

class Group2 : public Rtt::GroupObject {
public:
    Group2( Rtt_Allocator * allocator, Rtt::StageObject * stageObject );

    // BOILERPLATE
    // UNIQUE

    GroupParams fParams;
};

template<size_t offset, typename T> const DisplayObjectParams &
GetParams (const T & drawable)
{
    return *reinterpret_cast<const DisplayObjectParams *>(reinterpret_cast<const uint8_t *>(&drawable) + offset);
}
/*
static const DisplayObjectParams &
GetFromGroup (const Group2 & g)
{
    return GetParams<offsetof(Group2, fParams.inherited)>(g);
}
*/
Group2::Group2( Rtt_Allocator * allocator, Rtt::StageObject * stageObject )
    : GroupObject( allocator, stageObject )
{
}

static Rtt::GroupObject *
NewGroup2 (Rtt_Allocator * allocator, Rtt::StageObject * stageObject)
{
    return Rtt_NEW( allocator, Group2( allocator, NULL ) );
}

static bool 
CallNewFactory (lua_State * L, const char * name, void * func)
{
    int nargs = lua_gettop( L );

    if (PushFactory( L, name ) ) // args[, factories, factory]
    {
        lua_pushlightuserdata( L, func ); // args, factories, factory, func
        lua_rawseti( L, -2, lua_upvalueindex( 2 ) ); // args, factories, factory; factory.upvalue[2] = func
        lua_pushvalue( L, -1 ); // args, factories, factory, factory
        lua_insert( L, 1 ); // factory, args, factories, factory
        lua_insert( L, 1 ); // factory, factory, args, factories
        lua_pop( L, 1 ); // factory, factory, args
        lua_call( L, nargs, 1 ); // factory, object?
        lua_pushnil( L ); // factory, object?, nil
        lua_rawseti( L, -3, lua_upvalueindex( 2 ) ); // factory, object?; factory.upvalue[2] = nil

        return !lua_isnil( L, -1 );
    }

    return false;
}

CORONA_API
int CoronaObjectsPushGroup (lua_State * L, const GroupParams * params)
{
    if (CallNewFactory( L, "newGroup", &NewGroup2) ) // ...[, group]
    {
        Group2 * group = (Group2 *)lua_touserdata( L, -1 );

        group->fParams = *params;

        if (params->inherited.onCreate)
        {
            params->inherited.onCreate( group, params->inherited.userData );
        }

        return 1;
    }

    return 0;
}

CORONA_API
int CoronaObjectsPushRect (lua_State * L, const ShapeParams * params)
{
    return 0;
}

CORONA_API
int CoronaObjectsPushSnapshot (lua_State * L, const SnapshotParams * params)
{
    return 0;
}

CORONA_API
int CoronaObjectsShouldDraw (void * object, int * shouldDraw)
{
    // TODO: look for proxy on stack, validate object?
    // If so, then *shouldDraw = ((DisplayObject *)object)->shouldDraw()

    return 0;
}