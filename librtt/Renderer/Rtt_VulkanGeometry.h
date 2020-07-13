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

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();
		virtual void Bind();

	private:
		/*
		GLvoid* fPositionStart;
		GLvoid* fTexCoordStart;
		GLvoid* fColorScaleStart;
		GLvoid* fUserDataStart;
		GLuint fVAO;
		GLuint fVBO;
		GLuint fIBO;
		U32 fVertexCount;
		U32 fIndexCount;
		*/
		VulkanState * fState;
		VkBuffer fVertexBuffer;
		VkBuffer fIndexBuffer;
		VkDeviceMemory fVertexBufferMemory;
		VkDeviceMemory fIndexBufferMemory;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanGeometry_H__
