//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_VulkanState_H__
#define _Rtt_VulkanState_H__

#include "Renderer/Rtt_Renderer.h"
#include <vulkan/vulkan.h>
#include <utility>
#include <vector>

#ifdef free
#undef free
#endif

// ----------------------------------------------------------------------------

struct shaderc_compiler;
struct shaderc_compile_options;

namespace Rtt
{

// ----------------------------------------------------------------------------

class VulkanBufferData {
public:
	VulkanBufferData( VkDevice device, VkAllocationCallbacks * allocator );
	~VulkanBufferData();

	VkBuffer GetBuffer() const;
	VkDeviceMemory GetMemory() const;

	VulkanBufferData * Extract( Rtt_Allocator * allocator );

	void Clear();
	bool IsValid() const;

private:
	VkAllocationCallbacks * fAllocator;
	VkDevice fDevice;
	VkBuffer fBuffer;
	VkDeviceMemory fMemory;

	friend class VulkanState;
};

class VulkanState
{
	public:
		typedef VulkanState Self;

	public:
		VulkanState();
		~VulkanState();

	public:
		VkAllocationCallbacks * GetAllocator() const { return fAllocator; }
		VkInstance GetInstance() const { return fInstance; }
		VkDevice GetDevice() const { return fDevice; }
		VkPhysicalDevice GetPhysicalDevice() const { return fPhysicalDevice; }
		VkQueue GetGraphicsQueue() const { return fGraphicsQueue; }
		VkQueue GetPresentQueue() const { return fPresentQueue; }
		VkSurfaceKHR GetSurface() const { return fSurface; }
		VkPipelineCache GetPipelineCache() const { return fPipelineCache; }
		VkSampleCountFlags GetSampleCountFlags() const { return fSampleCountFlags; }

		struct shaderc_compiler * GetCompiler() const { return fCompiler; }
		struct shaderc_compile_options * GetCompileOptions() const { return fCompileOptions; }

	#ifndef NDEBUG
		void SetDebugMessenger( VkDebugUtilsMessengerEXT messenger ) { fDebugMessenger = messenger; }
	#endif

	public:
		VulkanBufferData CreateBuffer( VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties );
		bool FindMemoryType( uint32_t typeFilter, VkMemoryPropertyFlags properties, uint32_t & type );
	
	public:
	    VkCommandBuffer BeginSingleTimeCommands();
		void EndSingleTimeCommands( VkCommandBuffer commandBuffer );
		void CopyBuffer( VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size );
		void * MapData( VkDeviceMemory memory, VkDeviceSize count, VkDeviceSize offset = 0U );
		void StageData( VkDeviceMemory stagingMemory, const void * data, VkDeviceSize count, VkDeviceSize offset = 0U );

	public:
		struct NewSurfaceCallback {
			VkSurfaceKHR (*make)( VkInstance, void *, const VkAllocationCallbacks * );
			const char * extension;
			void * data;
		};

		static bool PopulatePreSwapchainDetails( VulkanState & state, const NewSurfaceCallback & surfaceCallback );
		static bool GetMultisampleDetails( VulkanState & state );
		static bool GetSwapchainDetails( VulkanState & state, uint32_t width, uint32_t height );

	private:
		VkAllocationCallbacks * fAllocator;
		VkInstance fInstance;
	#ifndef NDEBUG
		VkDebugUtilsMessengerEXT fDebugMessenger;
	#endif
		VkDevice fDevice;
		VkPhysicalDevice fPhysicalDevice;
		VkQueue fGraphicsQueue;
		VkQueue fPresentQueue;
		VkCommandPool fCurrentCommandPool;
		VkSurfaceKHR fSurface;
		VkPipelineCache fPipelineCache;
		VkSampleCountFlags fSampleCountFlags;
		VkSurfaceTransformFlagBitsKHR fTransformFlagBits;
		VkSwapchainKHR fSwapchain;
		VkExtent2D fSwapchainExtent;
		VkSurfaceFormatKHR fSwapchainFormat;
		uint32_t fMaxSwapImageCount;
		VkPresentModeKHR fPresentMode;
		std::vector< uint32_t > fQueueFamilies;

		struct shaderc_compiler * fCompiler;
		struct shaderc_compile_options * fCompileOptions;

		friend class VulkanRenderer;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanState_H__
