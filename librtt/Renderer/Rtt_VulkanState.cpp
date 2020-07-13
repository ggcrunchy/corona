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
    fDevice( VK_NULL_HANDLE ),
    fPhysicalDevice( VK_NULL_HANDLE )
{
}

VulkanState::~VulkanState()
{
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------