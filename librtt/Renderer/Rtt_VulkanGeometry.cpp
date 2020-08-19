//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Renderer/Rtt_VulkanRenderer.h"
#include "Renderer/Rtt_VulkanState.h"
#include "Renderer/Rtt_VulkanGeometry.h"

#include "Renderer/Rtt_Geometry_Renderer.h"
#include "CoronaLog.h"
/*
#include "Renderer/Rtt_GL.h"

#if defined( Rtt_EGL )
	#include <EGL/egl.h>
#endif

#include <stdio.h>
	*/

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

VulkanGeometry::VulkanGeometry( VulkanState * state )
:	fState( state ),
	fVertexBufferData( NULL ),
	fIndexBufferData( NULL ),
	fMappedVertices( NULL ),
	fVertexCount( 0U ),
	fIndexCount( 0U )
{
}

void
VulkanGeometry::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kGeometry == resource->GetType() );
	Geometry* geometry = static_cast< Geometry * >( resource );
	VkDeviceSize verticesSize = fVertexCount * sizeof( Geometry::Vertex );
	
	fVertexCount = geometry->GetVerticesAllocated();
	fIndexCount = geometry->GetIndicesAllocated();

	if ( geometry->GetStoredOnGPU() )
	{
		fVertexBufferData = CreateBufferOnGPU( verticesSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT );

		TransferToGPU( fVertexBufferData->GetBuffer(), geometry->GetVertexData(), verticesSize );

		Geometry::Index * indices = geometry->GetIndexData();

		if (indices)
		{
			VkDeviceSize indicesSize = fIndexCount * sizeof( Geometry::Index );

			fIndexBufferData = CreateBufferOnGPU( indicesSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT );

			TransferToGPU( fIndexBufferData->GetBuffer(), indices, indicesSize );
		}
	}
	else
	{
        VulkanBufferData bufferData = fState->CreateBuffer( verticesSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );

		fMappedVertices = fState->MapData( bufferData.GetMemory(), verticesSize );
		fVertexBufferData = bufferData.Extract( NULL );

		Update( resource );
	}
}

void
VulkanGeometry::Update( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kGeometry == resource->GetType() );
	Geometry* geometry = static_cast<Geometry*>( resource );
	const Geometry::Vertex* vertexData = geometry->GetVertexData();

	if ( !fMappedVertices )
	{
		// The user may have resized the given Geometry instance
		// since the last call to update (see Geometry::Resize()).
		if ( fVertexCount < geometry->GetVerticesAllocated() ||
			 fIndexCount < geometry->GetIndicesAllocated() )
		{
			Destroy();
			Create( resource );
		}
		
		// Copy the vertex data from main memory to GPU memory.
		else if ( vertexData )
		{
			TransferToGPU( fVertexBufferData->GetBuffer(), vertexData, fVertexCount * sizeof( Geometry::Vertex ) );
		}
		else
		{
			CoronaLog( "Unable to update GPU geometry. Data is NULL" );
		}
	}
	else
	{
		memcpy( fMappedVertices, vertexData, fVertexCount * sizeof( Geometry::Vertex ) );
	}
}

void
VulkanGeometry::Destroy()
{
	if (fMappedVertices)
	{
		vkUnmapMemory( fState->GetDevice(), fVertexBufferData->GetMemory() );

		fMappedVertices = NULL;
	}

	Rtt_DELETE( fVertexBufferData );
	Rtt_DELETE( fIndexBufferData );

	fVertexBufferData = fIndexBufferData = NULL;
}

void 
VulkanGeometry::Bind( VulkanRenderer & renderer, VkCommandBuffer commandBuffer )
{
	U32 bindingID = 0U; // n.b. for future use?

	VkVertexInputBindingDescription description;

	description.binding = 0U;
	description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	description.stride = sizeof( Geometry::Vertex );

	std::vector< VkVertexInputBindingDescription > inputBindingDescriptions;

	inputBindingDescriptions.push_back( description );

	renderer.SetBindingDescriptions( bindingID, inputBindingDescriptions );

	VkBuffer vertexBuffer = fVertexBufferData->GetBuffer();
	VkDeviceSize offset = 0U;

	vkCmdBindVertexBuffers( commandBuffer, 0U, 1U, &vertexBuffer, &offset );

	if (fIndexBufferData != VK_NULL_HANDLE)
	{
		vkCmdBindIndexBuffer( commandBuffer, fIndexBufferData->GetBuffer(), 0U, VK_INDEX_TYPE_UINT16 );
	}
}

VulkanBufferData *
VulkanGeometry::CreateBufferOnGPU( VkDeviceSize bufferSize, VkBufferUsageFlags usage )
{
    VulkanBufferData bufferData = fState->CreateBuffer( bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	return bufferData.Extract( NULL );
}

bool
VulkanGeometry::TransferToGPU( VkBuffer bufferOnGPU, const void * data, VkDeviceSize bufferSize )
{
    VulkanBufferData stagingBuffer = fState->CreateBuffer( bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
	bool ableToTransfer = stagingBuffer.IsValid();

	if (ableToTransfer)
	{
		fState->StageData( stagingBuffer.GetMemory(), data, bufferSize );
		fState->CopyBuffer( stagingBuffer.GetBuffer(), bufferOnGPU, bufferSize );
	}

	return ableToTransfer;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------