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
#include <vulkan/vulkan.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

class VulkanState;

// ----------------------------------------------------------------------------

class VulkanGeometry : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef VulkanGeometry Self;

	public:
		VulkanGeometry( VulkanState * state );

		struct VertexDescription {
			VertexDescription()
			{
			}

			VkVertexInputBindingDescription fDescription;
		//	U32 fID;
		};

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();

		VertexDescription Bind();

	private:
		/*
		GLvoid* fPositionStart;
		GLvoid* fTexCoordStart;
		GLvoid* fColorScaleStart;
		GLvoid* fUserDataStart;
		U32 fVertexCount;
		U32 fIndexCount;
		*/
		VulkanState * fState;
		VkBuffer fVertexBuffer;
		VkBuffer fIndexBuffer;
		VkDeviceMemory fVertexBufferMemory;
		VkDeviceMemory fIndexBufferMemory;
/*

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
*/
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanGeometry_H__
