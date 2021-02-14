//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_VulkanGeometry_H__
#define _Rtt_VulkanGeometry_H__

#include "Renderer/Rtt_GPUResource.h"

#include <vector>
#include <vulkan/vulkan.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

class Geometry;
class VulkanBufferData;
class VulkanRenderer;
class VulkanState;

// ----------------------------------------------------------------------------

class VulkanGeometry : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef VulkanGeometry Self;

	public:
		VulkanGeometry( VulkanState * state );
		virtual ~VulkanGeometry();

		struct Binding {
			Binding()
			:	fVertexBuffer( VK_NULL_HANDLE ),
				fIndexBuffer( VK_NULL_HANDLE )
			{
			}

			std::vector< VkVertexInputBindingDescription > fDescriptions;
			VkIndexType fIndexType;
			VkBuffer fVertexBuffer;
			VkBuffer fIndexBuffer;
			U32 fInputBindingID;
		};

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();

		void Bind( VulkanRenderer & renderer, VkCommandBuffer commandBuffer, bool populate );

	private:
		VulkanBufferData * CreateBufferOnGPU( VkDeviceSize bufferSize, VkBufferUsageFlags usage );
		bool TransferToGPU( VkBuffer bufferOnGPU, const void * data, VkDeviceSize bufferSize );

	private:
		VulkanState * fState;
		VulkanBufferData * fVertexBufferData;
		VulkanBufferData * fIndexBufferData;
		Geometry * fResource;
		void * fMappedVertices;
		U32 fVertexCount;
		U32 fIndexCount;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanGeometry_H__
