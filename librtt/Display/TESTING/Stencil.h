//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#ifndef _StencilTesting_H__
#define _StencilTesting_H__

#include "TESTING.h"
#include "Renderer/Rtt_GL.h"
#include <vector>

struct StencilSettings {
	int func{GL_ALWAYS}, funcRef{0}, fail{GL_KEEP}, zfail{GL_KEEP}, zpass{GL_KEEP};
	unsigned int funcMask{~0U}, mask{~0U};
	bool enabled{false};
};

struct StencilInfo {
	int clear{0};
	StencilSettings settings;
};

struct StencilEnvironment {
	StencilInfo current, working;
	std::vector<StencilInfo> stack;
	CoronaBeginFrameOpHandle beginFrameOp = {};
	CoronaClearOpHandle clearOp = {};
	CoronaCommandHandle command = {};
	U32 id;
	int clear{0}; // TODO: should add some way to set this, too...
	bool anySinceClear{false};
	bool hasSetID{false};
};

StencilEnvironment * InitStencilEnvironment( lua_State * L );

extern "C" {
	int StencilClearObject( lua_State * L );
	int StencilStateObject( lua_State * L );
}

#endif // _StencilTesting_H__