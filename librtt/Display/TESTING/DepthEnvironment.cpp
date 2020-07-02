//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#include "Depth.h"

DepthEnvironment * InitDepthEnvironment( lua_State * L )
{
	static int sNonce;

	auto env = GetOrNew< DepthEnvironment >( L, &sNonce, true );

	if (env.isNew)
	{
		CoronaCommand command = {
			[](const U8 * data, unsigned int size) {
				double clear;

				Rtt_ASSERT( size >= sizeof( double ) );

				memcpy( &clear, data, sizeof( double ) );

				Rtt_glClearDepth( GLdouble( clear ) ); // TODO: is this expensive? if so, avoid when possible...
				glClear( GL_DEPTH_BUFFER_BIT );
			}, CopyWriter
		};

		CoronaRendererRegisterCommand( L, &env.object->command, &command );
		CoronaRendererRegisterBeginFrameOp( L, &env.object->beginFrameOp, [](CoronaRendererHandle rendererHandle, void * userData) {
			DepthEnvironment * _this = static_cast< DepthEnvironment * >( userData );
			double clear = 1.;

			CoronaRendererIssueCommand( rendererHandle, _this->command, &clear, sizeof( double ) );
		}, env.object );
		CoronaRendererRegisterClearOp( L, &env.object->clearOp, [](CoronaRendererHandle rendererHandle, void * userData) {
			DepthEnvironment * _this = static_cast< DepthEnvironment * >( userData );

			CoronaRendererIssueCommand( rendererHandle, _this->command, &_this->clear, sizeof( double ) );
		}, env.object );
	}

	return env.object;
}