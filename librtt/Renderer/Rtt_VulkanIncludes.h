//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_VulkanIncludes_H__
#define _Rtt_VulkanIncludes_H__

#if defined( Rtt_ANDROID_ENV )
    #define VK_NO_PROTOTYPES
    #include <vulkan/vulkan.h>
    #include <vulkan/vulkan_android.h>
    #include "../../external/vulkan/utils/volk.h"
#else
    #ifdef Rtt_DEBUG
        #include <vulkan/vulkan.h>
    #else
        #include "../../vulkan/utils/volk.h"
    #endif
#endif

#endif // _Rtt_VulkanIncludes_H__