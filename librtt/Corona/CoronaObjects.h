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

typedef struct DisplayObjectParams {
    //
    // Bookends
    //

    // TODO:
    typedef void (*AddedToParentBookend) (const void * object, void * userData, lua_State * L, void * groupObject);

    AddedToParentBookend beforeAddedToParent;
    AddedToParentBookend afterAddedToParent;

    // TODO:
    typedef void (*BooleanResultBookend) (const void * object, void * userData, int * result);

    BooleanResultBookend beforeCanCull;
    BooleanResultBookend afterCanCull;

    BooleanResultBookend beforeCanHitTest;
    BooleanResultBookend afterCanHitTest;

    // TODO:
    typedef void (*BasicBookend) (const void * object, void * userData);

    BasicBookend beforeDidMoveOffscreen;
    BasicBookend afterDidMoveOffscreen;

    // TODO:
    typedef void (*MatrixBookend) (const void * object, void * userData, float matrix[9]);

    MatrixBookend beforeDidUpdateTransform;
    MatrixBookend afterDidUpdateTransform;

    BasicBookend beforeDraw;
    BasicBookend afterDraw;

    // TODO:
    typedef void (*RectResultBookend) (const void * object, void * userData, float * xMin, float * yMin, float * xMax, float * yMax);

    RectResultBookend beforeGetSelfBounds;
    RectResultBookend afterGetSelfBounds;

    RectResultBookend beforeGetSelfBoundsForAnchor;
    RectResultBookend afterGetSelfBoundsForAnchor;

    // TODO:
    typedef void (*BooleanResultPointBookend) (const void * object, void * userData, float x, float y, int * result);

    BooleanResultPointBookend beforeHitTest;
    BooleanResultPointBookend afterHitTest;

    BasicBookend beforePrepare;
    BasicBookend afterPrepare;

    // TODO:
    typedef void (*RemovedFromParentBookend) (const void * object, void * userData, lua_State * L, void * groupObject);

    RemovedFromParentBookend beforeRemovedFromParent;
    RemovedFromParentBookend afterRemovedFromParent;

    // TODO:
    typedef void (*RotateBookend) (const void * object, void * userData, float delta);

    RotateBookend beforeRotate;
    RotateBookend afterRotate;

    // TODO:
    typedef void (*ScaleBookend) (const void * object, void * userData, float sx, float sy, int isNew);

    ScaleBookend beforeScale;
    ScaleBookend afterScale;

    // TODO:
    typedef void (*SetValueBookend) (const void * object, void * userData, lua_State * L, const char key[], int valueIndex, int * result);

    SetValueBookend beforeSetValue;
    SetValueBookend afterSetValue;

    // TODO:
    typedef void (*TranslateBookend) (const void * object, void * userData, float x, float y);

    TranslateBookend beforeTranslate;
    TranslateBookend afterTranslate;

    // TODO:
    typedef void (*BooleanResultMatrixBookend) (const void * object, void * userData, const float matrix[9], int * result);

    BooleanResultMatrixBookend beforeUpdateTransform;
    BooleanResultMatrixBookend afterUpdateTransform;

    // TODO:
    typedef void (*ValueBookend) (const const void * object, void * userData, lua_State * L, const char key[], int * result);

    ValueBookend beforeValue;
    ValueBookend afterValue;

    BasicBookend beforeWillMoveOnscreen;
    BasicBookend afterWillMoveOnscreen;

    //
    // Lifetime and custom state
    //

    void (*onCreate) (const void * object, void * userData);
    void (*onFinalize) (const void * object, void * userData);

    //
    // Flags
    //

    typedef unsigned long long Flags;

    // put these last in case inheriting types can merge with them
    Flags ignoreOriginalAddedToParent : 1; // only use before and / or after, ignoring the original method

    Flags earlyOutCanCullIfZero : 1; // in the case of boolean results, calls are assumed to early-out if "before" returned either true or false: use zero (false)?
    Flags ignoreOriginalCanCull : 1;

    Flags earlyOutCanHitTestIfZero : 1;
    Flags ignoreOriginalCanHitTest : 1;

    Flags ignoreOriginalDidMoveOffscreen : 1;
    Flags ignoreOriginalDidUpdateTransform : 1;
    Flags ignoreOriginalDraw : 1;
    Flags ignoreOriginalGetSelfBounds : 1;
    Flags ignoreOriginalGetSelfBoundsForAnchor : 1;

    Flags earlyOutHitTestIfZero : 1;
    Flags ignoreOriginalHitTest : 1;

    Flags ignoreOriginalMoveOffscreen : 1;
    Flags ignoreOriginalPrepare : 1;
    Flags ignoreOriginalRemovedFromParent : 1;
    Flags ignoreOriginalRotate : 1;
    Flags ignoreOriginalScale : 1;

    Flags setValueDisallowEarlyOut : 1; // usually want to early-out if we set a value, but we can suppress this, say if we just wanted a side effect
    Flags ignoreOriginalSetValue : 1;

    Flags ignoreOriginalTranslate : 1;

    Flags earlyOutUpdateTransformIfZero : 1;
    Flags ignoreOriginalUpdateTransform : 1;

    Flags valueDisallowEarlyOut : 1; // usually we want to early-out if we got a value, but we can suppress this, say if we want to transform the result
    Flags valueEarlyOutIfZero : 1;
    Flags ignoreOriginalValue : 1;

    Flags ignoreOriginalWillMoveOnscreen : 1;
} DisplayObjectParams;

typedef struct GroupParams {
    DisplayObjectParams inherited;

    // put these here in case they can be merged into the inherited bitfields
    DisplayObjectParams::Flags ignoreOriginalDidInsert : 1;
    DisplayObjectParams::Flags ignoreOriginalDidRemove : 1;

    // TODO:
    typedef void (*DidInsertBookend) (void * groupObject, void * userData, int childParentChanged);

    DidInsertBookend beforeDidInsert;
    DidInsertBookend afterDidInsert;

    DisplayObjectParams::BasicBookend beforeDidRemove;
    DisplayObjectParams::BasicBookend afterDidRemove;
} GroupParams;

// TODO: allow params to be shared / reused, i.e. what we almost always want

CORONA_API
int CoronaObjectsPushGroup (lua_State * L, void * userData, const GroupParams * params, int temporaryParams) CORONA_PUBLIC_SUFFIX;

typedef struct ShapeParams {
    DisplayObjectParams inherited;
} ShapeParams;

CORONA_API
int CoronaObjectsPushRect (lua_State * L, void * userData, const ShapeParams * params, int temporaryParams) CORONA_PUBLIC_SUFFIX;

typedef struct SnapshotParams {
    DisplayObjectParams inherited;
    // TODO: render format
} SnapshotParams;

CORONA_API
int CoronaObjectsPushSnapshot (lua_State * L, void * userData, const SnapshotParams * params, int temporaryParams) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaObjectsShouldDraw (void * object, int * shouldDraw) CORONA_PUBLIC_SUFFIX;

#endif // _CoronaObjects_H__
