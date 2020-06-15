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
    typedef void (*BasicBookend) (void * object, void * userData);

    BasicBookend beforeAddedToParent;
    BasicBookend afterAddedToParent;

    // TODO:
    typedef void (*BooleanResultBookend) (void * object, void * userData, int * result);

    BooleanResultBookend beforeCanCull;
    BooleanResultBookend afterCanCull;

    BooleanResultBookend beforeCanHitTest;
    BooleanResultBookend afterCanHitTest;

    BasicBookend beforeDidMoveOffscreen;
    BasicBookend afterDidMoveOffscreen;

    // TODO:
    typedef void (*MatrixBookend) (void * object, void * userData, float matrix[9]);

    MatrixBookend beforeDidUpdateTransform;
    MatrixBookend afterDidUpdateTransform;

    BasicBookend beforeDraw;
    BasicBookend afterDraw;

    // TODO:
    typedef void (*RectResultBookend) (void * object, void * userData, float * xMin, float * yMin, float * xMax, float * yMax);

    RectResultBookend beforeGetSelfBounds;
    RectResultBookend afterGetSelfBounds;

    RectResultBookend beforeGetSelfBoundsForAnchor;
    RectResultBookend afterGetSelfBoundsForAnchor;

    // TODO:
    typedef void (*BooleanResultPointBookend) (void * object, void * userData, float x, float y, int * result);

    BooleanResultPointBookend beforeHitTest;
    BooleanResultPointBookend afterHitTest;

    BasicBookend beforePrepare;
    BasicBookend afterPrepare;

    // TODO:
    typedef void (*RemovedFromParentBookend) (void * object, void * userData, lua_State * L, void * groupObject);

    RemovedFromParentBookend beforeRemovedFromParent;
    RemovedFromParentBookend afterRemovedFromParent;

    // TODO:
    typedef void (*RotateBookend) (void * object, void * userData, float delta);

    RotateBookend beforeRotate;
    RotateBookend afterRotate;

    // TODO:
    typedef void (*ScaleBookend) (void * object, void * userData, float sx, float sy, int isNew);

    ScaleBookend beforeScale;
    ScaleBookend afterScale;

    // TODO:
    typedef void (*SetValueBookend) (void * object, void * userData, lua_State * L, const char key[], int valueIndex, int * result);

    SetValueBookend beforeSetValue;
    SetValueBookend afterSetValue;

    // TODO:
    typedef void (*TranslateBookend) (void * object, void * userData, float x, float y);

    TranslateBookend beforeTranslate;
    TranslateBookend afterTranslate;

    // TODO:
    typedef void (*BooleanResultMatrixBookend) (void * object, void * userData, const float matrix[9], int * result);

    BooleanResultMatrixBookend beforeUpdateTransform;
    BooleanResultMatrixBookend afterUpdateTransform;

    // TODO:
    typedef void (*ValueBookend) (void * object, void * userData, lua_State * L, const char key[], int * result);

    ValueBookend beforeValue;
    ValueBookend afterValue;

    BasicBookend beforeWillMoveOnscreen;
    BasicBookend afterWillMoveOnscreen;

    //
    // Lifetime and custom state
    //

    void * userData;

    void (*onCreate) (void * object, void * userData);
    void (*onFinalize) (void * object, void * userData);

    //
    // Flags
    //

    typedef unsigned long long Flags;

    // put these last in case inheriting types can merge with them
    Flags ignoreOriginalAddedToParent : 1; // only use before and / or after, ignoring the original method
    Flags ignoreOriginalMoveOffscreen : 1;

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

    Flags ignoreOriginalDidMoveOffscreen : 1;
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
    Flags ignoreOriginalUpdateTransform : 1;
    Flags updateTransformConditionallyOverwrite : 1;
    Flags updateTransformOverwriteResult : 1;

    Flags valueDisallowEarlyOut : 1; // see canCull notes
    Flags valueNotEarlyOutResult : 1;
    Flags ignoreOriginalValue : 1;
    Flags valueConditionallyOverwrite : 1;
    Flags valueOverwriteResult : 1;

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

CORONA_API
int CoronaObjectsPushGroup (lua_State * L, const GroupParams * params) CORONA_PUBLIC_SUFFIX;

typedef struct ShapeParams {
    DisplayObjectParams inherited;
} ShapeParams;

CORONA_API
int CoronaObjectsPushRect (lua_State * L, const ShapeParams * params) CORONA_PUBLIC_SUFFIX;

typedef struct SnapshotParams {
    DisplayObjectParams inherited;
    // TODO: render format
} SnapshotParams;

CORONA_API
int CoronaObjectsPushSnapshot (lua_State * L, const SnapshotParams * params) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaObjectsShouldDraw (void * object, int * shouldDraw) CORONA_PUBLIC_SUFFIX;

#endif // _CoronaObjects_H__
