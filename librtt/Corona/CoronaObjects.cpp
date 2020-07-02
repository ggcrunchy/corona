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
#include "Rtt_LuaProxy.h"
#include "Rtt_LuaProxyVTable.h"
#include "Rtt_Runtime.h"

#include "Corona/CoronaPluginSupportInternal.h"

#include "Display/Rtt_Display.h"
#include "Display/Rtt_GroupObject.h"
#include "Display/Rtt_RectObject.h"
#include "Display/Rtt_RectPath.h"
#include "Display/Rtt_SnapshotObject.h"
#include "Display/Rtt_StageObject.h"

#include <vector>
#include <stddef.h>

#define CORONA_OBJECTS_STREAM_METATABLE_NAME "CoronaObjectsStream"

#define SIZE_MINUS_OFFSET(TYPE, OFFSET) sizeof( TYPE ) - OFFSET
#define OFFSET_OF_MEMBER(NAME, MEMBER_NAME) offsetof( CoronaObject##NAME##Params, MEMBER_NAME )
#define AFTER_HEADER_STRUCT(NAME, MEMBER_NAME)																									\
	struct NAME##Struct { unsigned char bytes[ SIZE_MINUS_OFFSET( CoronaObject##NAME##Params, OFFSET_OF_MEMBER( NAME, MEMBER_NAME ) ) ]; } NAME
#define AFTER_HEADER_FLAG(NAME) AFTER_HEADER_STRUCT( NAME, ignoreOriginal )
#define AFTER_HEADER_ACTION(NAME) AFTER_HEADER_STRUCT( NAME, action )
#define AFTER_HEADER_FLAG_OFFSET(NAME) OFFSET_OF_MEMBER( NAME, ignoreOriginal )
#define AFTER_HEADER_ACTION_OFFSET(NAME) OFFSET_OF_MEMBER( NAME, action )

union GenericParams {
	AFTER_HEADER_FLAG( Basic );
	AFTER_HEADER_FLAG( GroupBasic );
	AFTER_HEADER_FLAG( AddedToParent );
	AFTER_HEADER_FLAG( DidInsert );
	AFTER_HEADER_FLAG( Matrix );
	AFTER_HEADER_FLAG( Draw );
	AFTER_HEADER_FLAG( RectResult );
	AFTER_HEADER_FLAG( RemovedFromParent );
	AFTER_HEADER_FLAG( Rotate );
	AFTER_HEADER_FLAG( Scale );
	AFTER_HEADER_FLAG( Translate );

	AFTER_HEADER_FLAG( BooleanResult );
	AFTER_HEADER_FLAG( BooleanResultPoint );
	AFTER_HEADER_FLAG( BooleanResultMatrix );

	AFTER_HEADER_FLAG( SetValue );
	AFTER_HEADER_FLAG( Value );

	AFTER_HEADER_ACTION( Lifetime );
	AFTER_HEADER_ACTION( OnMessage );
};

template<typename T> T
FindParams( const unsigned char * stream, unsigned short method, size_t offset )
{
	static_assert( kAugmentedMethod_Count < 256, "Stream assumes byte-sized methods" );
	
	T out = {};

	unsigned int count = *stream;
	const unsigned char * methods = stream + 1, * genericParams = methods + count;

	if (method < count) // methods are sorted, so cannot take more than `method` steps
	{
		count = method;
	}

	for (unsigned int i = 0; i < count; ++i, genericParams += sizeof( GenericParams )) 
	{
		if (methods[i] == method)
		{
			memcpy( reinterpret_cast< unsigned char * >( &out ) + offset, genericParams, SIZE_MINUS_OFFSET( T, offset ) );

			break;
		}
	}

	return out;
}

static bool
ValuePrologue( lua_State * L, const Rtt::MLuaProxyable& object, const char key[], void * userData, const CoronaObjectValueParams & params, int * result )
{
	if (params.before)
	{
		auto && storedObject = CoronaInternalStoreDisplayObject( &object );

		params.before( storedObject.GetHandle(), userData, L, key, result );

		bool canEarlyOut = !params.disallowEarlyOut, expectsNonZero = !params.earlyOutIfZero;

		if (canEarlyOut && expectsNonZero == !!*result)
		{
			return false;
		}
	}

	return true;
}

static int
ValueEpilogue( lua_State * L, const Rtt::MLuaProxyable& object, const char key[], void * userData, const CoronaObjectValueParams & params, int result )
{
	if (params.after)
	{
		auto && storedObject = CoronaInternalStoreDisplayObject( &object );

		params.after( storedObject.GetHandle(), userData, L, key, &result ); // n.b. `result` previous values still on stack
	}

	return result;
}

static bool
SetValuePrologue( lua_State * L, Rtt::MLuaProxyable& object, const char key[], int valueIndex, void * userData, const CoronaObjectSetValueParams & params, int * result )
{
	if (params.before)
	{
		auto && storedObject = CoronaInternalStoreDisplayObject( &object );

		params.before( storedObject.GetHandle(), userData, L, key, valueIndex, result );

		bool canEarlyOut = !params.disallowEarlyOut;

		if (canEarlyOut && *result)
		{
			return false;
		}
	}

	return true;
}

static bool
SetValueEpilogue( lua_State * L, Rtt::MLuaProxyable& object, const char key[], int valueIndex, void * userData, const CoronaObjectSetValueParams & params, int result )
{
	if (params.after)
	{
		auto && storedObject = CoronaInternalStoreDisplayObject( &object );

		params.after( storedObject.GetHandle(), userData, L, key, valueIndex, &result );
	}

	return result;
}

template<typename Proxy2, typename BaseProxyVTable>
class Proxy2VTable : public BaseProxyVTable
{
public:
	typedef Proxy2VTable Self;
	typedef BaseProxyVTable Super;

public:
	static const Self& Constant() { static const Self kVTable; return kVTable; }

protected:
	Proxy2VTable() {}

public:
	virtual int ValueForKey( lua_State *L, const Rtt::MLuaProxyable& object, const char key[], bool overrideRestriction = false ) const
	{
		const Proxy2 & resolved = static_cast< const Proxy2 & >( object );
		const auto params = FindParams< CoronaObjectValueParams >( resolved.fStream, kAugmentedMethod_Value, AFTER_HEADER_FLAG_OFFSET( Value ) );
		void * userData = const_cast< void * >( resolved.fUserData );
		int result = 0;

		if (!ValuePrologue( L, object, key, userData, params, &result ))
		{
			return result;
		}

		else if (!params.ignoreOriginal)
		{
			result += Super::Constant().ValueForKey( L, object, key, overrideRestriction );
		}

		return ValueEpilogue( L, object, key, userData, params, result );
	}

	virtual bool SetValueForKey( lua_State *L, Rtt::MLuaProxyable& object, const char key[], int valueIndex ) const
	{
		const Proxy2 & resolved = static_cast< const Proxy2 & >( object );
		const auto params = FindParams< CoronaObjectSetValueParams >( resolved.fStream, kAugmentedMethod_SetValue, AFTER_HEADER_FLAG_OFFSET( SetValue ) );
		void * userData = const_cast< void * >( resolved.fUserData );
		int result = 0;

		if (!SetValuePrologue( L, object, key, valueIndex, userData, params, &result ))
		{
			return result;
		}

		else if (!params.ignoreOriginal)
		{
			result = Super::Constant().SetValueForKey( L, object, key, valueIndex );
		}

		return SetValueEpilogue( L, object, key, valueIndex, userData, params, result );
	}

	virtual const Rtt::LuaProxyVTable& Parent() const { return Super::Constant(); }
	virtual const Rtt::LuaProxyVTable& ProxyVTable() const { return Self::Constant(); }
};

#define CORONA_OBJECTS_VTABLE(OBJECT_KIND, PROXY_KIND)															\
	class OBJECT_KIND##2;																						\
	typedef Proxy2VTable< OBJECT_KIND##2, Rtt::Lua##PROXY_KIND##ObjectProxyVTable > OBJECT_KIND##2ProxyVTable

static bool
PushFactory( lua_State * L, const char * name )
{
	Rtt::Display & display = Rtt::LuaContext::GetRuntime( L )->GetDisplay();

	if (!display.PushObjectFactories()) // stream, ...[, factories]
	{
		return false;
	}

	lua_getfield( L, -1, name ); // stream, ..., factories, factory

	return true;
}

struct StreamAndUserData {
	unsigned char * stream;
	void * userData;
};

static StreamAndUserData sStreamAndUserData;

static void
OnCreate( const void * object, void * userData, const unsigned char * stream )
{
	const auto params = FindParams< CoronaObjectLifetimeParams >( stream, kAugmentedMethod_OnCreate, sizeof( CoronaObjectLifetimeParams ) - sizeof( GenericParams::Lifetime ) );

	if (params.action)
	{
		auto && storedObject = CoronaInternalStoreDisplayObject( object );

		params.action( storedObject.GetHandle(), userData );
	}
}

#define CORONA_OBJECTS_BIND_STREAM_AND_USER_DATA(INDEX)							\
	sStreamAndUserData.stream = (unsigned char *)lua_touserdata( L, INDEX );	\
	sStreamAndUserData.userData = userData

#define CORONA_OBJECTS_ON_CREATE(OBJECT)				\
	OBJECT->fStream = sStreamAndUserData.stream;		\
	OBJECT->fUserData = sStreamAndUserData.userData;	\
														\
	sStreamAndUserData.stream = NULL;	\
	sStreamAndUserData.userData = NULL;	\
										\
	OnCreate( OBJECT, OBJECT->fUserData, OBJECT->fStream )

static bool 
CallNewFactory( lua_State * L, const char * name, void * func )
{
	if (PushFactory( L, name ) ) // stream, ...[, factories, factory]
	{
		lua_pushlightuserdata( L, func ); // stream, ..., factories, factory, func
		lua_setupvalue( L, -2, 2 ); // stream, ..., factories, factory; factory.upvalue[2] = func
		lua_insert( L, 2 ); // stream, factory, ..., factories
		lua_pop( L, 1 ); // stream, factory, ...
		lua_call( L, lua_gettop( L ) - 2, 1 ); // stream, object?

		return !lua_isnil( L, 2 );
	}

	return false;
}

static void
GetSizes( unsigned short method, size_t & fullSize, size_t & paramSize )
{
#define GET_SIZES(NAME)									\
	fullSize = sizeof( CoronaObject##NAME##Params );	\
	paramSize = sizeof( GenericParams::NAME.bytes )

#define UNIQUE_METHOD(NAME)		\
	kAugmentedMethod_##NAME:	\
		GET_SIZES(NAME)

	switch (method)
	{
	case kAugmentedMethod_DidMoveOffscreen:
	case kAugmentedMethod_Prepare:
	case kAugmentedMethod_WillMoveOnscreen:
	case kAugmentedMethod_DidRemove:
		GET_SIZES( Basic );

		break;
	case kAugmentedMethod_CanCull:
	case kAugmentedMethod_CanHitTest:
		GET_SIZES( BooleanResult );

		break;
	case kAugmentedMethod_OnCreate:
	case kAugmentedMethod_OnFinalize:
		GET_SIZES( Lifetime );

		break;
	case kAugmentedMethod_GetSelfBounds:
	case kAugmentedMethod_GetSelfBoundsForAnchor:
		GET_SIZES( RectResult );

		break;
	case kAugmentedMethod_DidUpdateTransform:
		GET_SIZES( Matrix );

		break;
	case kAugmentedMethod_HitTest:
		GET_SIZES( BooleanResultPoint );

		break;
	case kAugmentedMethod_UpdateTransform:
		GET_SIZES( BooleanResultMatrix );

		break;
	case UNIQUE_METHOD( AddedToParent );
		break;
	case UNIQUE_METHOD( DidInsert );
		break;
	case UNIQUE_METHOD( Draw );
		break;
	case UNIQUE_METHOD( OnMessage );
		break;
	case UNIQUE_METHOD( RemovedFromParent );
		break;
	case UNIQUE_METHOD( Rotate );
		break;
	case UNIQUE_METHOD( Scale );
		break;
	case UNIQUE_METHOD( SetValue );
		break;
	case UNIQUE_METHOD( Translate );
		break;
	case UNIQUE_METHOD( Value );
		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}

#undef GET_SIZES
#undef UNIQUE_METHOD
}

static bool
BuildMethodStream( lua_State * L, const CoronaObjectParamsHeader * head )
{
	if (!head)
	{
		return false;
	}

	std::vector< const CoronaObjectParamsHeader * > params;

	for (const CoronaObjectParamsHeader * cur = head; cur; cur = cur->next)
	{
		if (cur->method != kAugmentedMethod_None) // allow these as a convenience, mainly for the head
		{
			params.push_back( cur );
		}
	}

	if (params.empty())
	{
		return false;
	}

	// After being built, the stream is immutable, so sort by method to allow some additional optimizations.
	std::sort( params.begin(), params.end(), [](const CoronaObjectParamsHeader * p1, const CoronaObjectParamsHeader * p2) { return p1->method < p2->method; });

	if (params.back()->method >= (unsigned short)( kAugmentedMethod_Count )) // has bogus method(s)?
	{
		return false;
	}

	// Check for duplicates.
	unsigned short prev = (unsigned short)~0U;

	for ( const CoronaObjectParamsHeader * header : params )
	{
		if (header->method == prev)
		{
			return false;
		}

		prev = header->method;
	}

	unsigned char * stream = (unsigned char *)lua_newuserdata( L, 1U + (1U + sizeof( GenericParams )) * params.size() ); // ..., stream

	luaL_newmetatable( L, CORONA_OBJECTS_STREAM_METATABLE_NAME ); // ..., stream, mt
	lua_setmetatable( L, -2 ); // ..., stream; stream.metatable = mt

	*stream = (unsigned char)( params.size() );

	unsigned char * methods = stream + 1, * genericParams = methods + params.size();

	for (size_t i = 0; i < params.size(); ++i, genericParams += sizeof( GenericParams ))
	{
		methods[i] = (unsigned char)params[i]->method;

		size_t fullSize, paramSize;

		GetSizes( params[i]->method, fullSize, paramSize ); // we no longer need the next pointers and have the methods in front, so only want the post-header size...
		memcpy( genericParams, reinterpret_cast< const unsigned char * >( params[i] ) + (fullSize - paramSize), paramSize ); // ...and content
	}

	return true;
}

static bool
GetStream( lua_State * L, const CoronaObjectsParams * params )
{
	bool hasStream = false;

	if (params)
	{
		if (params->useRef)
		{
			lua_getref( L, params->u.ref ); // ..., stream?

			if (lua_getmetatable( L, -1 )) // ..., stream?[, mt1]
			{
				luaL_getmetatable( L, CORONA_OBJECTS_STREAM_METATABLE_NAME ); // ..., stream?, mt1, mt2

				hasStream = lua_equal( L, -2, -1 );

				lua_pop( L, 2 ); // ..., stream?
			}

			else
			{
				lua_pop( L, 1 ); // ...
			}
		}

		else
		{
			hasStream = BuildMethodStream( L, params->u.head ); // ...[, stream]
		}
	}

	return hasStream;
}

template<typename T>
int PushObject( lua_State * L, void * userData, const CoronaObjectParams * params, const char * name, void * func )
{
	if (!GetStream( L, params )) // ...[, stream]
	{
		return 0;
	}

	lua_insert( L, 1 ); // stream, ...

	CORONA_OBJECTS_BIND_STREAM_AND_USER_DATA( 1 );

	if (CallNewFactory( L, name, func )) // stream[, object]
	{
		T * object = (T *)Rtt::LuaProxy::GetProxyableObject( L, 2 );

		lua_insert( L, 1 ); // object, stream

		if (!params->useRef) // temporary?
		{
			object->fRef = luaL_ref( L, LUA_REGISTRYINDEX ); // object; registry = { ..., [ref] = stream }
		}
		
		else // guard stream in case reference is dropped; this is redundant after the first object using the stream, but harmless
		{
			lua_pushlightuserdata( L, lua_touserdata( L, -1 ) ); // object, stream, stream_ptr
			lua_insert( L, 2 ); // object, stream_ptr, stream
			lua_rawset( L, LUA_REGISTRYINDEX ); // object; registry = { [stream_ptr] = stream }
		}

		return 1;
	}

	else
	{
		lua_pop( L, 1 ); // ...
	}

	return 0;
}

#define CORONA_OBJECTS_PUSH(OBJECT_KIND) return PushObject< OBJECT_KIND##2 >( L, userData, params, "new" #OBJECT_KIND, &OBJECT_KIND##2::New )

#define STORE_THIS(OBJECT_TYPE) auto storedThis = CoronaInternalStore##OBJECT_TYPE( reinterpret_cast< const OBJECT_TYPE * >( this ) )
#define STORE_VALUE(NAME, OBJECT_TYPE) auto NAME##Stored = CoronaInternalStore##OBJECT_TYPE( NAME )

#define FIRST_ARGS storedThis.GetHandle(), fUserData
#define CORONA_OBJECTS_METHOD_BOOKEND(WHEN, ...)	\
	if (params.WHEN)								\
	{												\
		params.WHEN( __VA_ARGS__ );					\
	}

#define CORONA_OBJECTS_METHOD_CORE(METHOD_NAME)	\
	if (!params.ignoreOriginal)					\
	{											\
		Super::METHOD_NAME();					\
	}

#define CORONA_OBJECTS_METHOD_CORE_WITH_RESULT(METHOD_NAME)	\
	if (!params.ignoreOriginal)								\
	{														\
		result = Super::METHOD_NAME();						\
	}

#define CORONA_OBJECTS_METHOD_CORE_WITH_ARGS(METHOD_NAME, ...)	\
	if (!params.ignoreOriginal)									\
	{															\
		Super::METHOD_NAME( __VA_ARGS__ );						\
	}

#define CORONA_OBJECTS_METHOD_CORE_WITH_ARGS_AND_RESULT(METHOD_NAME, ...)	\
	if (!params.ignoreOriginal)												\
	{																		\
		result = Super::METHOD_NAME( __VA_ARGS__ );							\
	}

#define CORONA_OBJECTS_METHOD(METHOD_NAME)				\
	CORONA_OBJECTS_METHOD_BOOKEND( before, FIRST_ARGS )	\
	CORONA_OBJECTS_METHOD_CORE( METHOD_NAME )			\
	CORONA_OBJECTS_METHOD_BOOKEND( after, FIRST_ARGS)

#define CORONA_OBJECTS_METHOD_STRIP_ARGUMENT(METHOD_NAME, ARGUMENT)	\
	CORONA_OBJECTS_METHOD_BOOKEND( before, FIRST_ARGS )				\
	CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( METHOD_NAME, ARGUMENT )	\
	CORONA_OBJECTS_METHOD_BOOKEND( after, FIRST_ARGS)

#define CORONA_OBJECTS_METHOD_WITH_ARGS(METHOD_NAME, ...)				\
	CORONA_OBJECTS_METHOD_BOOKEND( before, FIRST_ARGS, __VA_ARGS__ )	\
	CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( METHOD_NAME, __VA_ARGS__ )	\
	CORONA_OBJECTS_METHOD_BOOKEND( after, FIRST_ARGS, __VA_ARGS__ )

#define CORONA_OBJECTS_EARLY_OUT_IF_APPROPRIATE()			\
	bool expectNonZeroResult = params.earlyOutIfNonZero;	\
															\
	if (params.before && expectNonZeroResult == !!result)	\
	{														\
		return !!result;									\
	}

#define CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT(...)	\
	int result = 0;												\
																\
	CORONA_OBJECTS_METHOD_BOOKEND( before, __VA_ARGS__, &result )	\
	CORONA_OBJECTS_EARLY_OUT_IF_APPROPRIATE()

static void
Copy3 (float * dst, const float * src)
{
	static_assert( sizeof( float ) == sizeof( Rtt::Real ), "Incompatible real type" );

	memcpy(dst, src, 3 * sizeof( float ));
}

#define CORONA_OBJECTS_INIT_MATRIX(SOURCE)	\
	Rtt::Real matrix[6];					\
											\
	Copy3( matrix, SOURCE.Row0() );		\
	Copy3( matrix + 3, SOURCE.Row1() )

#define CORONA_OBJECTS_MATRIX_BOOKEND_METHOD(WHEN)	\
	if (params.WHEN)								\
	{												\
		CORONA_OBJECTS_INIT_MATRIX( srcToDst );	\
												\
		params.WHEN( FIRST_ARGS, matrix );	\
											\
		Copy3(const_cast< float * >(srcToDst.Row0()), matrix);		\
		Copy3(const_cast< float * >(srcToDst.Row1()), matrix + 3);	\
	}

#define CORONA_OBJECTS_GET_PARAMS_SPECIFIC(METHOD, PARAMS_TYPE)																																		\
	const auto params = FindParams< CoronaObject##PARAMS_TYPE##Params >( fStream, kAugmentedMethod_##METHOD, sizeof( CoronaObject##PARAMS_TYPE##Params) - sizeof( GenericParams::PARAMS_TYPE ) )

#define CORONA_OBJECTS_GET_PARAMS(PARAMS_TYPE) CORONA_OBJECTS_GET_PARAMS_SPECIFIC(PARAMS_TYPE, PARAMS_TYPE)

#define CORONA_OBJECTS_INTERFACE(OBJECT_KIND)								\
	virtual void AddedToParent( lua_State * L, Rtt::GroupObject * parent )	\
	{																		\
		STORE_THIS( DisplayObject );		\
		STORE_VALUE( parent, GroupObject );	\
		CORONA_OBJECTS_GET_PARAMS( AddedToParent );											\
		CORONA_OBJECTS_METHOD_BOOKEND( before, FIRST_ARGS, L, parentStored.GetHandle() );	\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( AddedToParent, L, parent );					\
		CORONA_OBJECTS_METHOD_BOOKEND( after, FIRST_ARGS, L, parentStored.GetHandle() );	\
	}	\
		\
	virtual bool CanCull() const		\
	{									\
		STORE_THIS( DisplayObject );	\
		CORONA_OBJECTS_GET_PARAMS_SPECIFIC( CanCull, BooleanResult );	\
		CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT( FIRST_ARGS )	\
		CORONA_OBJECTS_METHOD_CORE_WITH_RESULT( CanCull ) 				\
		CORONA_OBJECTS_METHOD_BOOKEND( after, FIRST_ARGS, &result )		\
						\
		return result;	\
	}	\
		\
	virtual bool CanHitTest() const		\
	{									\
		STORE_THIS( DisplayObject );	\
		CORONA_OBJECTS_GET_PARAMS_SPECIFIC( CanHitTest, BooleanResult );	\
		CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT( FIRST_ARGS )		\
		CORONA_OBJECTS_METHOD_CORE_WITH_RESULT( CanHitTest )				\
		CORONA_OBJECTS_METHOD_BOOKEND( after, FIRST_ARGS, &result )			\
						\
		return result;	\
	}	\
		\
	virtual void DidMoveOffscreen()		\
	{									\
		STORE_THIS( DisplayObject );	\
		CORONA_OBJECTS_GET_PARAMS_SPECIFIC( DidMoveOffscreen, Basic );	\
		CORONA_OBJECTS_METHOD( DidMoveOffscreen )						\
	}	\
		\
	virtual void DidUpdateTransform( Rtt::Matrix & srcToDst )	\
	{															\
		STORE_THIS( DisplayObject );							\
		CORONA_OBJECTS_GET_PARAMS_SPECIFIC( DidUpdateTransform, Matrix );		\
		CORONA_OBJECTS_MATRIX_BOOKEND_METHOD( before )							\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( DidUpdateTransform, srcToDst )	\
		CORONA_OBJECTS_MATRIX_BOOKEND_METHOD( after )							\
	}	\
		\
	virtual void Draw( Rtt::Renderer & renderer ) const	\
	{													\
		STORE_THIS( DisplayObject );		\
		STORE_VALUE( &renderer, Renderer );	\
		CORONA_OBJECTS_GET_PARAMS( Draw );													\
		CORONA_OBJECTS_METHOD_BOOKEND( before, FIRST_ARGS, rendererStored.GetHandle() );	\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( Draw, renderer );								\
		CORONA_OBJECTS_METHOD_BOOKEND( after, FIRST_ARGS, rendererStored.GetHandle() );		\
	}	\
		\
	virtual void FinalizeSelf( lua_State * L )	\
	{											\
		STORE_THIS( DisplayObject );			\
		CORONA_OBJECTS_GET_PARAMS_SPECIFIC( OnFinalize, Lifetime );	\
																	\
		if (params.action)					\
		{									\
			params.action( FIRST_ARGS );	\
		}	\
			\
		lua_unref( L, fRef );	\
								\
		Super::FinalizeSelf( L );	\
	}	\
		\
	virtual void GetSelfBounds( Rtt::Rect & rect ) const	\
	{														\
		STORE_THIS( DisplayObject );						\
		CORONA_OBJECTS_GET_PARAMS_SPECIFIC( GetSelfBounds, RectResult );									\
		CORONA_OBJECTS_METHOD_BOOKEND( before, FIRST_ARGS, &rect.xMin, &rect.yMin, &rect.xMax, &rect.yMax )	\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( GetSelfBounds, rect )											\
		CORONA_OBJECTS_METHOD_BOOKEND( after, FIRST_ARGS, &rect.xMin, &rect.yMin, &rect.xMax, &rect.yMax )	\
	}	\
		\
	virtual void GetSelfBoundsForAnchor( Rtt::Rect & rect ) const	\
	{																\
		STORE_THIS( DisplayObject );								\
		CORONA_OBJECTS_GET_PARAMS_SPECIFIC( GetSelfBoundsForAnchor, RectResult );							\
		CORONA_OBJECTS_METHOD_BOOKEND( before, FIRST_ARGS, &rect.xMin, &rect.yMin, &rect.xMax, &rect.yMax )	\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( GetSelfBoundsForAnchor, rect )								\
		CORONA_OBJECTS_METHOD_BOOKEND( after, FIRST_ARGS, &rect.xMin, &rect.yMin, &rect.xMax, &rect.yMax )	\
	}	\
		\
	virtual bool HitTest( Rtt::Real contentX, Rtt::Real contentY )	\
	{																\
		STORE_THIS( DisplayObject );								\
		CORONA_OBJECTS_GET_PARAMS_SPECIFIC( HitTest, BooleanResultPoint );	\
		CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT( FIRST_ARGS, contentX, contentY )	\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS_AND_RESULT( HitTest, contentX, contentY )		\
		CORONA_OBJECTS_METHOD_BOOKEND( after, FIRST_ARGS, contentX, contentY, &result )		\
						\
		return result;	\
	}	\
		\
	virtual void Prepare( const Rtt::Display & display )	\
	{														\
		STORE_THIS( DisplayObject );						\
		CORONA_OBJECTS_GET_PARAMS_SPECIFIC( Prepare, Basic );		\
		CORONA_OBJECTS_METHOD_STRIP_ARGUMENT( Prepare, display )	\
	}	\
		\
	virtual void RemovedFromParent( lua_State * L, Rtt::GroupObject * parent )		\
	{																				\
		STORE_THIS( DisplayObject );		\
		STORE_VALUE( parent, GroupObject );	\
		CORONA_OBJECTS_GET_PARAMS( RemovedFromParent );										\
		CORONA_OBJECTS_METHOD_BOOKEND( before, FIRST_ARGS, L, parentStored.GetHandle() );	\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS( AddedToParent, L, parent );					\
		CORONA_OBJECTS_METHOD_BOOKEND( after, FIRST_ARGS, L, parentStored.GetHandle() );	\
	}	\
		\
	virtual void Rotate( Rtt::Real deltaTheta )								\
	{																		\
		STORE_THIS( DisplayObject );										\
		CORONA_OBJECTS_GET_PARAMS( Rotate );					\
		CORONA_OBJECTS_METHOD_WITH_ARGS( Rotate, deltaTheta )	\
	}	\
		\
	virtual void Scale( Rtt::Real sx, Rtt::Real sy, bool isNewValue )	\
	{																	\
		STORE_THIS( DisplayObject );									\
		CORONA_OBJECTS_GET_PARAMS( Scale );								\
		CORONA_OBJECTS_METHOD_WITH_ARGS( Scale, sx, sy, isNewValue )	\
	}	\
		\
	virtual void SendMessage( const char * message, const void * payload, U32 size ) const	\
	{																						\
		STORE_THIS( DisplayObject );														\
		CORONA_OBJECTS_GET_PARAMS( OnMessage );	\
												\
		if (params.action)											\
		{															\
			params.action( FIRST_ARGS, message, payload, size );	\
		}															\
	}	\
		\
	virtual void Translate( Rtt::Real deltaX, Rtt::Real deltaY )	\
	{																\
		STORE_THIS( DisplayObject );								\
		CORONA_OBJECTS_GET_PARAMS( Translate );							\
		CORONA_OBJECTS_METHOD_WITH_ARGS( Translate, deltaX, deltaY )	\
	}	\
		\
	virtual bool UpdateTransform( const Rtt::Matrix & parentToDstSpace )	\
	{																		\
		STORE_THIS( DisplayObject );										\
		CORONA_OBJECTS_INIT_MATRIX( parentToDstSpace );											\
		CORONA_OBJECTS_GET_PARAMS_SPECIFIC( UpdateTransform, BooleanResultMatrix );				\
		CORONA_OBJECTS_METHOD_BEFORE_WITH_BOOLEAN_RESULT( FIRST_ARGS, matrix )					\
		CORONA_OBJECTS_METHOD_CORE_WITH_ARGS_AND_RESULT( UpdateTransform, parentToDstSpace )	\
		CORONA_OBJECTS_METHOD_BOOKEND( after, FIRST_ARGS, matrix, &result )						\
						\
		return result;	\
	}	\
		\
	virtual void WillMoveOnscreen()		\
	{									\
		STORE_THIS( DisplayObject );	\
		CORONA_OBJECTS_GET_PARAMS_SPECIFIC( WillMoveOnscreen, Basic );	\
		CORONA_OBJECTS_METHOD( WillMoveOnscreen )						\
	}	\
		\
	virtual const Rtt::LuaProxyVTable& ProxyVTable() const { return OBJECT_KIND##2ProxyVTable::Constant(); }	\
																												\
	unsigned char * fStream;	\
	mutable void * fUserData;	\
	int fRef

CORONA_API
int CoronaObjectsBuildMethodStream( lua_State * L, const CoronaObjectParamsHeader * head )
{
	if (BuildMethodStream( L, head )) // ...[, stream]
	{
		return luaL_ref( L, LUA_REGISTRYINDEX ); // ...; registry = { ..., [ref] = stream }
	}

	return LUA_REFNIL;
}

#define CORONA_OBJECTS_DEFAULT_INITIALIZATION() fStream( NULL ), fUserData( NULL ), fRef( LUA_NOREF )

/**
TODO
*/
CORONA_OBJECTS_VTABLE( Group, Group );

class Group2 : public Rtt::GroupObject {
	Rtt_CLASS_NO_COPIES( Group2 )

public:
	typedef Group2 Self;
	typedef Rtt::GroupObject Super;

public:
	static Super *
	New( Rtt_Allocator * allocator, Rtt::StageObject * stageObject )
	{
		Self * group = Rtt_NEW( allocator, Self( allocator, NULL ) );

		CORONA_OBJECTS_ON_CREATE( group );

		return group;
	}

protected:
	Group2( Rtt_Allocator * allocator, Rtt::StageObject * stageObject )
		: Super( allocator, stageObject ),
		CORONA_OBJECTS_DEFAULT_INITIALIZATION()
	{
	}

public:
	virtual void DidInsert( bool childParentChanged )
	{
		STORE_THIS( GroupObject );
		CORONA_OBJECTS_GET_PARAMS( DidInsert );
		CORONA_OBJECTS_METHOD_WITH_ARGS( DidInsert, childParentChanged )
	}

	virtual void DidRemove()
	{
		STORE_THIS( GroupObject );
		CORONA_OBJECTS_GET_PARAMS_SPECIFIC( DidRemove, GroupBasic );
		CORONA_OBJECTS_METHOD( DidRemove )
	}

	CORONA_OBJECTS_INTERFACE( Group );
};

CORONA_API
int CoronaObjectsPushGroup( lua_State * L, void * userData, const CoronaObjectParams * params )
{
	CORONA_OBJECTS_PUSH( Group );
}

/**
TODO
*/
CORONA_OBJECTS_VTABLE( Mesh, Shape );

class Mesh2 : public Rtt::ShapeObject {
	Rtt_CLASS_NO_COPIES( Mesh2 )

public:
	typedef Mesh2 Self;
	typedef Rtt::ShapeObject Super;

public:
	static Super *
	New( Rtt_Allocator * allocator, Rtt::ClosedPath * path )
	{
		Self * mesh = Rtt_NEW( allocator, Self( path ) );

		CORONA_OBJECTS_ON_CREATE( mesh );

		return mesh;
	}

protected:
	Mesh2( Rtt::ClosedPath * path )
		: Super( path ),
		CORONA_OBJECTS_DEFAULT_INITIALIZATION()
	{
	}

public:
	CORONA_OBJECTS_INTERFACE( Mesh );
};

CORONA_API
int CoronaObjectsPushMesh( lua_State * L, void * userData, const CoronaObjectParams * params )
{
	CORONA_OBJECTS_PUSH( Mesh );
}

/**
TODO
*/
CORONA_OBJECTS_VTABLE( Polygon, Shape );

class Polygon2 : public Rtt::ShapeObject {
	Rtt_CLASS_NO_COPIES( Polygon2 )

public:
	typedef Polygon2 Self;
	typedef Rtt::ShapeObject Super;

public:
	static Super *
	New( Rtt_Allocator * allocator, Rtt::ClosedPath * path )
	{
		Self * polygon = Rtt_NEW( allocator, Self( path ) );

		CORONA_OBJECTS_ON_CREATE( polygon );

		return polygon;
	}

protected:
	Polygon2( Rtt::ClosedPath * path )
		: Super( path ),
		CORONA_OBJECTS_DEFAULT_INITIALIZATION()
	{
	}

public:
	CORONA_OBJECTS_INTERFACE( Polygon );
};

CORONA_API
int CoronaObjectsPushPolygon( lua_State * L, void * userData, const CoronaObjectParams * params )
{
	CORONA_OBJECTS_PUSH( Polygon );
}

/**
TODO
*/
CORONA_OBJECTS_VTABLE( Rect, Shape );

class Rect2 : public Rtt::RectObject {
	Rtt_CLASS_NO_COPIES( Rect2 )

public:
	typedef Rect2 Self;
	typedef Rtt::RectObject Super;

public:
	static Super *
	New( Rtt_Allocator* pAllocator, Rtt::Real width, Rtt::Real height )
	{
		Rtt::RectPath * path = Rtt::RectPath::NewRect( pAllocator, width, height );
		Self * rect = Rtt_NEW( pAllocator, Self( path ) );

		CORONA_OBJECTS_ON_CREATE( rect );

		return rect;
	}

protected:
	Rect2( Rtt::RectPath * path )
		: Super( path ),
		CORONA_OBJECTS_DEFAULT_INITIALIZATION()
	{
	}

public:
	CORONA_OBJECTS_INTERFACE( Rect );
};

CORONA_API
int CoronaObjectsPushRect( lua_State * L, void * userData, const CoronaObjectParams * params )
{
	CORONA_OBJECTS_PUSH( Rect );
}

/**
TODO
*/
CORONA_OBJECTS_VTABLE( Snapshot, Snapshot );

class Snapshot2 : public Rtt::SnapshotObject {
	Rtt_CLASS_NO_COPIES( Snapshot2 )

public:
	typedef Snapshot2 Self;
	typedef Rtt::SnapshotObject Super;

public:
	static Super *
	New( Rtt_Allocator * allocator, Rtt::Display & display, Rtt::Real width, Rtt::Real height )
	{
		Self * snapshot = Rtt_NEW( allocator, Self( allocator, display, width, height ) );

		CORONA_OBJECTS_ON_CREATE( snapshot );

		return snapshot;
	}

protected:
	Snapshot2( Rtt_Allocator * allocator, Rtt::Display & display, Rtt::Real contentW, Rtt::Real contentH )
		: Super( allocator, display, contentW, contentH ),
		CORONA_OBJECTS_DEFAULT_INITIALIZATION()
	{
	}

public:
	CORONA_OBJECTS_INTERFACE( Snapshot );
};

CORONA_API
int CoronaObjectsPushSnapshot( lua_State * L, void * userData, const CoronaObjectParams * params )
{
	CORONA_OBJECTS_PUSH( Snapshot );
}

CORONA_API
int CoronaObjectsShouldDraw( const CoronaDisplayObjectHandle object, int * shouldDraw )
{
    // TODO: look for proxy on stack, validate object?
    // If so, then *shouldDraw = ((DisplayObject *)object)->shouldDraw()

    return 0;
}

CORONA_API
const CoronaGroupObjectHandle CoronaObjectGetParent( const CoronaDisplayObjectHandle object )
{
	const Rtt::DisplayObject * displayObject = static_cast< const Rtt::GroupObject * >( CoronaExtractConstantDisplayObject( object ) );

	if (displayObject)
	{
		auto ref = CoronaInternalStoreGroupObject( displayObject->GetParent() );

		ref.Forget();

		return ref.GetHandle();
	}

	return NULL;
}

CORONA_API
const CoronaDisplayObjectHandle CoronaGroupObjectGetChild( const CoronaGroupObjectHandle groupObject, int index )
{
	const Rtt::GroupObject * go = static_cast< const Rtt::GroupObject * >( CoronaExtractConstantGroupObject( groupObject ) );

	if (go && index >= 0 && index < go->NumChildren())
	{
		auto ref = CoronaInternalStoreDisplayObject( &go->ChildAt( index ) );

		ref.Forget();

		return ref.GetHandle();
	}

	return NULL;
}

CORONA_API
int CoronaGroupObjectGetNumChildren( const CoronaGroupObjectHandle groupObject )
{
	const Rtt::GroupObject * go = static_cast< const Rtt::GroupObject * >( CoronaExtractConstantGroupObject( groupObject ) );

	return go ? go->NumChildren() : 0;
}

CORONA_API
int CoronaObjectSendMessage( const CoronaDisplayObjectHandle object, const char * message, const void * payload, unsigned int size )
{
	const Rtt::DisplayObject * displayObject = static_cast< const Rtt::GroupObject * >( CoronaExtractConstantDisplayObject( object ) );

	if (displayObject)
	{
		displayObject->SendMessage( message, payload, size );

		return 1;
	}

	return 0;
}

#undef SIZE_MINUS_OFFSET
#undef OFFSET_OF_MEMBER
#undef AFTER_HEADER_STRUCT
#undef AFTER_HEADER_FLAG
#undef AFTER_HEADER_ACTION
#undef AFTER_HEADER_FLAG_OFFSET
#undef AFTER_HEADER_ACTION_OFFSET
#undef FIRST_ARGS
#undef STORE_THIS
#undef STORE_VALUE