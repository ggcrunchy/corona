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
#include "CoronaLog.h"
#include <algorithm>
#include <utility>
#include <vector>

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
    fPhysicalDevice( VK_NULL_HANDLE ),
    fGraphicsQueue( VK_NULL_HANDLE ),
    fPresentQueue( VK_NULL_HANDLE ),
    fSurface( VK_NULL_HANDLE )
{
}

VulkanState::~VulkanState()
{
    vkDestroySurfaceKHR( fInstance, fSurface, fAllocationCallbacks );
    vkDestroyDevice( fDevice, fAllocationCallbacks );
#ifndef NDEBUG
    auto func = ( PFN_vkDestroyDebugUtilsMessengerEXT ) vkGetInstanceProcAddr( fInstance, "vkDestroyDebugUtilsMessengerEXT" );

    if (func)
    {
        func( fInstance, fDebugMessenger, fAllocationCallbacks );
    }
#endif
    vkDestroyInstance( fInstance, fAllocationCallbacks );
}

const char *
StringIdentity( const char * str )
{
	return str;
}

template<typename I, typename F> bool
FindString( I & i1, I & i2, const char * str, F && getString )
{
	return std::find_if( i1, i2, [str, getString]( const I::value_type & other ) { return strcmp( str, getString( other ) ) == 0; } ) != i2;
}

static void
CollectExtensions(std::vector<const char *> & extensions, std::vector<const char *> & optional, const std::vector<VkExtensionProperties> & extensionProps)
{
	auto optionalEnd = std::remove_if( optional.begin(), optional.end(), [&extensions]( const char * name )
	{
		return FindString( extensions.begin(), extensions.end(), name, StringIdentity );
	} );
		
	for (auto & props : extensionProps)
	{
		const char * name = props.extensionName;

		if (FindString( optional.begin(), optionalEnd, props.extensionName, StringIdentity ))
		{
			extensions.push_back(props.extensionName);
		}
	}
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
) {
    CoronaLog( "validation layer: %s", pCallbackData->pMessage );

/*
	VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT: Some event has happened that is unrelated to the specification or performance
	VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT: Something has happened that violates the specification or indicates a possible mistake
	VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT: Potential non-optimal use of Vulkan
*/

    return VK_FALSE;
}

static VkApplicationInfo
AppInfo()
{
	VkApplicationInfo appInfo = {};

	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Solar App"; // TODO?
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "Solar2D";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	return appInfo;
}

#ifndef NDEBUG
static std::pair< VkInstance, VkDebugUtilsMessengerEXT >
#else
static VkInstance
#endif
MakeInstance( VkApplicationInfo * appInfo, const char * extension, const VkAllocationCallbacks * allocator )
{
	VkInstanceCreateInfo createInfo = {};

	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = appInfo;

	std::vector< const char * > extensions = { VK_KHR_SURFACE_EXTENSION_NAME, extension }, optional;
	unsigned int extensionCount = 0U;

	vkEnumerateInstanceExtensionProperties( NULL, &extensionCount, NULL );

	std::vector< VkExtensionProperties > extensionProps(extensionCount);

	vkEnumerateInstanceExtensionProperties( NULL, &extensionCount, extensionProps.data() );

	VkInstance instance = VK_NULL_HANDLE;

#ifndef NDEBUG
	extensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
#endif

	CollectExtensions( extensions, optional, extensionProps );

	createInfo.enabledExtensionCount = extensions.size();
	createInfo.ppEnabledExtensionNames = extensions.data();

	bool ok = true;

#ifndef NDEBUG
    uint32_t layerCount;

    vkEnumerateInstanceLayerProperties( &layerCount, NULL );
	
	const std::vector< const char* > validationLayers = { "VK_LAYER_KHRONOS_validation" };
    std::vector< VkLayerProperties > availableLayers( layerCount );

    vkEnumerateInstanceLayerProperties( &layerCount, availableLayers.data() );

	for (const char * layerName : validationLayers)
	{
		if (!FindString( availableLayers.begin(), availableLayers.end(), layerName, []( const VkLayerProperties & props )
		{
			return props.layerName;
		} ) )
		{
			CoronaLog( "Unable to find layer %s", layerName );

			ok = false;

			break;
		}
	}

	createInfo.enabledLayerCount = validationLayers.size();
	createInfo.ppEnabledLayerNames = validationLayers.data();

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};

	debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	debugCreateInfo.pfnUserCallback = DebugCallback;

	createInfo.pNext = ( VkDebugUtilsMessengerCreateInfoEXT * ) &debugCreateInfo;
#endif

	if (ok && vkCreateInstance( &createInfo, allocator, &instance ) != VK_SUCCESS)
	{
		CoronaLog( "Failed to create instance!\n" );

		ok = false;
	}

#ifndef NDEBUG
	VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;

	if (ok)
	{
		auto func = ( PFN_vkCreateDebugUtilsMessengerEXT ) vkGetInstanceProcAddr( instance, "vkCreateDebugUtilsMessengerEXT" );

		if (!func || VK_SUCCESS == func( instance, &debugCreateInfo, allocator, &messenger ) != VK_SUCCESS)
		{
			CoronaLog( "Failed to create debug messenger!\n" );

			vkDestroyInstance( instance, allocator );

			instance = VK_NULL_HANDLE;
		}
	}

	return std::make_pair( instance, messenger );
#else
	return instance;
#endif
}

struct Queues {
	Queues()
:	fGraphicsQueue( ~0U ),
	fPresentQueue( ~0U )
	{
	}

	uint32_t fGraphicsQueue;
	uint32_t fPresentQueue;

	bool isComplete() const { return fGraphicsQueue != ~0U && fPresentQueue != ~0U; }

	std::vector< uint32_t > GetFamilies() const
	{
		std::vector< uint32_t > families;

		if (isComplete())
		{
			families.push_back( fGraphicsQueue );

			if (fGraphicsQueue != fPresentQueue)
			{
				families.push_back( fPresentQueue );
			}
		}

		return families;
	}
};

static bool
IsSuitableDevice( VkPhysicalDevice device, VkSurfaceKHR surface, Queues & queues )
{
	uint32_t queueFamilyCount = 0U;

	vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, NULL );

	std::vector< VkQueueFamilyProperties > queueFamilies( queueFamilyCount );

	vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, queueFamilies.data() );

	for (uint32_t i = 0; i < queueFamilyCount; ++i)
	{
		if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			queues.fGraphicsQueue = i;
		}

		VkBool32 supported = VK_FALSE;

		vkGetPhysicalDeviceSurfaceSupportKHR( device, i, surface, &supported );

		if (supported)
		{
			queues.fPresentQueue = i;
		}
	}

	if (!queues.isComplete())
	{
		return false;
	}

	uint32_t extensionCount = 0U;

    vkEnumerateDeviceExtensionProperties( device, NULL, &extensionCount, NULL );

    std::vector< VkExtensionProperties > availableExtensions( extensionCount );

    vkEnumerateDeviceExtensionProperties( device, NULL, &extensionCount, availableExtensions.data() );

	std::vector< const char * > extensions, required = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	CollectExtensions( extensions, required, availableExtensions );

	if (extensions.size() != required.size())
	{
		return false;
	}

	uint32_t formatCount = 0U, presentModeCount = 0U;

	vkGetPhysicalDeviceSurfaceFormatsKHR( device, surface, &formatCount, NULL );
	vkGetPhysicalDeviceSurfacePresentModesKHR( device, surface, &presentModeCount, NULL );

	return formatCount != 0U && presentModeCount != 0U;
}

static std::pair< VkPhysicalDevice, Queues >
ChoosePhysicalDevice( VkInstance instance, VkSurfaceKHR surface )
{
	uint32_t deviceCount = 0U;

	vkEnumeratePhysicalDevices( instance, &deviceCount, NULL );

	if (0U == deviceCount)
	{
		CoronaLog( "Failed to find GPUs with Vulkan support!" );

		return std::make_pair( VkPhysicalDevice( VK_NULL_HANDLE ), Queues() );
	}

	std::vector< VkPhysicalDevice > devices( deviceCount );

	vkEnumeratePhysicalDevices( instance, &deviceCount, devices.data() );
	
	VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
	uint32_t bestScore = 0U;
	Queues bestQueues;

	for (const VkPhysicalDevice & device : devices)
	{
		Queues queues;

		if (!IsSuitableDevice( device, surface, queues ))
		{
			continue;
		}

		VkPhysicalDeviceProperties deviceProperties;
		VkPhysicalDeviceFeatures deviceFeatures;

	    vkGetPhysicalDeviceProperties( device, &deviceProperties );
		vkGetPhysicalDeviceFeatures( device, &deviceFeatures );
		
		uint32_t score = 0U;

		// Discrete GPUs have a significant performance advantage
		if (VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU == deviceProperties.deviceType)
		{
			score += 1000U;
		}

		// Maximum possible size of textures affects graphics quality
		score += deviceProperties.limits.maxImageDimension2D;

		// other ideas: ETC2 etc. texture compression

		if (score > bestScore)
		{
			bestDevice = device;
			bestScore = score;
			bestQueues = queues;
		}
	}

	return std::make_pair( bestDevice, bestQueues );
}

static VkDevice
MakeLogicalDevice( VkPhysicalDevice physicalDevice, const std::vector<uint32_t> & families, const VkAllocationCallbacks * allocator )
{
	VkDeviceCreateInfo createDeviceInfo = {};

	createDeviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

	std::vector< VkDeviceQueueCreateInfo > queueCreateInfos;

	for (uint32_t index : families)
	{
		VkDeviceQueueCreateInfo queueCreateInfo = {};

		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = index;
		queueCreateInfo.queueCount = 1;

		queueCreateInfos.push_back( queueCreateInfo );
	}

	createDeviceInfo.pQueueCreateInfos = queueCreateInfos.data();
	createDeviceInfo.queueCreateInfoCount = queueCreateInfos.size();

	VkPhysicalDeviceFeatures deviceFeatures = {};

	createDeviceInfo.pEnabledFeatures = &deviceFeatures;

	VkDevice device;

	if (vkCreateDevice( physicalDevice, &createDeviceInfo, allocator, &device ) != VK_SUCCESS)
	{
		CoronaLog( "Failed to create logical device!" );

		return VK_NULL_HANDLE;
	}

	return device;
}

bool
VulkanState::PopulatePreSwapChainDetails( VulkanState & state, const NewSurfaceCallback & surfaceCallback )
{
	const VkAllocationCallbacks * allocator = state.GetAllocationCallbacks();

	VkInstanceCreateInfo createInfo = {};
	VkApplicationInfo appInfo = AppInfo();
	auto instanceData = MakeInstance( &appInfo, surfaceCallback.extension, allocator );

#ifndef NDEBUG
	state.SetDebugMessenger( instanceData.second );

	VkInstance instance = instanceData.first;
#else
	VkInstance instance = instanceData;
#endif

	if (instance != VK_NULL_HANDLE)
	{
		state.SetInstance( instance );

		VkSurfaceKHR surface = surfaceCallback.make( instance, surfaceCallback.data, allocator );

		if (surface != VK_NULL_HANDLE)
		{
			state.SetSurface( surface );

			auto physicalDeviceData = ChoosePhysicalDevice( instance, surface );

			if (physicalDeviceData.first != VK_NULL_HANDLE)
			{
				state.SetPhysicalDevice( physicalDeviceData.first );

				VkDevice device = MakeLogicalDevice( physicalDeviceData.first, physicalDeviceData.second.GetFamilies(), allocator );

				if (device != VK_NULL_HANDLE)
				{
					VkQueue graphicsQueue, presentQueue;

					vkGetDeviceQueue( device, physicalDeviceData.second.fGraphicsQueue, 0, &graphicsQueue );
					vkGetDeviceQueue( device, physicalDeviceData.second.fPresentQueue, 0, &presentQueue );

					state.SetGraphicsQueue( graphicsQueue );
					state.SetPresentQueue( presentQueue );

					return true;
				}
			}
		}
	}

	return false;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------