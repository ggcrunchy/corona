//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_VulkanExports_H__
#define _Rtt_VulkanExports_H__

#ifdef _WIN32
	#include <windows.h>
#endif

// ----------------------------------------------------------------------------

struct Rtt_Allocator;

namespace Rtt
{

class Renderer;

// ----------------------------------------------------------------------------

struct VulkanSurfaceParams {

#ifdef _WIN32
	HINSTANCE fInstance;
	HWND fWindowHandle;
#endif

};

class VulkanExports {
public:
	static bool CreateVulkanState( const VulkanSurfaceParams & params, void ** state );
	static void PopulateMultisampleDetails( void * state );
	static void DestroyVulkanState( void * state );
	static Renderer * CreateVulkanRenderer( Rtt_Allocator * allocator, void * state );
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanExports_H__