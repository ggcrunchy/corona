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

typedef void (*CoronaObjectBasicBookend) (const void * object, void * userData);
typedef void (*CoronaObjectAddedToParentBookend) (const void * object, void * userData, lua_State * L, void * groupObject);
typedef void (*CoronaObjectBooleanResultBookend) (const void * object, void * userData, int * result);
typedef void (*CoronaObjectMatrixBookend) (const void * object, void * userData, float matrix[6]);
typedef void (*CoronaObjectRectResultBookend) (const void * object, void * userData, float * xMin, float * yMin, float * xMax, float * yMax);
typedef void (*CoronaObjectBooleanResultPointBookend) (const void * object, void * userData, float x, float y, int * result);
typedef void (*CoronaObjectRemovedFromParentBookend) (const void * object, void * userData, lua_State * L, void * groupObject);
typedef void (*CoronaObjectRotateBookend) (const void * object, void * userData, float delta);
typedef void (*CoronaObjectScaleBookend) (const void * object, void * userData, float sx, float sy, int isNew);
typedef void (*CoronaObjectSetValueBookend) (const void * object, void * userData, lua_State * L, const char key[], int valueIndex, int * result);
typedef void (*CoronaObjectTranslateBookend) (const void * object, void * userData, float x, float y);
typedef void (*CoronaObjectBooleanResultMatrixBookend) (const void * object, void * userData, const float matrix[6], int * result);
typedef void (*CoronaObjectValueBookend) (const const void * object, void * userData, lua_State * L, const char key[], int * result);

typedef unsigned long long CoronaObjectFlags;

typedef struct CoronaDisplayObjectParams {
    //
    // Bookends
    //

    // TODO:

    CoronaObjectAddedToParentBookend beforeAddedToParent;
    CoronaObjectAddedToParentBookend afterAddedToParent;

    // TODO:

    CoronaObjectBooleanResultBookend beforeCanCull;
    CoronaObjectBooleanResultBookend afterCanCull;

    CoronaObjectBooleanResultBookend beforeCanHitTest;
    CoronaObjectBooleanResultBookend afterCanHitTest;

    // TODO:

    CoronaObjectBasicBookend beforeDidMoveOffscreen;
    CoronaObjectBasicBookend afterDidMoveOffscreen;

    // TODO:

    CoronaObjectMatrixBookend beforeDidUpdateTransform;
    CoronaObjectMatrixBookend afterDidUpdateTransform;

    CoronaObjectBasicBookend beforeDraw;
    CoronaObjectBasicBookend afterDraw;

    // TODO:

    CoronaObjectRectResultBookend beforeGetSelfBounds;
    CoronaObjectRectResultBookend afterGetSelfBounds;

    CoronaObjectRectResultBookend beforeGetSelfBoundsForAnchor;
    CoronaObjectRectResultBookend afterGetSelfBoundsForAnchor;

    // TODO:

    CoronaObjectBooleanResultPointBookend beforeHitTest;
    CoronaObjectBooleanResultPointBookend afterHitTest;

    CoronaObjectBasicBookend beforePrepare;
    CoronaObjectBasicBookend afterPrepare;

    // TODO:

    CoronaObjectRemovedFromParentBookend beforeRemovedFromParent;
    CoronaObjectRemovedFromParentBookend afterRemovedFromParent;

    // TODO:

    CoronaObjectRotateBookend beforeRotate;
    CoronaObjectRotateBookend afterRotate;

    // TODO:

    CoronaObjectScaleBookend beforeScale;
    CoronaObjectScaleBookend afterScale;

    // TODO:

    CoronaObjectSetValueBookend beforeSetValue;
    CoronaObjectSetValueBookend afterSetValue;

    // TODO:

    CoronaObjectTranslateBookend beforeTranslate;
    CoronaObjectTranslateBookend afterTranslate;

    // TODO:

    CoronaObjectBooleanResultMatrixBookend beforeUpdateTransform;
    CoronaObjectBooleanResultMatrixBookend afterUpdateTransform;

    // TODO:

    CoronaObjectValueBookend beforeValue;
    CoronaObjectValueBookend afterValue;

    CoronaObjectBasicBookend beforeWillMoveOnscreen;
    CoronaObjectBasicBookend afterWillMoveOnscreen;

    //
    // Lifetime and custom state
    //

    void (*onCreate) (const void * object, void * userData);
    void (*onFinalize) (const void * object, void * userData);

    //
    // Flags
    //

    // put these last in case inheriting types can merge with them
    CoronaObjectFlags ignoreOriginalAddedToParent : 1; // only use before and / or after, ignoring the original method

    CoronaObjectFlags earlyOutCanCullIfZero : 1; // in the case of boolean results, calls are assumed to early-out if "before" returned either true or false: use zero (false)?
    CoronaObjectFlags ignoreOriginalCanCull : 1;

    CoronaObjectFlags earlyOutCanHitTestIfZero : 1;
    CoronaObjectFlags ignoreOriginalCanHitTest : 1;

    CoronaObjectFlags ignoreOriginalDidMoveOffscreen : 1;
    CoronaObjectFlags ignoreOriginalDidUpdateTransform : 1;
    CoronaObjectFlags ignoreOriginalDraw : 1;
    CoronaObjectFlags ignoreOriginalGetSelfBounds : 1;
    CoronaObjectFlags ignoreOriginalGetSelfBoundsForAnchor : 1;

    CoronaObjectFlags earlyOutHitTestIfZero : 1;
    CoronaObjectFlags ignoreOriginalHitTest : 1;

    CoronaObjectFlags ignoreOriginalMoveOffscreen : 1;
    CoronaObjectFlags ignoreOriginalPrepare : 1;
    CoronaObjectFlags ignoreOriginalRemovedFromParent : 1;
    CoronaObjectFlags ignoreOriginalRotate : 1;
    CoronaObjectFlags ignoreOriginalScale : 1;

    CoronaObjectFlags setValueDisallowEarlyOut : 1; // usually want to early-out if we set a value, but we can suppress this, say if we just wanted a side effect
    CoronaObjectFlags ignoreOriginalSetValue : 1;

    CoronaObjectFlags ignoreOriginalTranslate : 1;

    CoronaObjectFlags earlyOutUpdateTransformIfZero : 1;
    CoronaObjectFlags ignoreOriginalUpdateTransform : 1;

    CoronaObjectFlags valueDisallowEarlyOut : 1; // usually we want to early-out if we got a value, but we can suppress this, say if we want to transform the result
    CoronaObjectFlags valueEarlyOutIfZero : 1;
    CoronaObjectFlags ignoreOriginalValue : 1;

    CoronaObjectFlags ignoreOriginalWillMoveOnscreen : 1;
} CoronaDisplayObjectParams;

typedef void (*GroupObjectDidInsertBookend) (void * groupObject, void * userData, int childParentChanged);

typedef struct CoronaGroupObjectParams {
    CoronaDisplayObjectParams inherited;

    // put these here in case they can be merged into the inherited bitfields
    CoronaObjectFlags ignoreOriginalDidInsert : 1;
    CoronaObjectFlags ignoreOriginalDidRemove : 1;

    // TODO:
    GroupObjectDidInsertBookend beforeDidInsert;
    GroupObjectDidInsertBookend afterDidInsert;

    CoronaObjectBasicBookend beforeDidRemove;
    CoronaObjectBasicBookend afterDidRemove;
} CoronaGroupObjectParams;

// TODO: allow params to be shared / reused, i.e. what we almost always want

CORONA_API
int CoronaObjectsPushGroup (lua_State * L, void * userData, const CoronaGroupObjectParams * params, int temporaryParams) CORONA_PUBLIC_SUFFIX;

typedef struct CoronaShapeObjectParams {
    CoronaDisplayObjectParams inherited;
} CoronaShapeObjectParams;

CORONA_API
int CoronaObjectsPushRect (lua_State * L, void * userData, const CoronaShapeObjectParams * params, int temporaryParams) CORONA_PUBLIC_SUFFIX;

typedef struct CoronaSnapshotObjectParams {
    CoronaDisplayObjectParams inherited;
    // TODO: render format
} CoronaSnapshotObjectParams;

CORONA_API
int CoronaObjectsPushSnapshot (lua_State * L, void * userData, const CoronaSnapshotObjectParams * params, int temporaryParams) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaObjectsShouldDraw (void * object, int * shouldDraw) CORONA_PUBLIC_SUFFIX;

#endif // _CoronaObjects_H__
