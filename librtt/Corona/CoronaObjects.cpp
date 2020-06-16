//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "CoronaObjects.h"
#include "CoronaLua.h"

#include "Rtt_LuaContext.h"
#include "Rtt_LuaProxyVTable.h"
#include "Rtt_Runtime.h"

#include "Display/Rtt_Display.h"
#include "Display/Rtt_GroupObject.h"
#include "Display/Rtt_RectObject.h"
#include "Display/Rtt_SnapshotObject.h"
#include "Display/Rtt_StageObject.h"

static bool
ValuePrologue( lua_State * L, const Rtt::MLuaProxyable& object, const char key[], void * userData, const DisplayObjectParams & params, int * result )
{
	if (params.beforeValue)
	{
		params.beforeValue( &object, userData, L, key, result );

		bool canEarlyOut = !params.valueDisallowEarlyOut, expectsNonZero = !params.valueEarlyOutIfZero;

		if (canEarlyOut && expectsNonZero == !!result)
		{
			return false;
		}
	}

	return true;
}

static int
ValueEpilogue( lua_State * L, const Rtt::MLuaProxyable& object, const char key[], void * userData, const DisplayObjectParams & params, int result )
{
	if (params.afterValue)
	{
		params.afterValue( &object, userData, L, key, &result ); // n.b. `result` previous values still on stack
	}

	return result;
}

static bool
SetValuePrologue( lua_State * L, Rtt::MLuaProxyable& object, const char key[], int valueIndex, void * userData, const DisplayObjectParams & params, int * result )
{
	if (params.beforeSetValue)
	{
		params.beforeSetValue( &object, userData, L, key, valueIndex, result );

		bool canEarlyOut = !params.setValueDisallowEarlyOut;

		if (canEarlyOut && result)
		{
			return false;
		}
	}

	return true;
}

static bool
SetValueEpilogue( lua_State * L, Rtt::MLuaProxyable& object, const char key[], int valueIndex, void * userData, const DisplayObjectParams & params, int result )
{
	if (params.afterSetValue)
	{
		params.afterSetValue( &object, userData, L, key, valueIndex, &result );
	}

	return result;
}

#define CORONA_OBJECTS_VTABLE(OBJECT_KIND, TO_PARAMS)  \
								 					   \
class OBJECT_KIND##2ProxyVTable : public Rtt::Lua##OBJECT_KIND##ObjectProxyVTable \
{																	              \
public:																			  \
	typedef OBJECT_KIND##2ProxyVTable Self;				\
	typedef Lua##OBJECT_KIND##ObjectProxyVTable Super;  \
														\
public:																			 \
	static const Self& Constant() { static const Self kVTable; return kVTable; } \
																				 \
protected:							\
	OBJECT_KIND##2ProxyVTable() {}	\
									\
public:																																	\
	virtual int ValueForKey( lua_State *L, const Rtt::MLuaProxyable& object, const char key[], bool overrideRestriction = false ) const \
	{																																	\
		const DisplayObjectParams & params = static_cast<const OBJECT_KIND##2 &>(object).TO_PARAMS;		\
		void * userData = const_cast<void *>(static_cast<const OBJECT_KIND##2 &>(object).fUserData);	\
		int result = 0;																					\
																										\
		if (!ValuePrologue( L, object, key, userData, params, &result ))	\
		{																	\
			return result;													\
		}																	\
																			\
		else if (!params.ignoreOriginalValue)												\
		{																					\
			result += Super::Constant().ValueForKey( L, object, key, overrideRestriction );	\
		}																					\
																							\
		return ValueEpilogue( L, object, key, userData, params, result );	\
	}																		\
																			\
	virtual bool SetValueForKey( lua_State *L, Rtt::MLuaProxyable& object, const char key[], int valueIndex ) const	\
	{																												\
		const DisplayObjectParams & params = static_cast<OBJECT_KIND##2 &>(object).TO_PARAMS;	\
		void * userData = static_cast<const OBJECT_KIND##2 &>(object).fUserData;				\
		int result = 0;																			\
																								\
		if (!SetValuePrologue( L, object, key, valueIndex, userData, params, &result ))	\
		{																				\
			return result;																\
		}																				\
																						\
		else if (!params.ignoreOriginalSetValue)									\
		{																			\
			result = Super::Constant().SetValueForKey( L, object, key, valueIndex );\
		}																			\
																					\
		return SetValueEpilogue( L, object, key, valueIndex, userData, params, result );	\
	}																						\
																							\
	virtual const LuaProxyVTable& Parent() const { return Super::Constant(); }	\
}

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

static void *
CopyParams( lua_State * L, const void * from, size_t size, bool isTemporary, int & ref )
{
	if (isTemporary)
	{
		void * out = lua_newuserdata( L, size );

		memcpy( out, from, size );

		ref = luaL_ref( L, LUA_REGISTRYINDEX );

		return out;
	}

	return const_cast<void *>(from);
}

#define CORONA_OBJECTS_PUSH(OBJECT_KIND, PARAMS_KIND, TO_MEMBERS)							\
	if (CallNewFactory( L, "new" #OBJECT_KIND, &New##OBJECT_KIND##2) ) /* ...[, object] */	\
	{																			\
		OBJECT_KIND##2 * object = (OBJECT_KIND##2 *)lua_touserdata( L, -1 );	\
																				\
		object->fParams = (PARAMS_KIND *)CopyParams( L, params, sizeof(*params), temporaryParams, object->fRef );	\
		object->fUserData = userData;																				\
																													\
		if (TO_MEMBERS.onCreate)	\
		{							\
			TO_MEMBERS.onCreate( object, userData );	\
		}												\
					\
		return 1;	\
	}				\
					\
	return 0

#define FIRST_ARGS this, fUserData
#define CORONA_OBJECTS_METHOD_BOOKEND(WHEN, METHOD_NAME, ...)	\
	if (params.WHEN##METHOD_NAME)								\
	{															\
		params.WHEN##METHOD_NAME( __VA_ARGS__ );				\
	}

#define CORONA_OBJECTS_METHOD_CORE(METHOD_NAME)	\
	if (!params.ignoreOriginal##METHOD_NAME)	\
	{											\
		Super::METHOD_NAME();					\
	}

#define CORONA_OBJECTS_METHOD_CORE_WITH_RESULT(METHOD_NAME)	\
	if (!params.ignoreOriginal##METHOD_NAME)				\
	{														\
		result = Super::METHOD_NAME();						\
	}

#define CORONA_OBJECTS_METHOD_CORE_WITH_ARGS(METHOD_NAME, ...)	\
	if (!params.ignoreOriginal##METHOD_NAME)					\
	{															\
		Super::METHOD_NAME( __VA_ARGS__ );						\
	}

#define CORONA_OBJECTS_METHOD_CORE_WITH_ARGS_AND_RESULT(METHOD_NAME, ...)	\
	if (!params.ignoreOriginal##METHOD_NAME)								\
	{																		\
		result = Super::METHOD_NAME( __VA_ARGS__ );							\
	}

#define CORONA_OBJECTS_METHOD(METHOD_NAME, TO_PARAMS)					\
	const DisplayObjectParams & params = TO_PARAMS;						\
																		\
	CORONA_OBJECTS_METHOD_BOOKEND( before, METHOD_NAME, FIRST_ARGS )	\
	CORONA_OBJECTS_METHOD_CORE( METHOD_NAME )							\
	CORONA_OBJECTS_METHOD_BOOKEND( after, METHOD_NAME, FIRST_ARGS)

#define CORONA_OBJECTS_METHOD_STRIP_ARGUMENT(METHOD_NAME, TO_PARAMS, ARGUMENT)	\
	const DisplayObjectParams & params = TO_PARAMS;								\
																		\
	CORONA_OBJECTS_METHOD_BOOKEND( before, METHOD_NAME, FIRST_ARGS )	\
	CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( METHOD_NAME, ARGUMENT )		\
	CORONA_OBJECTS_METHOD_BOOKEND( after, METHOD_NAME, FIRST_ARGS)

#define CORONA_OBJECTS_METHOD_WITH_ARGS(METHOD_NAME, TO_PARAMS, ...)	\
	const DisplayObjectParams & params = TO_PARAMS;						\
																		\
	CORONA_OBJECTS_METHOD_BOOKEND( before, METHOD_NAME, FIRST_ARGS, __VA_ARGS__ )	\
	CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( METHOD_NAME, __VA_ARGS__ )				\
	CORONA_OBJECTS_METHOD_BOOKEND( after, METHOD_NAME, FIRST_ARGS, __VA_ARGS__ )

#define CORONA_OBJECTS_EARLY_OUT_IF_APPROPRIATE(METHOD_NAME)		\
	bool expectZeroResult = params.earlyOut##METHOD_NAME##IfZero;	\
																	\
	if (params.before##METHOD_NAME && expectZeroResult == !result)	\
	{																\
		return !!result;											\
	}

#define CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT(METHOD_NAME, ...) \
	int result = 0;	\
					\
	CORONA_OBJECTS_METHOD_BOOKEND( before, METHOD_NAME, __VA_ARGS__, &result )	\
	CORONA_OBJECTS_EARLY_OUT_IF_APPROPRIATE( METHOD_NAME )

static void
Copy3 (float * dst, const float * src)
{
	static_assert(sizeof(float) == sizeof(Rtt::Real), "Incompatible real type");

	memcpy(dst, src, 3 * sizeof(float));
}

#define CORONA_OBJECTS_MATRIX_BOOKEND_METHOD(WHEN, METHOD_NAME)	\
	if (params.WHEN##METHOD_NAME)								\
	{															\
		Rtt::Real matrix[6];				\
											\
		Copy3(matrix, srcToDst.Row0());		\
		Copy3(matrix + 3, srcToDst.Row1());	\
											\
		params.WHEN##METHOD_NAME(FIRST_ARGS, matrix);				\
																	\
		Copy3(const_cast<float *>(srcToDst.Row0()), matrix);		\
		Copy3(const_cast<float *>(srcToDst.Row1()), matrix + 3);	\
	}

class Group2 : public Rtt::GroupObject {
public:
	typedef Group2 Self;
	typedef Rtt::GroupObject Super;

public:

	Group2( Rtt_Allocator * allocator, Rtt::StageObject * stageObject );

	// Didinsert
	// DidRemove

	#define GROUP2_MEMBERS fParams->inherited

	virtual void AddedToParent( lua_State * L, GroupObject * parent )
	{
		CORONA_OBJECTS_METHOD_WITH_ARGS( AddedToParent, GROUP2_MEMBERS, L, parent )
	}

	virtual bool CanCull() const
	{
		const DisplayObjectParams & params = GROUP2_MEMBERS;

		CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT( CanCull, FIRST_ARGS )
		CORONA_OBJECTS_METHOD_CORE_WITH_RESULT( CanCull )
		CORONA_OBJECTS_METHOD_BOOKEND( after, CanCull, FIRST_ARGS, &result )

		return result;
	}

	virtual bool CanHitTest() const
	{
		const DisplayObjectParams & params = GROUP2_MEMBERS;

		CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT( CanHitTest, FIRST_ARGS )
		CORONA_OBJECTS_METHOD_CORE_WITH_RESULT( CanHitTest )
		CORONA_OBJECTS_METHOD_BOOKEND( after, CanHitTest, FIRST_ARGS, &result )

		return result;
	}

	virtual void DidMoveOffscreen()
	{
		CORONA_OBJECTS_METHOD( DidMoveOffscreen, GROUP2_MEMBERS )
	}

	virtual void DidUpdateTransform( Rtt::Matrix & srcToDst )
	{
		const DisplayObjectParams & params = GROUP2_MEMBERS;

		CORONA_OBJECTS_MATRIX_BOOKEND_METHOD( before, DidUpdateTransform )
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( DidUpdateTransform, srcToDst )
		CORONA_OBJECTS_MATRIX_BOOKEND_METHOD( after, DidUpdateTransform )
	}

	virtual void Draw( Rtt::Renderer & renderer ) const
	{
		CORONA_OBJECTS_METHOD_STRIP_ARGUMENT( Draw, GROUP2_MEMBERS, renderer )
	}

	virtual void FinalizeSelf( lua_State * L )
	{
		if (GROUP2_MEMBERS.onFinalize)
		{
			GROUP2_MEMBERS.onFinalize( this, fUserData );
		}

		lua_unref( L, fRef );

		Super::FinalizeSelf( L );
	}

	virtual void GetSelfBounds( Rtt::Rect & rect ) const
	{
		const DisplayObjectParams & params = GROUP2_MEMBERS;

		CORONA_OBJECTS_METHOD_BOOKEND( before, GetSelfBounds, FIRST_ARGS, &rect.xMin, &rect.yMin, &rect.xMax, &rect.yMax )
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( GetSelfBounds, rect )
		CORONA_OBJECTS_METHOD_BOOKEND( after, GetSelfBounds, FIRST_ARGS, &rect.xMin, &rect.yMin, &rect.xMax, &rect.yMax )
	}

	virtual void GetSelfBoundsForAnchor( Rtt::Rect & rect ) const
	{
		const DisplayObjectParams & params = GROUP2_MEMBERS;

		CORONA_OBJECTS_METHOD_BOOKEND( before, GetSelfBoundsForAnchor, FIRST_ARGS, &rect.xMin, &rect.yMin, &rect.xMax, &rect.yMax )
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( GetSelfBoundsForAnchor, rect )
		CORONA_OBJECTS_METHOD_BOOKEND( after, GetSelfBoundsForAnchor, FIRST_ARGS, &rect.xMin, &rect.yMin, &rect.xMax, &rect.yMax )
	}

	virtual bool HitTest( Rtt::Real contentX, Rtt::Real contentY )
	{
		const DisplayObjectParams & params = GROUP2_MEMBERS;

		CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT( HitTest, FIRST_ARGS, contentX, contentY )
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS_AND_RESULT( HitTest, contentX, contentY )
		CORONA_OBJECTS_METHOD_BOOKEND( after, HitTest, FIRST_ARGS, contentX, contentY, &result )

		return result;
	}

	virtual void Prepare( const Rtt::Display & display )
	{
		CORONA_OBJECTS_METHOD_STRIP_ARGUMENT( Prepare, GROUP2_MEMBERS, display )
	}

	virtual void RemovedFromParent( lua_State * L, GroupObject * parent )
	{
		CORONA_OBJECTS_METHOD_WITH_ARGS( RemovedFromParent, GROUP2_MEMBERS, L, parent )
	}

	virtual void Rotate( Rtt::Real deltaTheta )
	{
		CORONA_OBJECTS_METHOD_WITH_ARGS( Rotate, GROUP2_MEMBERS, deltaTheta )
	}

	virtual void Scale( Rtt::Real sx, Rtt::Real sy, bool isNewValue )
	{
		CORONA_OBJECTS_METHOD_WITH_ARGS( Scale, GROUP2_MEMBERS, sx, sy, isNewValue )
	}

	virtual void Translate( Rtt::Real deltaX, Rtt::Real deltaY )
	{
		CORONA_OBJECTS_METHOD_WITH_ARGS( Translate, GROUP2_MEMBERS, deltaX, deltaY )
	}

	virtual bool UpdateTransform( const Rtt::Matrix & parentToDstSpace )
	{
		float matrix[6];

		Copy3( matrix, parentToDstSpace.Row0() );
		Copy3( matrix + 3, parentToDstSpace.Row1() );

		const DisplayObjectParams & params = GROUP2_MEMBERS;

		CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT( UpdateTransform, FIRST_ARGS, matrix )
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS_AND_RESULT( UpdateTransform, parentToDstSpace )
		CORONA_OBJECTS_METHOD_BOOKEND( after, UpdateTransform, FIRST_ARGS, matrix, &result )

		return result;
	}

	virtual void WillMoveOnscreen()
	{
		CORONA_OBJECTS_METHOD( WillMoveOnscreen, GROUP2_MEMBERS )
	}

	virtual const Rtt::LuaProxyVTable& ProxyVTable() const;

	GroupParams * fParams;
	int fRef;
	void * fUserData;

	#undef GROUP2_MEMBERS
};

Group2::Group2( Rtt_Allocator * allocator, Rtt::StageObject * stageObject )
	: GroupObject( allocator, stageObject ),
		fParams( NULL ),
		fRef( LUA_NOREF ),
		fUserData( NULL )
{
}

CORONA_OBJECTS_VTABLE(Group, fParams->inherited);

const Rtt::LuaProxyVTable&
Group2::ProxyVTable() const
{
	return Group2ProxyVTable::Constant();
}

static Rtt::GroupObject *
NewGroup2 (Rtt_Allocator * allocator, Rtt::StageObject * stageObject)
{
    return Rtt_NEW( allocator, Group2( allocator, NULL ) );
}

CORONA_API
int CoronaObjectsPushGroup (lua_State * L, void * userData, const GroupParams * params, int temporaryParams)
{
	CORONA_OBJECTS_PUSH(Group, GroupParams, object->fParams->inherited);
}
/*
class LuaShape2ProxyVTable : public LuaShapeObjectProxyVTable
{
public:
	typedef LuaShape2ProxyVTable Self;
	typedef LuaShapeObjectProxyVTable Super;

public:
	static const Self& Constant();

protected:
	LuaShape2ProxyVTable() {}

public:
	virtual int ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[], bool overrideRestriction = false ) const;
	virtual bool SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const;
	virtual const LuaProxyVTable& Parent() const;
};
*/

//CORONA_OBJECTS_VTABLE(Group, fParams);

CORONA_API
int CoronaObjectsPushRect (lua_State * L, void * userData, const ShapeParams * params, int temporaryParams)
{
    return 0;
}

/*
class LuaSnapshot2ProxyVTable : public LuaSnapshotObjectProxyVTable
{
public:
	typedef LuaSnapshot2ProxyVTable Self;
	typedef LuaSnapshotObjectProxyVTable Super;

public:
	static const Self& Constant();

protected:
	LuaSnapshot2ProxyVTable() {}

public:
	virtual int ValueForKey( lua_State *L, const MLuaProxyable& object, const char key[], bool overrideRestriction = false ) const;
	virtual bool SetValueForKey( lua_State *L, MLuaProxyable& object, const char key[], int valueIndex ) const;
	virtual const LuaProxyVTable& Parent() const;
};
*/

CORONA_API
int CoronaObjectsPushSnapshot (lua_State * L, void * userData, const SnapshotParams * params, int temporaryParams)
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

#undef FIRST_ARGS