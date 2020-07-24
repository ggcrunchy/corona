//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Renderer/Rtt_VulkanState.h"
#include "Renderer/Rtt_VulkanGeometry.h"

#include "Renderer/Rtt_Geometry_Renderer.h"
/*
#include "Renderer/Rtt_GL.h"

#if defined( Rtt_EGL )
	#include <EGL/egl.h>
#endif

#include <stdio.h>
	*/
// ----------------------------------------------------------------------------

namespace /*anonymous*/
{
	using namespace Rtt;
/*
	void createVBO(Geometry* geometry, GLuint& VBO, GLuint& IBO)
	{
		glGenBuffers( 1, &VBO ); GL_CHECK_ERROR();
		glBindBuffer( GL_ARRAY_BUFFER, VBO ); GL_CHECK_ERROR();

		glEnableVertexAttribArray( Geometry::kVertexPositionAttribute );
		glEnableVertexAttribArray( Geometry::kVertexTexCoordAttribute );
		glEnableVertexAttribArray( Geometry::kVertexColorScaleAttribute );
		glEnableVertexAttribArray( Geometry::kVertexUserDataAttribute );
		GL_CHECK_ERROR();

		const Geometry::Vertex* vertexData = geometry->GetVertexData();
		if ( !vertexData )
		{
			GL_LOG_ERROR( "Unable to initialize GPU geometry. Data is NULL" );
		}

		// It is valid to pass a NULL pointer, so allocation is done either way
		const U32 vertexCount = geometry->GetVerticesAllocated();
		glBufferData( GL_ARRAY_BUFFER, vertexCount * sizeof(Geometry::Vertex), vertexData, GL_STATIC_DRAW );
		GL_CHECK_ERROR();
		
		const Geometry::Index* indexData = geometry->GetIndexData();
		if ( indexData )
		{
			const U32 indexCount = geometry->GetIndicesAllocated();
			glGenBuffers( 1, &IBO );
			glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, IBO );
			glBufferData( GL_ELEMENT_ARRAY_BUFFER, indexCount * sizeof(Geometry::Index), indexData, GL_STATIC_DRAW );
		}

	}

    void createVertexBuffer() {
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
            memcpy(data, vertices.data(), (size_t) bufferSize);
        vkUnmapMemory(device, stagingBufferMemory);

        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);

        copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
    }

    void createIndexBuffer() {
        VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
            memcpy(data, indices.data(), (size_t) bufferSize);
        vkUnmapMemory(device, stagingBufferMemory);

        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);

        copyBuffer(stagingBuffer, indexBuffer, bufferSize);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
    }
	*/
}

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

VulkanGeometry::VulkanGeometry( VulkanState * state )
:	fState( state ),
	fVertexBuffer( VK_NULL_HANDLE ),
	fIndexBuffer( VK_NULL_HANDLE ),
	fVertexBufferMemory( VK_NULL_HANDLE ),
	fIndexBufferMemory( VK_NULL_HANDLE )
/*
:	fPositionStart( NULL ),
	fTexCoordStart( NULL ),
	fColorScaleStart( NULL ),
	fUserDataStart( NULL )
*/
{
}

void
VulkanGeometry::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kGeometry == resource->GetType() );
	Geometry* geometry = static_cast<Geometry*>( resource );

	bool shouldStoreOnGPU = geometry->GetStoredOnGPU();
	if ( shouldStoreOnGPU )
	{
/*
		createVBO( geometry, fVBO, fIBO );

		Geometry::Vertex kVertex; // Uninitialized! Used for offset calculation only.

		// Initialize offsets
		fPositionStart = NULL;
		fTexCoordStart = (GLvoid *)((S8*)&kVertex.u - (S8*)&kVertex);
		fColorScaleStart = (GLvoid *)((S8*)&kVertex.rs - (S8*)&kVertex);
		fUserDataStart = (GLvoid *)((S8*)&kVertex.ux - (S8*)&kVertex);

		fVertexCount = geometry->GetVerticesAllocated();
		fIndexCount = geometry->GetIndicesAllocated();
*/
	}
	else
	{
		Update( resource );
	}
}

void
VulkanGeometry::Update( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kGeometry == resource->GetType() );
	Geometry* geometry = static_cast<Geometry*>( resource );
	/*
	if ( fVBO )
	{
		// The user may have resized the given Geometry instance
		// since the last call to update (see Geometry::Resize()).
		if ( fVertexCount < geometry->GetVerticesAllocated() ||
			 fIndexCount < geometry->GetIndicesAllocated() )
		{
			destroyVBO( fVBO, fIBO );
			createVBO( geometry, fVBO, fIBO );
			fVertexCount = geometry->GetVerticesAllocated();
			fIndexCount = geometry->GetIndicesAllocated();
		}
		
		// Copy the vertex data from main memory to GPU memory.
		const Geometry::Vertex* vertexData = geometry->GetVertexData();
		if ( vertexData )
		{
			glBindBuffer( GL_ARRAY_BUFFER, fVBO );
			glBufferSubData( GL_ARRAY_BUFFER, 0, fVertexCount * sizeof(Geometry::Vertex), vertexData );
			glBindBuffer( GL_ARRAY_BUFFER, 0 );
		}
		else
		{
			GL_LOG_ERROR( "Unable to update GPU geometry. Data is NULL" );
		}
	}
	else
	{
		Geometry::Vertex* data = geometry->GetVertexData();
		fPositionStart = data;
		fTexCoordStart = &data[0].u;
		fColorScaleStart = &data[0].rs;
		fUserDataStart = &data[0].ux;
	}
	GL_CHECK_ERROR();*/
}

void
VulkanGeometry::Destroy()
{
	/*
		fPositionStart = NULL;
		fTexCoordStart = NULL;
		fColorScaleStart = NULL;
		fUserDataStart = NULL;
	*/

	VkDevice device = fState->GetDevice();
	VkAllocationCallbacks * callbacks = fState->GetAllocationCallbacks();

    vkDestroyBuffer( device, fIndexBuffer, callbacks );
	vkFreeMemory( device, fIndexBufferMemory, callbacks );

    vkDestroyBuffer( device, fVertexBuffer, callbacks );
    vkFreeMemory( device, fVertexBufferMemory, callbacks );

	fIndexBuffer = fVertexBuffer = VK_NULL_HANDLE;
	fIndexBufferMemory = fVertexBufferMemory = VK_NULL_HANDLE;
}

VulkanGeometry::VertexDescription 
VulkanGeometry::Bind()
{
	VertexDescription desc;

	desc.fDescription.binding = 0U;
	desc.fDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	desc.fDescription.stride = sizeof( Geometry::Vertex );

	return desc;
/*
	Rtt_ASSERT( fPositionStart || fVBO); // offset is 0 when VBO is available
	Rtt_ASSERT( fTexCoordStart );
	Rtt_ASSERT( fColorScaleStart );
	Rtt_ASSERT( fUserDataStart );
		
	glBindBuffer( GL_ARRAY_BUFFER, fVBO ); GL_CHECK_ERROR();
		
	const size_t size = sizeof(Geometry::Vertex);
	glVertexAttribPointer( Geometry::kVertexPositionAttribute, 3, GL_FLOAT, GL_FALSE, size, fPositionStart ); GL_CHECK_ERROR();
	glVertexAttribPointer( Geometry::kVertexTexCoordAttribute, 3, GL_FLOAT, GL_FALSE, size, fTexCoordStart ); GL_CHECK_ERROR();
	glVertexAttribPointer( Geometry::kVertexColorScaleAttribute, 4, GL_UNSIGNED_BYTE, GL_TRUE, size, fColorScaleStart ); GL_CHECK_ERROR();
	glVertexAttribPointer( Geometry::kVertexUserDataAttribute, 4, GL_FLOAT, GL_FALSE, size, fUserDataStart ); GL_CHECK_ERROR();
		
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, fIBO ); GL_CHECK_ERROR();
*/
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------