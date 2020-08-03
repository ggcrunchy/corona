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
#include "Core/Rtt_Assert.h"
#include "CoronaLog.h"
#include <shaderc/shaderc.h>
#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

// ----------------------------------------------------------------------------

namespace Rtt
{

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
	size_t minSize = std::min( fContents.size(), other.fContents.size() );
	int result = memcmp( fContents.data(), other.fContents.data(), minSize );

	return result < 0 || (0 == result && other.fContents.size() > minSize);
}

VulkanBufferData::VulkanBufferData( VkDevice device, VkAllocationCallbacks * allocator )
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

VkBuffer
VulkanBufferData::GetBuffer() const
{
	return fBuffer;
}

VkDeviceMemory
VulkanBufferData::GetMemory() const
{
	return fMemory;
}

VulkanBufferData *
VulkanBufferData::Extract( Rtt_Allocator * allocator )
{
	VulkanBufferData * bufferData = Rtt_NEW( allocator, VulkanBufferData( fDevice, fAllocator ) );
	
	bufferData->fBuffer = fBuffer;
	bufferData->fMemory = fMemory;

	fBuffer = VK_NULL_HANDLE;
	fMemory = VK_NULL_HANDLE;

	return bufferData;
}

void
VulkanBufferData::Clear()
{
    vkDestroyBuffer( fDevice, fBuffer, fAllocator );
    vkFreeMemory( fDevice, fMemory, fAllocator );

	fBuffer = VK_NULL_HANDLE;
	fMemory = VK_NULL_HANDLE;
}

bool
VulkanBufferData::IsValid() const
{
	return fBuffer != VK_NULL_HANDLE && fMemory != VK_NULL_HANDLE;
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
	fCommandPool( VK_NULL_HANDLE ),
    fSurface( VK_NULL_HANDLE ),
	fPipelineCache( VK_NULL_HANDLE ),
	fSwapchain( VK_NULL_HANDLE ),
	fSampleCountFlags( VK_SAMPLE_COUNT_1_BIT ),
	fCompiler( NULL ),
	fCompileOptions( NULL )
{
}

/*
    void initVulkan() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPool();
        createColorResources();
        createDepthResources();
        createFramebuffers();
        createTextureImage();
        createTextureImageView();
        createTextureSampler();
        loadModel();
        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
    }
*/

VulkanState::~VulkanState()
{
	VulkanProgram::CleanUpCompiler( fCompiler, fCompileOptions );

	vkDestroyCommandPool( fDevice, fCommandPool, fAllocator );
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

bool
VulkanState::FindRenderPassData( const RenderPassKey & key ) const
{
	return fRenderPasses.end() != fRenderPasses.find( key );
}

bool
VulkanState::AddRenderPass( const RenderPassKey & key, VkRenderPass renderPass )
{
	RenderPassData data = { fRenderPasses.size(), renderPass };
	auto result = fRenderPasses.insert( std::make_pair( key, data ) );

	return result.second;
}

VulkanBufferData
VulkanState::CreateBuffer( VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties )
{
    VkBufferCreateInfo createBufferInfo = {};

    createBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createBufferInfo.size = size;
    createBufferInfo.usage = usage;

	VulkanBufferData bufferData( fDevice, fAllocator );

	VkBuffer buffer;

    if (VK_SUCCESS == vkCreateBuffer( fDevice, &createBufferInfo, fAllocator, &buffer ))
	{
		VkMemoryRequirements memRequirements;

		vkGetBufferMemoryRequirements( fDevice, buffer, &memRequirements );

		VkMemoryAllocateInfo allocInfo = {};

		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;

		if (FindMemoryType( memRequirements.memoryTypeBits, properties, allocInfo.memoryTypeIndex ))
		{
			VkDeviceMemory bufferMemory;

			if (VK_SUCCESS == vkAllocateMemory( fDevice, &allocInfo, fAllocator, &bufferMemory ))
			{
				vkBindBufferMemory( fDevice, buffer, bufferMemory, 0U );

				bufferData.fBuffer = buffer;
				bufferData.fMemory = bufferMemory;
			}

			else
			{
				CoronaLog( "Failed to allocate buffer memory!" );
			}
		}

		if (!bufferData.IsValid())
		{
			vkDestroyBuffer( fDevice, buffer, fAllocator );
		}
	}

	else
	{
        CoronaLog( "Failed to create buffer!" );
    }

	return bufferData;
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
    allocInfo.commandPool = fCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VkCommandBuffer commandBuffer;

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

	VkFence fence;
	
	if (VK_SUCCESS == vkCreateFence( fDevice, &fenceCreateInfo, fAllocator, &fence ))
	{
		VkSubmitInfo submitInfo = {};

		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1U;
		submitInfo.pCommandBuffers = &commandBuffer;

		if (VK_SUCCESS == vkQueueSubmit( fGraphicsQueue, 1U, &submitInfo, fence ))
		{
			vkWaitForFences( fDevice, 1U, &fence, VK_TRUE, std::numeric_limits< uint64_t >::max() );
		}

		vkDestroyFence( fDevice, fence, fAllocator );
	}

    vkFreeCommandBuffers( fDevice, fCommandPool, 1U, &commandBuffer );
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
	void * mapping;

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
/*


    void cleanupSwapChain() {
        vkDestroyImageView(device, depthImageView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthImageMemory, nullptr);

        vkDestroyImageView(device, colorImageView, nullptr);
        vkDestroyImage(device, colorImage, nullptr);
        vkFreeMemory(device, colorImageMemory, nullptr);

        for (auto framebuffer : swapChainFramebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }

        vkFreeCommandBuffers(device, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());

        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);

        for (auto imageView : swapChainImageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }

        vkDestroySwapchainKHR(device, swapChain, nullptr);

        for (size_t i = 0; i < swapChainImages.size(); i++) {
            vkDestroyBuffer(device, uniformBuffers[i], nullptr);
            vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
        }

        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }

    void cleanup() {
        cleanupSwapChain();

        vkDestroySampler(device, textureSampler, nullptr);
        vkDestroyImageView(device, textureImageView, nullptr);

        vkDestroyImage(device, textureImage, nullptr);
        vkFreeMemory(device, textureImageMemory, nullptr);

        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        vkDestroyBuffer(device, indexBuffer, nullptr);
        vkFreeMemory(device, indexBufferMemory, nullptr);

        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vkFreeMemory(device, vertexBufferMemory, nullptr);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(device, inFlightFences[i], nullptr);
        }

        vkDestroyCommandPool(device, commandPool, nullptr);

        vkDestroyDevice(device, nullptr);

        if (enableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }

        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);

        glfwDestroyWindow(window);

        glfwTerminate();
    }

    void recreateSwapChain() {
        int width = 0, height = 0;
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(window, &width, &height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(device);

        cleanupSwapChain();

        createSwapChain();
        createImageViews();
        createRenderPass();
        createGraphicsPipeline();
        createColorResources();
        createDepthResources();
        createFramebuffers();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
    }
*/

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
:	fGraphicsFamily( ~0U ),
	fPresentFamily( ~0U )
	{
	}

	uint32_t fGraphicsFamily;
	uint32_t fPresentFamily;

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
};

static bool
IsSuitableDevice( VkPhysicalDevice device, VkSurfaceKHR surface, Queues & queues )
{
	uint32_t queueFamilyCount;

	vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, NULL );

	std::vector< VkQueueFamilyProperties > queueFamilies( queueFamilyCount );

	vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, queueFamilies.data() );

	for (uint32_t i = 0; i < queueFamilyCount; ++i)
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

		if (features.shaderSampledImageArrayDynamicIndexing)
		{
			score += 500U;

			deviceDetails.features.shaderSampledImageArrayDynamicIndexing = true;
		}

		// Maximum possible size of textures affects graphics quality
		score += deviceDetails.properties.limits.maxImageDimension2D;

		// other ideas: ETC2 etc. texture compression
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
MakeLogicalDevice( VkPhysicalDevice physicalDevice, const std::vector<uint32_t> & families, const VkAllocationCallbacks * allocator )
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
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.queueFamilyIndex = index;

		queueCreateInfos.push_back( queueCreateInfo );
	}

	createDeviceInfo.pQueueCreateInfos = queueCreateInfos.data();
	createDeviceInfo.queueCreateInfoCount = queueCreateInfos.size();

	VkPhysicalDeviceFeatures deviceFeatures = {};

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

static VkCommandPool
MakeCommandPool( VkDevice device, uint32_t graphicsFamily, const VkAllocationCallbacks * allocator )
{
    VkCommandPoolCreateInfo createPoolInfo = {};

    createPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createPoolInfo.queueFamilyIndex = graphicsFamily;

	VkCommandPool commandPool;

    if (VK_SUCCESS == vkCreateCommandPool( device, &createPoolInfo, allocator, &commandPool ))
	{
		return commandPool;
	}

	else
	{
        CoronaLog( "Failed to create graphics command pool!" );

		return VK_NULL_HANDLE;
    }
}

bool
VulkanState::PopulatePreSwapchainDetails( VulkanState & state, const NewSurfaceCallback & surfaceCallback )
{
	VkApplicationInfo appInfo = AppInfo();
	const VkAllocationCallbacks * allocator = state.GetAllocator();
	auto instanceData = MakeInstance( &appInfo, surfaceCallback.extension, allocator );

#ifndef NDEBUG
	state.SetDebugMessenger( instanceData.second );

	VkInstance instance = instanceData.first;
#else
	VkInstance instance = instanceData;
#endif

	if (instance != VK_NULL_HANDLE)
	{
		state.fInstance = instance;

		VkSurfaceKHR surface = surfaceCallback.make( instance, surfaceCallback.data, allocator );

		if (surface != VK_NULL_HANDLE)
		{
			state.fSurface = surface;

			auto physicalDeviceData = ChoosePhysicalDevice( instance, surface );

			if (std::get< 0 >( physicalDeviceData ) != VK_NULL_HANDLE)
			{
				state.fPhysicalDevice = std::get< 0 >( physicalDeviceData );

				// TODO: enable any features and remember this

				const Queues & queues = std::get< 1 >( physicalDeviceData );
				auto families = queues.GetFamilies();
				VkDevice device = MakeLogicalDevice( state.fPhysicalDevice, families, allocator );

				if (device != VK_NULL_HANDLE)
				{
					state.fDevice = device;
					state.fDeviceDetails = std::get< 2 >( physicalDeviceData );
					state.fQueueFamilies = families;

					vkGetDeviceQueue( device, queues.fGraphicsFamily, 0U, &state.fGraphicsQueue );
					vkGetDeviceQueue( device, queues.fPresentFamily, 0U, &state.fPresentQueue );

					VkCommandPool commandPool = MakeCommandPool( device, queues.fGraphicsFamily, allocator );

					if (commandPool != VK_NULL_HANDLE)
					{
						state.fCommandPool = commandPool;

						VulkanProgram::InitializeCompiler( &state.fCompiler, &state.fCompileOptions );

						return true;
					}
				}
			}
		}
	}

	return false;
}

bool
VulkanState::PopulateSwapchainDetails( VulkanState & state, uint32_t width, uint32_t height )
{
	VkPhysicalDevice device = state.GetPhysicalDevice();
	VkSurfaceKHR surface = state.GetSurface();
	uint32_t formatCount, presentModeCount;

	vkGetPhysicalDeviceSurfaceFormatsKHR( device, surface, &formatCount, NULL );
	vkGetPhysicalDeviceSurfacePresentModesKHR( device, surface, &presentModeCount, NULL );

	std::vector< VkSurfaceFormatKHR > formats( formatCount );
	std::vector< VkPresentModeKHR > presentModes( presentModeCount );

	vkGetPhysicalDeviceSurfaceFormatsKHR( device, surface, &formatCount, formats.data() );
	vkGetPhysicalDeviceSurfacePresentModesKHR( device, surface, &presentModeCount, presentModes.data() );

	VkSurfaceFormatKHR format = formats.front();

	for (const VkSurfaceFormatKHR & formatInfo : formats)
	{
		if (VK_FORMAT_B8G8R8A8_SRGB == formatInfo.format && VK_COLOR_SPACE_SRGB_NONLINEAR_KHR == formatInfo.colorSpace) // TODO: use a rating system, in case not present?
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

	VkSurfaceCapabilitiesKHR capabilities;

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities);

	uint32_t imageCount = capabilities.minImageCount + 1U;

	if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
	{
		imageCount = capabilities.maxImageCount;
	}

	SwapchainDetails & details = state.fSwapchainDetails;

	details.fImageCount = imageCount;
    details.fExtent.width = std::max( capabilities.minImageExtent.width, std::min( capabilities.maxImageExtent.width, width ) );
	details.fExtent.height = std::max( capabilities.minImageExtent.height, std::min( capabilities.maxImageExtent.height, height ) );
	details.fFormat = format;
	details.fPresentMode = mode;
	details.fTransformFlagBits = capabilities.currentTransform;

	return true;
}

/*
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            drawFrame();
        }

        vkDeviceWaitIdle(device);
    }
*/

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------