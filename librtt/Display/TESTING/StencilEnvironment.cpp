//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#include "Stencil.h"

StencilEnvironment * InitStencilEnvironment( lua_State * L )
{
	static int sNonce;

	auto state = GetOrNew< StencilEnvironment >( L, &sNonce, true );

	if (state.isNew)
	{
		CoronaCommand command = {
			[](const U8 * data, unsigned int size) {
				int clear;

				Rtt_ASSERT( size >= sizeof( int ) );

				memcpy( &clear, data, sizeof( int ) );

				glClearStencil( GLint( clear ) ); // TODO: is this expensive? if so, avoid when possible...
				glClear( GL_STENCIL_BUFFER_BIT );
			}, CopyWriter
		};

		CoronaRendererRegisterCommand( L, &state.object->command, &command );

		CoronaRendererRegisterBeginFrameOp( L, &state.object->beginFrameOp, [](CoronaRendererHandle rendererHandle, void * userData) {
			StencilEnvironment * _this = static_cast< StencilEnvironment * >( userData );
			int clear = 0;

			CoronaRendererIssueCommand( rendererHandle, _this->command, &clear, sizeof( int ) );
		}, state.object );
		CoronaRendererRegisterClearOp( L, &state.object->clearOp, [](CoronaRendererHandle rendererHandle, void * userData) {
			StencilEnvironment * _this = static_cast< StencilEnvironment * >( userData );

			CoronaRendererIssueCommand( rendererHandle, _this->command, &_this->clear, sizeof( int ) );
		}, state.object );
	}

	return state.object;
}