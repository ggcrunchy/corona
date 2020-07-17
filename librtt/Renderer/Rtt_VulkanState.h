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
#include <vector>

#ifdef free
#undef free
#endif

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class VulkanState
{
	public:
		typedef VulkanState Self;

	public:
		VulkanState();
		~VulkanState();

	public:
		VkAllocationCallbacks * GetAllocationCallbacks() const { return fAllocationCallbacks; }
		VkInstance GetInstance() const { return fInstance; }
		VkDevice GetDevice() const { return fDevice; }
		VkPhysicalDevice GetPhysicalDevice() const { return fPhysicalDevice; }
		VkQueue GetGraphicsQueue() const { return fGraphicsQueue; }
		VkQueue GetPresentQueue() const { return fPresentQueue; }
		VkSurfaceKHR GetSurface() const { return fSurface; }
		VkSampleCountFlags GetSampleCountFlags() const { return fSampleCountFlags; }

	#ifndef NDEBUG
		void SetDebugMessenger( VkDebugUtilsMessengerEXT messenger ) { fDebugMessenger = messenger; }
	#endif

	public:
		void BuildUpSwapchain();
		void TearDownSwapchain();

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
		struct SwapchainImage {
			VkImage image;
			VkImageView view;
		};

		VkAllocationCallbacks * fAllocationCallbacks;
		VkInstance fInstance;
	#ifndef NDEBUG
		VkDebugUtilsMessengerEXT fDebugMessenger;
	#endif
		VkDevice fDevice;
		VkPhysicalDevice fPhysicalDevice;
		VkQueue fGraphicsQueue;
		VkQueue fPresentQueue;
		VkSurfaceKHR fSurface;
		VkSampleCountFlags fSampleCountFlags;
		VkSurfaceTransformFlagBitsKHR fTransformFlagBits;
		VkSwapchainKHR fSwapchain;
		VkExtent2D fSwapchainExtent;
		VkSurfaceFormatKHR fSwapchainFormat;
		uint32_t fMaxSwapImageCount;
		VkPresentModeKHR fPresentMode;
		std::vector< SwapchainImage > fSwapchainImages;
		std::vector< uint32_t > fQueueFamilies;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanState_H__
