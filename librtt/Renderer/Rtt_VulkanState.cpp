//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_VulkanState.h"
#include "Core/Rtt_Assert.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

VulkanState::VulkanState()
:   fAllocationCallbacks( NULL ),
    fInstance( VK_NULL_HANDLE ),
#ifndef NDEBUG
	fDebugMessenger( VK_NULL_HANDLE ),
#endif
    fDevice( VK_NULL_HANDLE ),
    fPhysicalDevice( VK_NULL_HANDLE )
{
}

VulkanState::~VulkanState()
{
    vkDestroyDevice(fDevice, fAllocationCallbacks);
#ifndef NDEBUG
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(fInstance, "vkDestroyDebugUtilsMessengerEXT");

    if (func)
    {
        func(fInstance, fDebugMessenger, fAllocationCallbacks);
    }
#endif
    vkDestroyInstance(fInstance, fAllocationCallbacks);
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------