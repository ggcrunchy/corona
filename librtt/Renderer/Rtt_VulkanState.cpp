//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_VulkanState.h"
#include "Renderer/Rtt_VulkanProgram.h"
#include "Renderer/Rtt_VulkanTexture.h"
#include "Renderer/Rtt_VulkanExports.h"
#include "Core/Rtt_Assert.h"
#include "CoronaLog.h"
#include <shaderc/shaderc.h>
#include <pthread.h>
#include <limits>
#include <tuple>
#include <utility>

#include <cinttypes> // debugging

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

bool
VulkanExports::CreateVulkanState( const VulkanSurfaceParams & params, void ** state )
{
	VulkanState * vulkanState = Rtt_NEW( NULL, VulkanState );
	
	*state = vulkanState; // if we encounter an error we'll need to destroy this, so assign it early

	VkAllocationCallbacks * allocator = NULL; // TODO

	return VulkanState::PopulatePreSwapchainDetails( *vulkanState, params );
}

void
VulkanExports::PopulateMultisampleDetails( void * state )
{
	VulkanState::PopulateMultisampleDetails( *static_cast< VulkanState * >( state ) );
}

void
VulkanExports::DestroyVulkanState( void * state )
{
	VulkanState * vulkanState = static_cast< VulkanState * >( state );

	Rtt_DELETE( vulkanState );
}

// ----------------------------------------------------------------------------

void
RenderPassKey::SetContents( std::vector< U8 > & contents )
{
	fContents.swap( contents );
}

bool
RenderPassKey::operator == ( const RenderPassKey & other ) const
{
	return fContents == other.fContents;
}

bool
RenderPassKey::operator < ( const RenderPassKey & other ) const
{
	size_t minSize = (std::min)( fContents.size(), other.fContents.size() );
	int result = memcmp( fContents.data(), other.fContents.data(), minSize );

	return result < 0 || (0 == result && other.fContents.size() > minSize);
}

VulkanBufferData::VulkanBufferData( VkDevice device, const VkAllocationCallbacks * allocator )
:	fDevice( device ),
	fAllocator( allocator ),
	fBuffer( VK_NULL_HANDLE ),
	fMemory( VK_NULL_HANDLE )
{
}

VulkanBufferData::~VulkanBufferData()
{
	Clear();
}

VulkanBufferData *
VulkanBufferData::Extract( Rtt_Allocator * allocator )
{
	if (VK_NULL_HANDLE == fBuffer)
	{
		return NULL;
	}

	VulkanBufferData * bufferData = Rtt_NEW( allocator, VulkanBufferData( fDevice, fAllocator ) );
	
	bufferData->fBuffer = fBuffer;
	bufferData->fMemory = fMemory;

	Disown();

	return bufferData;
}

void
VulkanBufferData::Clear()
{
    vkDestroyBuffer( fDevice, fBuffer, fAllocator );
    vkFreeMemory( fDevice, fMemory, fAllocator );

	Disown();
}

void
VulkanBufferData::Disown()
{
	fBuffer = VK_NULL_HANDLE;
	fMemory = VK_NULL_HANDLE;
}

VulkanState::VulkanState()
:   fAllocator( NULL ),
    fInstance( VK_NULL_HANDLE ),
#ifndef NDEBUG
	fDebugMessenger( VK_NULL_HANDLE ),
#endif
    fDevice( VK_NULL_HANDLE ),
    fPhysicalDevice( VK_NULL_HANDLE ),
    fGraphicsQueue( VK_NULL_HANDLE ),
    fPresentQueue( VK_NULL_HANDLE ),
	fSingleTimeCommandsPool( VK_NULL_HANDLE ),
    fSurface( VK_NULL_HANDLE ),
	fPipelineCache( VK_NULL_HANDLE ),
	fSwapchain( VK_NULL_HANDLE ),
	fSampleCountFlags( VK_SAMPLE_COUNT_1_BIT ),
	fCompiler( NULL ),
	fCompileOptions( NULL )
{
}

VulkanState::~VulkanState()
{
// vkDeviceWaitIdle( fDevice );
	VulkanProgram::CleanUpCompiler( fCompiler, fCompileOptions );

	for (auto & renderPass : fRenderPasses)
	{
		vkDestroyRenderPass( fDevice, renderPass.second.fPass, fAllocator );
	}

	vkDestroyCommandPool( fDevice, fSingleTimeCommandsPool, fAllocator );
    vkDestroySurfaceKHR( fInstance, fSurface, fAllocator );
    vkDestroyDevice( fDevice, fAllocator );
#ifndef NDEBUG
    auto func = ( PFN_vkDestroyDebugUtilsMessengerEXT ) vkGetInstanceProcAddr( fInstance, "vkDestroyDebugUtilsMessengerEXT" );

    if (func)
    {
        func( fInstance, fDebugMessenger, fAllocator );
    }
#endif
    vkDestroyInstance( fInstance, fAllocator );

	// allocator?
}

const RenderPassData *
VulkanState::AddRenderPass( const RenderPassKey & key, VkRenderPass renderPass )
{
	RenderPassData data = { fRenderPasses.size(), renderPass };
	auto result = fRenderPasses.insert( std::make_pair( key, data ) );

	return result.second ? &result.first->second : NULL;
}

const RenderPassData *
VulkanState::FindRenderPassData( const RenderPassKey & key ) const
{
	auto iter = fRenderPasses.find( key );

	return fRenderPasses.end() != iter ? &iter->second : NULL;
}

bool
VulkanState::CreateBuffer( VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VulkanBufferData & bufferData )
{
    VkBufferCreateInfo createBufferInfo = {};

    createBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createBufferInfo.size = size;
    createBufferInfo.usage = usage;

	VkBuffer buffer = VK_NULL_HANDLE;

    if (VK_SUCCESS == vkCreateBuffer( fDevice, &createBufferInfo, fAllocator, &buffer ))
	{
		VkMemoryRequirements memRequirements;

		vkGetBufferMemoryRequirements( fDevice, buffer, &memRequirements );

		VkMemoryAllocateInfo allocInfo = {};

		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;

		if (FindMemoryType( memRequirements.memoryTypeBits, properties, allocInfo.memoryTypeIndex ))
		{
			VkDeviceMemory bufferMemory = VK_NULL_HANDLE;

			if (VK_SUCCESS == vkAllocateMemory( fDevice, &allocInfo, fAllocator, &bufferMemory ))
			{
				vkBindBufferMemory( fDevice, buffer, bufferMemory, 0U );

				bufferData.fBuffer = buffer;
				bufferData.fMemory = bufferMemory;

			#if 0
				CoronaLog( "Buffer Vulkan ID=%" PRIx64 ", Memory=%" PRIx64, buffer, bufferMemory );
			#endif

				return true;
			}

			else
			{
				CoronaLog( "Failed to allocate buffer memory!" );
			}
		}

		vkDestroyBuffer( fDevice, buffer, fAllocator );
	}

	else
	{
        CoronaLog( "Failed to create buffer!" );
    }

	return false;
}

bool
VulkanState::FindMemoryType( uint32_t typeFilter, VkMemoryPropertyFlags properties, uint32_t & type )
{
    VkPhysicalDeviceMemoryProperties memProperties;

    vkGetPhysicalDeviceMemoryProperties( fPhysicalDevice, &memProperties );

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
	{
        if ((typeFilter & (1U << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
            type = i;

			return true;
        }
    }

    CoronaLog( "Failed to find suitable memory type!" );

	return false;
}

VkCommandBuffer
VulkanState::BeginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo = {};

    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandBufferCount = 1U;
    allocInfo.commandPool = fSingleTimeCommandsPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    vkAllocateCommandBuffers( fDevice, &allocInfo, &commandBuffer );

    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer( commandBuffer, &beginInfo );

    return commandBuffer;
}

void
VulkanState::EndSingleTimeCommands( VkCommandBuffer commandBuffer )
{
    vkEndCommandBuffer( commandBuffer );

	VkFenceCreateInfo fenceCreateInfo = {};

	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	VkFence fence = VK_NULL_HANDLE;
	
	if (VK_SUCCESS == vkCreateFence( fDevice, &fenceCreateInfo, fAllocator, &fence ))
	{
		VkSubmitInfo submitInfo = {};

		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1U;
		submitInfo.pCommandBuffers = &commandBuffer;

		if (VK_SUCCESS == vkQueueSubmit( fGraphicsQueue, 1U, &submitInfo, fence ))
		{
			WaitOnFence( fence );
		}

		vkDestroyFence( fDevice, fence, fAllocator );
	}

    vkFreeCommandBuffers( fDevice, fSingleTimeCommandsPool, 1U, &commandBuffer );
}

void
VulkanState::CopyBuffer( VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size )
{
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
    VkBufferCopy copyRegion = {};

    copyRegion.size = size;

    vkCmdCopyBuffer( commandBuffer, srcBuffer, dstBuffer, 1U, &copyRegion );

    EndSingleTimeCommands( commandBuffer );
}

void *
VulkanState::MapData( VkDeviceMemory memory, VkDeviceSize count, VkDeviceSize offset )
{
	void * mapping = NULL;

	vkMapMemory( fDevice, memory, offset, count, 0, &mapping );

	return mapping;
}

void
VulkanState::StageData( VkDeviceMemory stagingMemory, const void * data, VkDeviceSize count, VkDeviceSize offset )
{
	void * mapping = MapData( stagingMemory, count, offset );

	memcpy( mapping, data, static_cast< size_t >( count ) );
    vkUnmapMemory( fDevice, stagingMemory );
}

void
VulkanState::WaitOnFence( VkFence fence )
{
	Rtt_ASSERT( VK_NULL_HANDLE != fence );

	vkWaitForFences( fDevice, 1U, &fence, VK_TRUE, (std::numeric_limits< uint64_t >::max)() );
}

void
VulkanState::PrepareCompiler()
{
	VulkanProgram::InitializeCompiler( &fCompiler, &fCompileOptions );
}

VkCommandPool
VulkanState::MakeCommandPool( uint32_t queueFamily, bool resetCommandBuffer )
{
    VkCommandPoolCreateInfo createPoolInfo = {};

    createPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createPoolInfo.queueFamilyIndex = queueFamily;

	if (resetCommandBuffer)
	{
		createPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	}

	VkCommandPool commandPool = VK_NULL_HANDLE;

    if (VK_SUCCESS == vkCreateCommandPool( GetDevice(), &createPoolInfo, GetAllocator(), &commandPool ))
	{
		return commandPool;
	}

	else
	{
        CoronaLog( "Failed to create command pool!" );

		return VK_NULL_HANDLE;
    }
}

bool
VulkanState::PopulateMultisampleDetails( VulkanState & state )
{
	const VkPhysicalDeviceLimits & limits = state.fDeviceDetails.properties.limits;
    VkSampleCountFlags counts = limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts;

    if (counts & VK_SAMPLE_COUNT_64_BIT)
	{
		state.fSampleCountFlags = VK_SAMPLE_COUNT_64_BIT;
	}

    else if (counts & VK_SAMPLE_COUNT_32_BIT)
	{
		state.fSampleCountFlags = VK_SAMPLE_COUNT_32_BIT;
	}

    else if (counts & VK_SAMPLE_COUNT_16_BIT)
	{
		state.fSampleCountFlags = VK_SAMPLE_COUNT_16_BIT;
	}

    else if (counts & VK_SAMPLE_COUNT_8_BIT)
	{
		state.fSampleCountFlags = VK_SAMPLE_COUNT_8_BIT;
	}

    else if (counts & VK_SAMPLE_COUNT_4_BIT)
	{
		state.fSampleCountFlags = VK_SAMPLE_COUNT_4_BIT;
	}

    else if (counts & VK_SAMPLE_COUNT_2_BIT)
	{
		state.fSampleCountFlags = VK_SAMPLE_COUNT_2_BIT;
	}

	else
	{
		state.fSampleCountFlags = VK_SAMPLE_COUNT_1_BIT;
	}

	return true;
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
CollectExtensions( std::vector<const char *> & extensions, std::vector<const char *> & optional, const std::vector<VkExtensionProperties> & extensionProps )
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
			extensions.push_back( props.extensionName );
		}
	}
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
) {
	if (strcmp( pCallbackData->pMessageIdName, "VUID-VkViewport-height-01772" ) != 0) // seems spurious with VK_KHR_MAINTENANCE1_EXTENSION_NAME enabled...
	{
		CoronaLog( "validation layer: %s", pCallbackData->pMessage );
	}

//	VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT: Some event has happened that is unrelated to the specification or performance
//	VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT: Something has happened that violates the specification or indicates a possible mistake
//	VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT: Potential non-optimal use of Vulkan

    return VK_FALSE;
}

static VkApplicationInfo
AppInfo()
{
	VkApplicationInfo appInfo = {};

	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.apiVersion = VK_API_VERSION_1_0;
	appInfo.applicationVersion = VK_MAKE_VERSION( 1, 0, 0 );
	appInfo.engineVersion = VK_MAKE_VERSION( 1, 0, 0 );
	appInfo.pApplicationName = "Solar App"; // TODO?
	appInfo.pEngineName = "Solar2D";

	return appInfo;
}

static VkInstance
MakeInstance( VkApplicationInfo * appInfo, const char * extension, const VkAllocationCallbacks * allocator, VkDebugUtilsMessengerEXT * debugMessenger )
{
	VkInstanceCreateInfo createInfo = {};

	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = appInfo;

	std::vector< const char * > extensions = { VK_KHR_SURFACE_EXTENSION_NAME, extension }, optional;
	unsigned int extensionCount = 0U;

	vkEnumerateInstanceExtensionProperties( NULL, &extensionCount, NULL );

	std::vector< VkExtensionProperties > extensionProps( extensionCount );

	vkEnumerateInstanceExtensionProperties( NULL, &extensionCount, extensionProps.data() );

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
	
	const std::vector< const char * > layersToTry = {
	//	"VK_LAYER_LUNARG_api_dump",
	//	"VK_LAYER_LUNARG_gfxreconstruct",
		"VK_LAYER_LUNARG_standard_validation",
		"VK_LAYER_KHRONOS_validation"
	};
    std::vector< VkLayerProperties > availableLayers( layerCount );

    vkEnumerateInstanceLayerProperties( &layerCount, availableLayers.data() );

	std::vector< const char * > validationLayers;

	for (const char * layerName : layersToTry)
	{
		if (FindString( availableLayers.begin(), availableLayers.end(), layerName, []( const VkLayerProperties & props )
		{
			return props.layerName;
		} ) )
		{
			validationLayers.push_back( layerName );
		}

		else
		{
			CoronaLog( "Unable to find layer %s", layerName );
		}
	}

	createInfo.enabledLayerCount = validationLayers.size();
	createInfo.ppEnabledLayerNames = validationLayers.data();

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};

	debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	debugCreateInfo.pfnUserCallback = DebugCallback;

	createInfo.pNext = &debugCreateInfo;
#endif
	VkInstance instance = VK_NULL_HANDLE;

	if (ok && vkCreateInstance( &createInfo, allocator, &instance ) != VK_SUCCESS)
	{
		CoronaLog( "Failed to create instance!\n" );

		ok = false;
	}
#ifndef NDEBUG
	if (ok && debugMessenger)
	{
		auto func = ( PFN_vkCreateDebugUtilsMessengerEXT ) vkGetInstanceProcAddr( instance, "vkCreateDebugUtilsMessengerEXT" );

		if (!func || func( instance, &debugCreateInfo, allocator, debugMessenger ) != VK_SUCCESS)
		{
			CoronaLog( "Failed to create debug messenger!\n" );

			vkDestroyInstance( instance, allocator );

			instance = VK_NULL_HANDLE;
		}
	}
#endif
	return instance;
}

struct Queues {
	Queues()
:	fGraphicsFamily( ~0U ),
	fPresentFamily( ~0U )
	{
	}

	bool isComplete() const { return fGraphicsFamily != ~0U && fPresentFamily != ~0U; }

	std::vector< uint32_t > GetFamilies() const
	{
		std::vector< uint32_t > families;

		if (isComplete())
		{
			families.push_back( fGraphicsFamily );

			if (fGraphicsFamily != fPresentFamily)
			{
				families.push_back( fPresentFamily );
			}
		}

		return families;
	}

	uint32_t fGraphicsFamily;
	uint32_t fPresentFamily;
};

static bool
IsSuitableDevice( VkPhysicalDevice device, VkSurfaceKHR surface, Queues & queues )
{
	uint32_t queueFamilyCount = 0U;

	vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, NULL );

	std::vector< VkQueueFamilyProperties > queueFamilies( queueFamilyCount );

	vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, queueFamilies.data() );

	for (uint32_t i = 0; i < queueFamilyCount && !queues.isComplete(); ++i)
	{
		if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			queues.fGraphicsFamily = i;
		}

		VkBool32 supported;

		vkGetPhysicalDeviceSurfaceSupportKHR( device, i, surface, &supported );

		if (supported)
		{
			queues.fPresentFamily = i;
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

	std::vector< const char * > extensions, required = { VK_KHR_MAINTENANCE1_EXTENSION_NAME, VK_KHR_SWAPCHAIN_EXTENSION_NAME };

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

static std::tuple< VkPhysicalDevice, Queues, VulkanState::DeviceDetails >
ChoosePhysicalDevice( VkInstance instance, VkSurfaceKHR surface )
{
	uint32_t deviceCount = 0U;

	vkEnumeratePhysicalDevices( instance, &deviceCount, NULL );

	if (0U == deviceCount)
	{
		CoronaLog( "Failed to find GPUs with Vulkan support!" );

		return std::make_tuple( VkPhysicalDevice( VK_NULL_HANDLE ), Queues(), VulkanState::DeviceDetails{} );
	}

	std::vector< VkPhysicalDevice > devices( deviceCount );

	vkEnumeratePhysicalDevices( instance, &deviceCount, devices.data() );
	
	VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
	VulkanState::DeviceDetails bestDetails;
	uint32_t bestScore = 0U;
	Queues bestQueues;

	for (const VkPhysicalDevice & device : devices)
	{
		Queues queues;

		if (!IsSuitableDevice( device, surface, queues ))
		{
			continue;
		}

		VulkanState::DeviceDetails deviceDetails = {};
		VkPhysicalDeviceFeatures features;
		
		vkGetPhysicalDeviceFeatures( device, &features );
	    vkGetPhysicalDeviceProperties( device, &deviceDetails.properties );
		
		uint32_t score = 0U;

		// Discrete GPUs have a significant performance advantage
		if (VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU == deviceDetails.properties.deviceType)
		{
			score += 1000U;
		}

		if (deviceDetails.properties.limits.maxPushConstantsSize > 128)
		{
			score += 250U;
		}

		if (features.shaderSampledImageArrayDynamicIndexing)
		{
			score += 500U;

			deviceDetails.features.shaderSampledImageArrayDynamicIndexing = true;
		}

		if (features.samplerAnisotropy)
		{
			score += 500U;

			deviceDetails.features.samplerAnisotropy = true;
		}

		// Maximum possible size of textures affects graphics quality
		score += deviceDetails.properties.limits.maxImageDimension2D;

		// other ideas: texture compression
		// could make the rating adjustable in config.lua?

		if (score > bestScore)
		{
			bestDevice = device;
			bestDetails = deviceDetails;
			bestScore = score;
			bestQueues = queues;
		}
	}

	return std::make_tuple( bestDevice, bestQueues, bestDetails );
}

static VkDevice
MakeLogicalDevice( VkPhysicalDevice physicalDevice, const std::vector< uint32_t > & families, const VulkanState::DeviceDetails & deviceDetails, const VkAllocationCallbacks * allocator )
{
	VkDeviceCreateInfo createDeviceInfo = {};

	createDeviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

	std::vector< VkDeviceQueueCreateInfo > queueCreateInfos;

	float queuePriorities[] = { 1.0f };

	for (uint32_t index : families)
	{
		VkDeviceQueueCreateInfo queueCreateInfo = {};

		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.pQueuePriorities = queuePriorities;
		queueCreateInfo.queueCount = 1U;
		queueCreateInfo.queueFamilyIndex = index;

		queueCreateInfos.push_back( queueCreateInfo );
	}

	createDeviceInfo.pQueueCreateInfos = queueCreateInfos.data();
	createDeviceInfo.queueCreateInfoCount = queueCreateInfos.size();

	VkPhysicalDeviceFeatures deviceFeatures = {};

	deviceFeatures.samplerAnisotropy = deviceDetails.features.samplerAnisotropy ? VK_TRUE : VK_FALSE;
	deviceFeatures.shaderSampledImageArrayDynamicIndexing = deviceDetails.features.shaderSampledImageArrayDynamicIndexing ? VK_TRUE : VK_FALSE;

	createDeviceInfo.pEnabledFeatures = &deviceFeatures;

	const char * deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	createDeviceInfo.enabledExtensionCount = 1U;
	createDeviceInfo.ppEnabledExtensionNames = deviceExtensions;

	VkDevice device;

	if (VK_SUCCESS == vkCreateDevice( physicalDevice, &createDeviceInfo, allocator, &device ))
	{
		return device;
	}

	else
	{
		CoronaLog( "Failed to create logical device!" );

		return VK_NULL_HANDLE;
	}
}

struct CompilerDetails {
	shaderc_compiler * compiler;
	shaderc_compile_options * options;
};

static void *
CompilerThread(void * arg)
{
	VulkanState * state = static_cast< VulkanState * >( arg );

	state->PrepareCompiler(); // this is relatively slow, at least in debug

	return NULL;
}

bool
VulkanState::PopulatePreSwapchainDetails( VulkanState & state, const VulkanSurfaceParams & params )
{
	pthread_t thread_id;

	pthread_create( &thread_id, NULL, &CompilerThread, &state );

	VkApplicationInfo appInfo = AppInfo();
	const VkAllocationCallbacks * allocator = state.GetAllocator();
	VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
	VkInstance instance = VK_NULL_HANDLE;

	if (VulkanState::VolkInitialize())
	{
		instance = MakeInstance( &appInfo,
		
#ifdef _WIN32
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
		, allocator, &messenger );
	}

	bool ok = false;

	if (instance != VK_NULL_HANDLE)
	{
		state.fInstance = instance;
	#ifndef NDEBUG
		state.fDebugMessenger = messenger;
	#endif

		VulkanState::VolkLoadInstance( instance );

	#ifdef _WIN32
		VkWin32SurfaceCreateInfoKHR createSurfaceInfo = {};

		createSurfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		createSurfaceInfo.hwnd = params.fWindowHandle;
		createSurfaceInfo.hinstance = params.fInstance;

		#define CREATE_VULKAN_SURFACE vkCreateWin32SurfaceKHR
	#else
		#error Non-Windows Vulkan state not implemented!
	#endif

		VkSurfaceKHR surface;

		if (VK_SUCCESS == CREATE_VULKAN_SURFACE( instance, &createSurfaceInfo, allocator, &surface ))
		{
			state.fSurface = surface;

			auto physicalDeviceData = ChoosePhysicalDevice( instance, surface );

			if (std::get< 0 >( physicalDeviceData ) != VK_NULL_HANDLE)
			{
				state.fPhysicalDevice = std::get< 0 >( physicalDeviceData );

				const Queues & queues = std::get< 1 >( physicalDeviceData );
				const std::vector< uint32_t > & families = queues.GetFamilies();
				VkDevice device = MakeLogicalDevice( state.fPhysicalDevice, families, std::get< 2 >( physicalDeviceData ), allocator );

				if (device != VK_NULL_HANDLE)
				{
					state.fDevice = device;
					state.fDeviceDetails = std::get< 2 >( physicalDeviceData );
					state.fQueueFamilies = families;

					vkGetDeviceQueue( device, queues.fGraphicsFamily, 0U, &state.fGraphicsQueue );
					vkGetDeviceQueue( device, queues.fPresentFamily, 0U, &state.fPresentQueue );

					state.fSingleTimeCommandsPool = state.MakeCommandPool( queues.fGraphicsFamily );

					if (VK_NULL_HANDLE != state.fSingleTimeCommandsPool)
					{
						VulkanState::VolkLoadDevice( device );

						ok = true;
					}
				}
			}
		}
	}

	pthread_join( thread_id, NULL );

	return ok;
}

bool
VulkanState::PopulateSwapchainDetails( VulkanState & state )
{
	VkPhysicalDevice device = state.GetPhysicalDevice();
	VkSurfaceKHR surface = state.GetSurface();
	uint32_t formatCount = 0U, presentModeCount = 0U;

	vkGetPhysicalDeviceSurfaceFormatsKHR( device, surface, &formatCount, NULL );
	vkGetPhysicalDeviceSurfacePresentModesKHR( device, surface, &presentModeCount, NULL );

	std::vector< VkSurfaceFormatKHR > formats( formatCount );
	std::vector< VkPresentModeKHR > presentModes( presentModeCount );

	vkGetPhysicalDeviceSurfaceFormatsKHR( device, surface, &formatCount, formats.data() );
	vkGetPhysicalDeviceSurfacePresentModesKHR( device, surface, &presentModeCount, presentModes.data() );

	VkSurfaceFormatKHR format = formats.front();

	for (const VkSurfaceFormatKHR & formatInfo : formats)
	{
		if (VK_FORMAT_B8G8R8A8_UNORM == formatInfo.format) // TODO: use a rating system, in case not present?
		{
            format = formatInfo;

			break;
        }
	}

	VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;

	for (const VkPresentModeKHR & presentMode : presentModes)
	{
		if (VK_PRESENT_MODE_MAILBOX_KHR == presentMode)
		{
			mode = presentMode;
		}
	}

	SwapchainDetails & details = state.fSwapchainDetails;

	details.fFormat = format;
	details.fPresentMode = mode;

	UpdateSwapchainDetails( state );

	return true;
}

void
VulkanState::UpdateSwapchainDetails( VulkanState & state )
{
	VkSurfaceCapabilitiesKHR capabilities;

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR( state.GetPhysicalDevice(), state.GetSurface(), &capabilities );

	uint32_t imageCount = capabilities.minImageCount + 1U;

	if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
	{
		imageCount = capabilities.maxImageCount;
	}

	SwapchainDetails & details = state.fSwapchainDetails;

	details.fImageCount = imageCount;
    details.fExtent = capabilities.currentExtent;
	details.fTransformFlagBits = capabilities.currentTransform;
}

bool
VulkanState::VolkInitialize()
{
#ifdef NDEBUG
	return VK_SUCCESS == volkInitialize();
#else
	return true;
#endif
}
		
void
VulkanState::VolkLoadInstance( VkInstance instance )
{
#ifdef NDEBUG
	volkLoadInstance( instance );
#endif
}

void
VulkanState::VolkLoadDevice( VkDevice device )
{
#ifdef NDEBUG
	volkLoadDevice( device );
#endif
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------