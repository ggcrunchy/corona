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
#include <map>
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

class RenderPassKey {
	public:
		void SetContents( std::vector< U8 > & contents );

		bool operator == ( const RenderPassKey & other ) const;
		bool operator < ( const RenderPassKey & other ) const;

	private:
		std::vector< U8 > fContents;
};

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
		VkCommandPool GetCommandPool() const { return fCommandPool; }
		VkSurfaceKHR GetSurface() const { return fSurface; }
		VkPipelineCache GetPipelineCache() const { return fPipelineCache; }
		VkSampleCountFlags GetSampleCountFlags() const { return fSampleCountFlags; }
		VkSwapchainKHR GetSwapchain() const { return fSwapchain; }
		const std::vector< uint32_t > & GetQueueFamilies() const { return fQueueFamilies; }

		void SetSwapchain( VkSwapchainKHR swapchain ) { fSwapchain = swapchain; }

		shaderc_compiler * GetCompiler() const { return fCompiler; }
		shaderc_compile_options * GetCompileOptions() const { return fCompileOptions; }

	#ifndef NDEBUG
		void SetDebugMessenger( VkDebugUtilsMessengerEXT messenger ) { fDebugMessenger = messenger; }
	#endif

	public:
		struct RenderPassData {
			U32 fID;
			VkRenderPass fPass;
		};

		bool FindRenderPassData( const RenderPassKey & key ) const;
		bool AddRenderPass( const RenderPassKey & key, VkRenderPass renderPass );

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

		static bool PopulateMultisampleDetails( VulkanState & state );
		static bool PopulatePreSwapchainDetails( VulkanState & state, const NewSurfaceCallback & surfaceCallback );
		static bool PopulateSwapchainDetails( VulkanState & state, uint32_t width, uint32_t height );

		struct SwapchainDetails {
			VkSurfaceTransformFlagBitsKHR fTransformFlagBits;
			VkExtent2D fExtent;
			VkSurfaceFormatKHR fFormat;
			uint32_t fImageCount;
			VkPresentModeKHR fPresentMode;
		};

		const SwapchainDetails & GetSwapchainDetails() const { return fSwapchainDetails; }

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
		VkCommandPool fCommandPool;
		VkSurfaceKHR fSurface;
		VkPipelineCache fPipelineCache;
		VkSampleCountFlags fSampleCountFlags;
		VkSwapchainKHR fSwapchain;
		SwapchainDetails fSwapchainDetails;
		std::vector< uint32_t > fQueueFamilies;
		std::map< RenderPassKey, RenderPassData > fRenderPasses;
		shaderc_compiler * fCompiler;
		shaderc_compile_options * fCompileOptions;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanState_H__
