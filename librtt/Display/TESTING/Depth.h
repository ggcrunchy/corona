//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#ifndef _DepthTesting_H__
#define _DepthTesting_H__

#include "TESTING.h"
#include "Renderer/Rtt_GL.h"
#include <vector>

struct DepthSettings {
	int func{GL_LESS}, cullFace{GL_BACK}, frontFace{GL_CCW};
	double near{0.}, far{1.};
	bool enabled{false}, mask{true};
};

struct DepthInfo {
	double clear{1.};
	CoronaMatrix4x4 projectionMatrix, viewMatrix;
	DepthSettings settings;
};

struct DepthEnvironment {
	DepthInfo current, working;
	std::vector<DepthInfo> stack;
	CoronaBeginFrameOpHandle beginFrameOp = {};
	CoronaClearOpHandle clearOp = {};
	CoronaCommandHandle command = {};
	U32 id;
	double clear{1.}; // TODO: should add some way to set this, too...
	bool anySinceClear{false};
	bool hasSetID{false};
	bool matricesValid{false};
};

DepthEnvironment * InitDepthEnvironment( lua_State * L );

const char * FillMatrixFromArray( lua_State * L, int index, const char * what, CoronaMatrix4x4 matrix );
void FillArrayFromMatrix( lua_State * L, int index, const CoronaMatrix4x4 matrix );

extern "C" {
	int DepthClearObject( lua_State * L );
	int DepthStateObject( lua_State * L );
}

#endif // _DepthTesting_H__