//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_VulkanRenderer.h"

#include "Renderer/Rtt_VulkanState.h"
#include "Renderer/Rtt_VulkanCommandBuffer.h"
#include "Renderer/Rtt_VulkanFrameBufferObject.h"
#include "Renderer/Rtt_VulkanGeometry.h"
#include "Renderer/Rtt_VulkanProgram.h"
#include "Renderer/Rtt_VulkanTexture.h"
#include "Renderer/Rtt_CPUResource.h"
#include "Core/Rtt_Assert.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

VulkanRenderer::VulkanRenderer( Rtt_Allocator* allocator, VulkanState * state )
:   Super( allocator ),
	fState( state )
{
//	fFrontCommandBuffer = Rtt_NEW( allocator, VulkanCommandBuffer( allocator ) );
//	fBackCommandBuffer = Rtt_NEW( allocator, VulkanCommandBuffer( allocator ) );
	// N.B. this will probably be RADICALLY different in Vulkan
	// maybe these can just be hints to some unified thing?

	VkPhysicalDevice physicalDevice = state ? state->GetPhysicalDevice() : VK_NULL_HANDLE;

	if (physicalDevice != VK_NULL_HANDLE)
	{
		VkDeviceCreateInfo createInfo = {};

		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

		// TODO: extensions, layers, features

		VkDeviceQueueCreateInfo queueCreateInfo = {};

		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = 0;

		float queuePriorities[] = { 1.0f };

		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = queuePriorities;

		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;

		VkDevice device;
		VkResult result = vkCreateDevice( physicalDevice, &createInfo, NULL, &device );

		if (VK_SUCCESS == result)
		{
			state->SetDevice( device );
		}

		else
		{
			Rtt_LogException( "Failed creating logical device: %d\n", result );
		}
	}

	else
	{
		Rtt_LogException( "Vulkan physical device unavailable" );
	}
}

VulkanRenderer::~VulkanRenderer()
{
	Rtt_DELETE( fState );
}

GPUResource* 
VulkanRenderer::Create( const CPUResource* resource )
{
	switch( resource->GetType() )
	{
		case CPUResource::kFrameBufferObject: return new VulkanFrameBufferObject;
		case CPUResource::kGeometry: return new VulkanGeometry( fState );
		case CPUResource::kProgram: return new VulkanProgram;
		case CPUResource::kTexture: return new VulkanTexture( fState );
		case CPUResource::kUniform: return NULL;
		default: Rtt_ASSERT_NOT_REACHED(); return NULL; // iPhone irrelevant
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------