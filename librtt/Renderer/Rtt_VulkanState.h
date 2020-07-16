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
		void SetInstance( VkInstance instance ) { fInstance = instance; }
		VkDevice GetDevice() const { return fDevice; }
		void SetDevice( VkDevice device ) { fDevice = device; }
		VkPhysicalDevice GetPhysicalDevice() const { return fPhysicalDevice; }
		void SetPhysicalDevice( VkPhysicalDevice physicalDevice ) { fPhysicalDevice = physicalDevice; }
		VkQueue GetGraphicsQueue() const { return fGraphicsQueue; }
		void SetGraphicsQueue( VkQueue queue ) { fGraphicsQueue = queue; }
		VkQueue GetPresentQueue() const { return fPresentQueue; }
		void SetPresentQueue( VkQueue queue ) { fPresentQueue = queue; }
		void SetSurface( VkSurfaceKHR surface ) { fSurface = surface; }
		void SetSampleCount( unsigned int sampleCount ) { fSampleCount = sampleCount; }
		unsigned int GetSampleCount() const { return fSampleCount; }

	public:
		struct NewSurfaceCallback {
			VkSurfaceKHR (*make)( VkInstance, void *, const VkAllocationCallbacks * );
			const char * extension;
			void * data;
		};

		static bool PopulatePreSwapChainDetails( VulkanState & state, const NewSurfaceCallback & surfaceCallback );
		static bool GetMultisampleDetails( VulkanState & state );
		static bool GetSwapchainFormat( VulkanState & state );

	#ifndef NDEBUG
		void SetDebugMessenger( VkDebugUtilsMessengerEXT messenger ) { fDebugMessenger = messenger; }
	#endif

	private:
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
		unsigned int fSampleCount;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanState_H__
