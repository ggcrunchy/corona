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
#include "Display/Rtt_RectPath.h"
#include "Display/Rtt_SnapshotObject.h"
#include "Display/Rtt_StageObject.h"

#include "CoronaGraphicsTypes.h"

static bool
ValuePrologue( lua_State * L, const Rtt::MLuaProxyable& object, const char key[], void * userData, const CoronaDisplayObjectParams & params, int * result )
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
ValueEpilogue( lua_State * L, const Rtt::MLuaProxyable& object, const char key[], void * userData, const CoronaDisplayObjectParams & params, int result )
{
	if (params.afterValue)
	{
		params.afterValue( &object, userData, L, key, &result ); // n.b. `result` previous values still on stack
	}

	return result;
}

static bool
SetValuePrologue( lua_State * L, Rtt::MLuaProxyable& object, const char key[], int valueIndex, void * userData, const CoronaDisplayObjectParams & params, int * result )
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
SetValueEpilogue( lua_State * L, Rtt::MLuaProxyable& object, const char key[], int valueIndex, void * userData, const CoronaDisplayObjectParams & params, int result )
{
	if (params.afterSetValue)
	{
		params.afterSetValue( &object, userData, L, key, valueIndex, &result );
	}

	return result;
}

#define CORONA_OBJECTS_VTABLE(OBJECT_KIND, PROXY_KIND, TO_PARAMS)	\
								 									\
class OBJECT_KIND##2ProxyVTable : public Rtt::Lua##PROXY_KIND##ObjectProxyVTable	\
{																					\
public:																				\
	typedef OBJECT_KIND##2ProxyVTable Self;				\
	typedef Lua##PROXY_KIND##ObjectProxyVTable Super;	\
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
		const OBJECT_KIND##2 & resolved = static_cast<const OBJECT_KIND##2 &>(object);	\
		const CoronaDisplayObjectParams & params = TO_PARAMS;							\
		void * userData = const_cast<void *>( resolved.fUserData );						\
		int result = 0;																	\
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
		const OBJECT_KIND##2 & resolved = static_cast<const OBJECT_KIND##2 &>(object);	\
		const CoronaDisplayObjectParams & params = TO_PARAMS;							\
		void * userData = const_cast<void *>( resolved.fUserData );						\
		int result = 0;																	\
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
};																				\
																				\
const Rtt::LuaProxyVTable& OBJECT_KIND##2::ProxyVTable() const { return OBJECT_KIND##2ProxyVTable::Constant(); }

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
	if (PushFactory( L, name ) ) // args[, factories, factory]
	{
		lua_pushlightuserdata( L, func ); // args, factories, factory, func
		lua_setupvalue( L, -2, 2 ); // args, factories, factory; factory.upvalue[2] = func
		lua_insert( L, 1 ); // factory, args, factories
		lua_pop( L, 1 ); // factory, args
		lua_call( L, lua_gettop( L ) - 1, 1 ); // object?

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

#define CORONA_OBJECTS_PUSH(OBJECT_KIND, PARAMS_KIND, TO_PARAMS)							\
	if (CallNewFactory( L, "new" #OBJECT_KIND, &New##OBJECT_KIND##2) ) /* ...[, object] */	\
	{																			\
		OBJECT_KIND##2 * object = (OBJECT_KIND##2 *)lua_touserdata( L, -1 );	\
																				\
		object->fParams = (PARAMS_KIND *)CopyParams( L, params, sizeof(*params), temporaryParams, object->fRef );	\
		object->fUserData = userData;																				\
																													\
		if (( TO_PARAMS ).onCreate)						\
		{												\
			( TO_PARAMS ).onCreate( object, userData );	\
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

#define CORONA_OBJECTS_METHOD(METHOD_NAME, TO_PARAMS)	\
	const auto & params = TO_PARAMS;					\
														\
	CORONA_OBJECTS_METHOD_BOOKEND( before, METHOD_NAME, FIRST_ARGS )	\
	CORONA_OBJECTS_METHOD_CORE( METHOD_NAME )							\
	CORONA_OBJECTS_METHOD_BOOKEND( after, METHOD_NAME, FIRST_ARGS)

#define CORONA_OBJECTS_METHOD_STRIP_ARGUMENT(METHOD_NAME, TO_PARAMS, ARGUMENT)	\
	const auto & params = TO_PARAMS;	\
										\
	CORONA_OBJECTS_METHOD_BOOKEND( before, METHOD_NAME, FIRST_ARGS )	\
	CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( METHOD_NAME, ARGUMENT )		\
	CORONA_OBJECTS_METHOD_BOOKEND( after, METHOD_NAME, FIRST_ARGS)

#define CORONA_OBJECTS_METHOD_WITH_ARGS(METHOD_NAME, TO_PARAMS, ...)	\
	const auto & params = TO_PARAMS;	\
										\
	CORONA_OBJECTS_METHOD_BOOKEND( before, METHOD_NAME, FIRST_ARGS, __VA_ARGS__ )	\
	CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( METHOD_NAME, __VA_ARGS__ )				\
	CORONA_OBJECTS_METHOD_BOOKEND( after, METHOD_NAME, FIRST_ARGS, __VA_ARGS__ )

#define CORONA_OBJECTS_EARLY_OUT_IF_APPROPRIATE(METHOD_NAME)			\
	bool expectNonZeroResult = params.earlyOut##METHOD_NAME##IfNonZero;	\
																		\
	if (params.before##METHOD_NAME && expectNonZeroResult == !!result)	\
	{																	\
		return !!result;												\
	}

#define CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT(METHOD_NAME, ...)	\
	int result = 0;															\
																			\
	CORONA_OBJECTS_METHOD_BOOKEND( before, METHOD_NAME, __VA_ARGS__, &result )	\
	CORONA_OBJECTS_EARLY_OUT_IF_APPROPRIATE( METHOD_NAME )

static void
Copy3 (float * dst, const float * src)
{
	static_assert(sizeof(float) == sizeof(Rtt::Real), "Incompatible real type");

	memcpy(dst, src, 3 * sizeof(float));
}

#define CORONA_OBJECTS_INIT_MATRIX(SOURCE)	\
	Rtt::Real matrix[6];					\
											\
	Copy3( matrix, SOURCE.Row0() );		\
	Copy3( matrix + 3, SOURCE.Row1() )

#define CORONA_OBJECTS_MATRIX_BOOKEND_METHOD(WHEN, METHOD_NAME)	\
	if (params.WHEN##METHOD_NAME)								\
	{															\
		CORONA_OBJECTS_INIT_MATRIX( srcToDst );	\
												\
		params.WHEN##METHOD_NAME( FIRST_ARGS, matrix );	\
														\
		Copy3(const_cast<float *>(srcToDst.Row0()), matrix);		\
		Copy3(const_cast<float *>(srcToDst.Row1()), matrix + 3);	\
	}

#define CORONA_OBJECTS_DRAW_BOOKEND_METHOD(WHEN, METHOD_NAME)	\
	if (params.WHEN##METHOD_NAME)				\
	{											\
		CoronaGraphicsToken token;	\
									\
		CoronaGraphicsEncodeAsTokens( &token, 0xFF, &renderer );	\
		params.WHEN##METHOD_NAME( FIRST_ARGS, &token );				\
		CoronaGraphicsEncodeAsTokens( &token, 0xFF, nullptr );		\
	}

#define CORONA_OBJECTS_INTERFACE(PARAMS_TYPE, TO_PARAMS)						\
	virtual void AddedToParent( lua_State * L, Rtt::GroupObject * parent )		\
	{																			\
		CORONA_OBJECTS_METHOD_WITH_ARGS( AddedToParent, TO_PARAMS, L, parent )	\
	}																			\
																				\
	virtual bool CanCull() const								\
	{															\
		const CoronaDisplayObjectParams & params = TO_PARAMS;	\
																\
		CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT( CanCull, FIRST_ARGS )	\
		CORONA_OBJECTS_METHOD_CORE_WITH_RESULT( CanCull ) 						\
		CORONA_OBJECTS_METHOD_BOOKEND( after, CanCull, FIRST_ARGS, &result )	\
						\
		return result;	\
	}					\
						\
	virtual bool CanHitTest() const								\
	{															\
		const CoronaDisplayObjectParams & params = TO_PARAMS;	\
																\
		CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT( CanHitTest, FIRST_ARGS )	\
		CORONA_OBJECTS_METHOD_CORE_WITH_RESULT( CanHitTest )						\
		CORONA_OBJECTS_METHOD_BOOKEND( after, CanHitTest, FIRST_ARGS, &result )		\
						\
		return result;	\
	}					\
						\
	virtual void DidMoveOffscreen()								\
	{															\
		CORONA_OBJECTS_METHOD( DidMoveOffscreen, TO_PARAMS )	\
	}															\
																\
	virtual void DidUpdateTransform( Rtt::Matrix & srcToDst )	\
	{															\
		const CoronaDisplayObjectParams & params = TO_PARAMS;	\
																\
		CORONA_OBJECTS_MATRIX_BOOKEND_METHOD( before, DidUpdateTransform )		\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( DidUpdateTransform, srcToDst )	\
		CORONA_OBJECTS_MATRIX_BOOKEND_METHOD( after, DidUpdateTransform )		\
	}																			\
																				\
	virtual void Draw(Rtt::Renderer & renderer) const			\
	{															\
		const CoronaDisplayObjectParams & params = TO_PARAMS;	\
																\
		CORONA_OBJECTS_DRAW_BOOKEND_METHOD( before, Draw )		\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( Draw, renderer )	\
		CORONA_OBJECTS_DRAW_BOOKEND_METHOD( after, Draw )		\
	}															\
																\
	virtual void FinalizeSelf( lua_State * L )				\
	{														\
		if (( TO_PARAMS ).onFinalize)						\
		{													\
			( TO_PARAMS ).onFinalize( this, fUserData );	\
		}													\
															\
		lua_unref( L, fRef );	\
								\
		Super::FinalizeSelf( L );	\
	}								\
									\
	virtual void GetSelfBounds( Rtt::Rect & rect ) const		\
	{															\
		const CoronaDisplayObjectParams & params = TO_PARAMS;	\
																\
		CORONA_OBJECTS_METHOD_BOOKEND( before, GetSelfBounds, FIRST_ARGS, &rect.xMin, &rect.yMin, &rect.xMax, &rect.yMax )	\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( GetSelfBounds, rect )															\
		CORONA_OBJECTS_METHOD_BOOKEND( after, GetSelfBounds, FIRST_ARGS, &rect.xMin, &rect.yMin, &rect.xMax, &rect.yMax )	\
	}																														\
																															\
	virtual void GetSelfBoundsForAnchor( Rtt::Rect & rect ) const	\
	{																\
		const CoronaDisplayObjectParams & params = TO_PARAMS;		\
																	\
		CORONA_OBJECTS_METHOD_BOOKEND( before, GetSelfBoundsForAnchor, FIRST_ARGS, &rect.xMin, &rect.yMin, &rect.xMax, &rect.yMax )	\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( GetSelfBoundsForAnchor, rect )														\
		CORONA_OBJECTS_METHOD_BOOKEND( after, GetSelfBoundsForAnchor, FIRST_ARGS, &rect.xMin, &rect.yMin, &rect.xMax, &rect.yMax )	\
	}																																\
																																	\
	virtual bool HitTest( Rtt::Real contentX, Rtt::Real contentY )	\
	{																\
		const CoronaDisplayObjectParams & params = TO_PARAMS;		\
																	\
		CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT( HitTest, FIRST_ARGS, contentX, contentY )	\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS_AND_RESULT( HitTest, contentX, contentY )				\
		CORONA_OBJECTS_METHOD_BOOKEND( after, HitTest, FIRST_ARGS, contentX, contentY, &result )	\
																									\
		return result;	\
	}					\
						\
	virtual void Prepare( const Rtt::Display & display )					\
	{																		\
		CORONA_OBJECTS_METHOD_STRIP_ARGUMENT( Prepare, TO_PARAMS, display )	\
	}																		\
																			\
	virtual void RemovedFromParent( lua_State * L, Rtt::GroupObject * parent )		\
	{																				\
		CORONA_OBJECTS_METHOD_WITH_ARGS( RemovedFromParent, TO_PARAMS, L, parent )	\
	}																				\
																					\
	virtual void Rotate( Rtt::Real deltaTheta )								\
	{																		\
		CORONA_OBJECTS_METHOD_WITH_ARGS( Rotate, TO_PARAMS, deltaTheta )	\
	}																		\
																			\
	virtual void Scale( Rtt::Real sx, Rtt::Real sy, bool isNewValue )				\
	{																				\
		CORONA_OBJECTS_METHOD_WITH_ARGS( Scale, TO_PARAMS, sx, sy, isNewValue )		\
	}																				\
																					\
	virtual void SendMessage( const char * message, const void * payload, U32 size ) const	\
	{																						\
		if (( TO_PARAMS ).onMessage)														\
		{																					\
			( TO_PARAMS ).onMessage( FIRST_ARGS, message, payload, size );					\
		}																					\
	}																						\
																							\
	virtual void Translate( Rtt::Real deltaX, Rtt::Real deltaY )				\
	{																			\
		CORONA_OBJECTS_METHOD_WITH_ARGS( Translate, TO_PARAMS, deltaX, deltaY )	\
	}																			\
																				\
	virtual bool UpdateTransform( const Rtt::Matrix & parentToDstSpace )	\
	{																		\
		CORONA_OBJECTS_INIT_MATRIX( parentToDstSpace );						\
																			\
		const CoronaDisplayObjectParams & params = TO_PARAMS;	\
																\
		CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT( UpdateTransform, FIRST_ARGS, matrix )	\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS_AND_RESULT( UpdateTransform, parentToDstSpace )	\
		CORONA_OBJECTS_METHOD_BOOKEND( after, UpdateTransform, FIRST_ARGS, matrix, &result )	\
																								\
		return result;	\
	}					\
						\
	virtual void WillMoveOnscreen()								\
	{															\
		CORONA_OBJECTS_METHOD( WillMoveOnscreen, TO_PARAMS )	\
	}															\
																\
	virtual const Rtt::LuaProxyVTable& ProxyVTable() const;	\
															\
	PARAMS_TYPE * fParams;		\
	int fRef;					\
	mutable void * fUserData

/**
TODO
*/
class Group2 : public Rtt::GroupObject {
public:
	typedef Group2 Self;
	typedef Rtt::GroupObject Super;

public:

	Group2( Rtt_Allocator * allocator, Rtt::StageObject * stageObject );

	virtual void DidInsert( bool childParentChanged )
	{
		CORONA_OBJECTS_METHOD_WITH_ARGS( DidInsert, *fParams, childParentChanged )
	}

	virtual void DidRemove()
	{
		CORONA_OBJECTS_METHOD( DidRemove, *fParams, ... )
	}

	CORONA_OBJECTS_INTERFACE( CoronaGroupObjectParams, fParams->inherited );
};

Group2::Group2( Rtt_Allocator * allocator, Rtt::StageObject * stageObject )
	: GroupObject( allocator, stageObject ),
		fParams( NULL ),
		fRef( LUA_NOREF ),
		fUserData( NULL )
{
}

CORONA_OBJECTS_VTABLE( Group, Group, resolved.fParams->inherited )

static Rtt::GroupObject *
NewGroup2( Rtt_Allocator * allocator, Rtt::StageObject * stageObject )
{
    return Rtt_NEW( allocator, Group2( allocator, NULL ) );
}

CORONA_API
int CoronaObjectsPushGroup( lua_State * L, void * userData, const CoronaGroupObjectParams * params, int temporaryParams )
{
	CORONA_OBJECTS_PUSH( Group, CoronaGroupObjectParams, object->fParams->inherited );
}

/**
TODO
*/
class Rect2 : public Rtt::RectObject {
public:
	typedef Rect2 Self;
	typedef Rtt::RectObject Super;

public:
	Rect2( Rtt::RectPath * path );

	CORONA_OBJECTS_INTERFACE( CoronaDisplayObjectParams, *fParams ); // TODO: should this be ShapeParams? could streamline macros if ALWAYS expecting `inherited`...
};

Rect2::Rect2( Rtt::RectPath * path )
	: RectObject( path ),
	fParams( NULL ),
	fRef( LUA_NOREF ),
	fUserData( NULL )
{
}

CORONA_OBJECTS_VTABLE( Rect, Shape, *resolved.fParams )

static Rtt::RectObject *
NewRect2( Rtt_Allocator* pAllocator, Rtt::Real width, Rtt::Real height )
{
	Rtt::RectPath* path = Rtt::RectPath::NewRect( pAllocator, width, height );

	return Rtt_NEW( pAllocator, Rect2( path ) );
}

CORONA_API
int CoronaObjectsPushRect (lua_State * L, void * userData, const CoronaShapeObjectParams * params, int temporaryParams)
{
	CORONA_OBJECTS_PUSH( Rect, CoronaDisplayObjectParams, *object->fParams );
}

/**
TODO
*/
class Snapshot2 : public Rtt::SnapshotObject {
public:
	typedef Snapshot2 Self;
	typedef Rtt::SnapshotObject Super;

public:
	Snapshot2( Rtt_Allocator * pAllocator, Rtt::Display & display, Rtt::Real contentW, Rtt::Real contentH );

	CORONA_OBJECTS_INTERFACE( CoronaSnapshotObjectParams, fParams->inherited );
};

Snapshot2::Snapshot2( Rtt_Allocator * pAllocator, Rtt::Display & display, Rtt::Real contentW, Rtt::Real contentH )
	: SnapshotObject( pAllocator, display, contentW, contentH ),
	fParams( NULL ),
	fRef( LUA_NOREF ),
	fUserData( NULL )
{
}

CORONA_OBJECTS_VTABLE( Snapshot, Snapshot, resolved.fParams->inherited )

static Rtt::SnapshotObject *
NewSnapshot2( Rtt_Allocator * pAllocator, Rtt::Display & display, Rtt::Real width, Rtt::Real height )
{
	return Rtt_NEW( pAllocator, Snapshot2( pAllocator, display, width, height ) );
}

CORONA_API
int CoronaObjectsPushSnapshot( lua_State * L, void * userData, const CoronaSnapshotObjectParams * params, int temporaryParams )
{
	CORONA_OBJECTS_PUSH( Snapshot, CoronaSnapshotObjectParams, object->fParams->inherited );
}

CORONA_API
int CoronaObjectsShouldDraw( const void * object, int * shouldDraw )
{
    // TODO: look for proxy on stack, validate object?
    // If so, then *shouldDraw = ((DisplayObject *)object)->shouldDraw()

    return 0;
}

CORONA_API
const void * CoronaObjectGetParent( const void * object )
{
	return static_cast< const Rtt::DisplayObject *>( object )->GetParent(); // TODO: validation?
}

CORONA_API
const void * CoronaGroupObjectGetChild( const void * groupObject, int index )
{
	const Rtt::GroupObject * group = static_cast< const Rtt::GroupObject *>( groupObject );

	return (index >= 0 && index < group->NumChildren()) ? &group->ChildAt( index ) : nullptr;
}

CORONA_API
int CoronaGroupObjectGetNumChildren( const void * groupObject )
{
	return static_cast< const Rtt::GroupObject *>( groupObject )->NumChildren();
}

CORONA_API
int CoronaObjectSendMessage( const void * object, const char * message, const void * payload, unsigned int size )
{
	static_cast< const Rtt::DisplayObject *>( object )->SendMessage( message, payload, size );

	return 1;
}

#undef FIRST_ARGS