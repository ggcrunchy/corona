//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#ifndef _CoronaObjects_H__
#define _CoronaObjects_H__

#include "CoronaMacros.h"


#ifdef __cplusplus
extern "C" {
#endif
	typedef struct lua_State lua_State;
#ifdef __cplusplus
}
#endif


// C API
// ----------------------------------------------------------------------------

/**
 Most of this assumes some familiarity with the source, in particular from librtt\Display:

 Rtt_MDrawable.h, Rtt_DisplayObject.h, Rtt_GroupObject.h, Rtt_Snapshot.h

 TODO: backend, e.g. texture / FBO stuff
*/

typedef unsigned long long CoronaObjectFlags;

typedef enum {
    kAugmentedMethod_None,

    /* The first few seem to come up most often in tests, so favor them in sorts */
    kAugmentedMethod_Draw, // uses "own" type, i.e. `CoronaObjectDrawParams`
    kAugmentedMethod_CanCull, // BooleanResult, i.e. uses `CoronaObjectBooleanResultParams` type
    kAugmentedMethod_CanHitTest, // BooleanResult
    kAugmentedMethod_OnMessage,
    kAugmentedMethod_SetValue,
    kAugmentedMethod_Value,
    kAugmentedMethod_OnFinalize, // Lifetime

    /* No clear pattern for the rest */
    kAugmentedMethod_AddedToParent,
    kAugmentedMethod_DidMoveOffscreen, // Basic
    kAugmentedMethod_DidUpdateTransform, // Matrix
    kAugmentedMethod_GetSelfBounds, // RectResult
    kAugmentedMethod_GetSelfBoundsForAnchor, // RectResult
    kAugmentedMethod_HitTest, // BooleanResultPoint
    kAugmentedMethod_OnCreate, // Lifetime
    kAugmentedMethod_Prepare, // Basic
    kAugmentedMethod_RemovedFromParent,
    kAugmentedMethod_Rotate,
    kAugmentedMethod_Scale,
    kAugmentedMethod_Translate,
    kAugmentedMethod_UpdateTransform, // BooleanResultMatrix
    kAugmentedMethod_WillMoveOnscreen, // Basic

    /* These are relevant to groups */
    kAugmentedMethod_DidInsert,
    kAugmentedMethod_DidRemove, // Basic

    kAugmentedMethod_Count
} CoronaObjectAugmentedMethod;

typedef struct CoronaObjectParamsHeader {
    struct CoronaObjectParamsHeader * next;
    unsigned short method; // n.b. quite generous: all methods fit easily within a byte)
} CoronaObjectParamsHeader;

#define CORONA_OBJECTS_BOOKENDED_PARAMS(NAME, ...)                \
    typedef void (*CoronaObject##NAME##Bookend) (__VA_ARGS__);    \
                                                                  \
    typedef struct CoronaObject##NAME##Params {     \
        CoronaObjectParamsHeader header;            \
        unsigned short ignoreOriginal;              \
        CoronaObject##NAME##Bookend before, after;  \
    } CoronaObject##NAME##Params

CORONA_OBJECTS_BOOKENDED_PARAMS( Basic, const void * object, void * userData ); // CoronaObjectBasicBookend, CoronaObjectBasicParams...
CORONA_OBJECTS_BOOKENDED_PARAMS( AddedToParent, const void * object, void * userData, lua_State * L, void * groupObject ); // ...and so on
CORONA_OBJECTS_BOOKENDED_PARAMS( Matrix, const void * object, void * userData, float matrix[6] );
CORONA_OBJECTS_BOOKENDED_PARAMS( Draw, const void * object, void * userData, const struct CoronaGraphicsToken * rendererToken );
CORONA_OBJECTS_BOOKENDED_PARAMS( RectResult, const void * object, void * userData, float * xMin, float * yMin, float * xMax, float * yMax );
CORONA_OBJECTS_BOOKENDED_PARAMS( RemovedFromParent, const void * object, void * userData, lua_State * L, void * groupObject );
CORONA_OBJECTS_BOOKENDED_PARAMS( Rotate, const void * object, void * userData, float delta );
CORONA_OBJECTS_BOOKENDED_PARAMS( Scale, const void * object, void * userData, float sx, float sy, int isNew );
CORONA_OBJECTS_BOOKENDED_PARAMS( Translate, const void * object, void * userData, float x, float y );
CORONA_OBJECTS_BOOKENDED_PARAMS( DidInsert, void * groupObject, void * userData, int childParentChanged );

#define CORONA_OBJECTS_EARLY_OUTABLE_BOOKENDED_PARAMS(NAME, ...)    \
    typedef void (*CoronaObject##NAME##Bookend) (__VA_ARGS__);      \
                                                                    \
    typedef struct CoronaObject##NAME##Params {             \
        CoronaObjectParamsHeader header;                    \
        unsigned char ignoreOriginal, earlyOutIfNonZero;    \
        CoronaObject##NAME##Bookend before, after;          \
    } CoronaObject##NAME##Params

CORONA_OBJECTS_EARLY_OUTABLE_BOOKENDED_PARAMS( BooleanResult, const void * object, void * userData, int * result );
CORONA_OBJECTS_EARLY_OUTABLE_BOOKENDED_PARAMS( BooleanResultPoint, const void * object, void * userData, float x, float y, int * result );
CORONA_OBJECTS_EARLY_OUTABLE_BOOKENDED_PARAMS( BooleanResultMatrix, const void * object, void * userData, const float matrix[6], int * result );

typedef void (*CoronaObjectSetValueBookend) (const void * object, void * userData, lua_State * L, const char key[], int valueIndex, int * result );

typedef struct CoronaObjectSetValueParams {
    CoronaObjectParamsHeader header;
    unsigned char ignoreOriginal, disallowEarlyOut;
    CoronaObjectSetValueBookend before, after;
} CoronaObjectSetValueParams;

typedef void (*CoronaObjectValueBookend) (const void * object, void * userData, lua_State * L, const char key[], int * result );

typedef struct CoronaObjectValueParams {
    CoronaObjectParamsHeader header;
    unsigned char ignoreOriginal, disallowEarlyOut : 1, earlyOutIfZero : 1;
    CoronaObjectValueBookend before, after;
} CoronaObjectValueParams;

typedef struct CoronaObjectLifetimeParams {
    CoronaObjectParamsHeader header;
    CoronaObjectBasicBookend action;
} CoronaObjectLifetimeParams;

typedef struct CoronaObjectOnMessageParams {
    CoronaObjectParamsHeader header;
    void (*action)(const void * object, void * userData, const char * message, const void * data, unsigned int size);
} CoronaObjectOnMessageParams;

typedef struct CoronaObjectParams {
    union {
        CoronaObjectParamsHeader * head;
        int ref;
    } u;
    int useRef;
} CoronaObjectsParams;

CORONA_API
int CoronaObjectsBuildMethodStream( lua_State * L, const CoronaObjectParamsHeader * head ) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaObjectsPushGroup( lua_State * L, void * userData, const CoronaObjectParams * params ) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaObjectsPushRect( lua_State * L, void * userData, const CoronaObjectParams * params ) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaObjectsPushSnapshot( lua_State * L, void * userData, const CoronaObjectParams * params ) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaObjectsShouldDraw( const void * object, int * shouldDraw ) CORONA_PUBLIC_SUFFIX;

CORONA_API
const void * CoronaObjectGetParent( const void * object ) CORONA_PUBLIC_SUFFIX;

CORONA_API
const void * CoronaGroupObjectGetChild( const void * groupObject, int index ) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaGroupObjectGetNumChildren( const void * groupObject ) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaObjectSendMessage( const void * object, const char * message, const void * payload, unsigned int size ) CORONA_PUBLIC_SUFFIX;

#endif // _CoronaObjects_H__
