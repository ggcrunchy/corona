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
#include "Renderer/Rtt_FrameBufferObject.h"
#include "Core/Rtt_Assert.h"
#include "CoronaLog.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

Descriptor::Descriptor(  VkDescriptorSetLayout setLayout )
:	fSetLayout( setLayout ),
	fDirty( 0U )
{
}

bool
Descriptor::IsMaskPushConstant( int index )
{
	return index >= Uniform::kMaskMatrix0 && index <= Uniform::kMaskMatrix2;
}

bool
Descriptor::IsPushConstant( int index, bool userDataPushConstants )
{
	return Uniform::kTotalTime == index
		|| Uniform::kTexelSize == index
		|| IsMaskPushConstant( index )
		|| (userDataPushConstants && index >= Uniform::kUserData0 && index <= Uniform::kUserData3);
}

bool
Descriptor::IsUserData( int index )
{
	return index >= Uniform::kUserData0;
}

BufferData::BufferData()
:	fData( NULL ),
	fMapped( NULL ),
	fSet( VK_NULL_HANDLE )
{
}

void
BufferData::Wipe()
{
	if (fMapped)
	{
		Rtt_ASSERT( fData );

		vkUnmapMemory( fData->GetDevice(), fData->GetMemory() );
	}

	Rtt_DELETE( fData );
}

BufferDescriptor::BufferDescriptor( VulkanState * state, VkDescriptorPool pool, VkDescriptorSetLayout setLayout, VkDescriptorType type, size_t count, size_t size )
:	Descriptor( setLayout ),
	fLastSet( VK_NULL_HANDLE ),
	fType( type ),
	fDynamicAlignment( 0U ),
	fWorkspace( NULL ),
	fIndex( 0U ),
	fOffset( 0U ),
	fLastOffset( 0U ),
	fAtomSize( 0U ),
	fBufferSize( 0U ),
	fRawSize( size ),
	fNonCoherentRawSize( size ),
	fWritten( 0U ),
	fMarkWritten( false )
{
	const VkPhysicalDeviceLimits & limits = state->GetProperties().limits;
	VulkanBufferData bufferData( state->GetDevice(), state->GetAllocator() );

	VkBufferUsageFlags usageFlags = 0;
	uint32_t alignment = 0U, maxSize = ~0U;

	if (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER == type || VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC == type)
	{
		usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		alignment = limits.minUniformBufferOffsetAlignment;
		maxSize = limits.maxUniformBufferRange;
	}

	else if (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == type || VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC == type)
	{
		usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		alignment = limits.minStorageBufferOffsetAlignment;
		maxSize = limits.maxStorageBufferRange;
	}

	bool isDynamic = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC == type || VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC == type;

	fDynamicAlignment = fRawSize;

	if (isDynamic && alignment > 0U)
	{
		fDynamicAlignment = (fDynamicAlignment + alignment - 1) & ~(alignment - 1);
	}

	fAtomSize = limits.nonCoherentAtomSize;

	U32 remainder = fNonCoherentRawSize % fAtomSize;

	if (remainder)
	{
		fNonCoherentRawSize += limits.nonCoherentAtomSize - remainder;

		Rtt_ASSERT( !isDynamic || fNonCoherentRawSize <= fDynamicAlignment );
	}

	fBufferSize = isDynamic ? U32( count * fDynamicAlignment ) : fRawSize;

	U32 n = fBufferSize / maxSize + (fBufferSize % maxSize ? 1 : 0);

	if (n > 1U)
	{
		fBufferSize = maxSize;
	}

// ^^ TODO: this is somewhat limiting for SSBO case

	for (U32 i = 0; i < n; ++i)
	{
		if (state->CreateBuffer( fBufferSize, usageFlags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, bufferData ))
		{
			BufferData buffer;

			buffer.fMapped = state->MapData( bufferData.GetMemory() );
			buffer.fData = bufferData.Extract( NULL );

			VkDescriptorSetAllocateInfo allocInfo = {};

			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = pool;
			allocInfo.descriptorSetCount = 1U;
			allocInfo.pSetLayouts = &fSetLayout;

			if (VK_SUCCESS == vkAllocateDescriptorSets( state->GetDevice(), &allocInfo, &buffer.fSet ))
			{
				VkWriteDescriptorSet descriptorWrite = {};
				VkDescriptorBufferInfo bufferInfo = {};

				bufferInfo.buffer = buffer.fData->GetBuffer();
				bufferInfo.range = fRawSize;// BufferSize;

				descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrite.descriptorCount = 1U;
				descriptorWrite.descriptorType = type;
				descriptorWrite.dstSet = buffer.fSet;
				descriptorWrite.pBufferInfo = &bufferInfo;

				vkUpdateDescriptorSets( state->GetDevice(), 1U, &descriptorWrite, 0U, NULL );

				fBuffers.push_back( buffer );
			}

			else
			{
				buffer.Wipe();

				CoronaLog( "Failed to allocate buffer descriptor" );
			}
		}

		else
		{
			CoronaLog( "Failed to create buffer" );

			break;
		}
	}
}

void
BufferDescriptor::Reset( VkDevice )
{
	fLastSet = VK_NULL_HANDLE;
	fIndex = fOffset = fLastOffset = 0U;
	fDirty = fWritten = 0U;
}

void
BufferDescriptor::Wipe( VkDevice, const VkAllocationCallbacks * )
{
	for (BufferData & buffer : fBuffers)
	{
		buffer.Wipe();
	}

	fBuffers.clear();
}

void
BufferDescriptor::SetWorkspace( void * workspace )
{
	fWorkspace = static_cast< U8 * >( workspace );
}

void
BufferDescriptor::TryToAddMemory( std::vector< VkMappedMemoryRange > & ranges, VkDescriptorSet sets[], size_t & count )
{
	const BufferData & buffer = fBuffers[fIndex];
	bool allWritten = (fWritten & fDirty) == fDirty;

	if (!allWritten)
	{
		bool dynamicBuffer = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC == fType || VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC == fType;

		if (dynamicBuffer && fIndex == fBuffers.size())
		{
			CoronaLog( "Failed to add memory: buffer full!" );

			return; // TODO: much more than this needs hardening
		}

		memcpy( static_cast< U8 * >( buffer.fMapped ) + fOffset, fWorkspace, fRawSize );
			
		VkMappedMemoryRange range = {};

		bool isFull = fOffset + fNonCoherentRawSize >= fBufferSize;

		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.memory = buffer.fData->GetMemory();
		range.offset = fOffset; // TODO: must be multiple of atom size... how to handle if alignment disagrees?
		range.size =  isFull ? VK_WHOLE_SIZE : fNonCoherentRawSize;

		ranges.push_back( range );

		fLastSet = buffer.fSet;
		fLastOffset = fOffset;

		if (dynamicBuffer)
		{
			if (isFull)
			{
				fOffset = 0U;

				++fIndex;
			}

			else
			{
				fOffset += U32( fDynamicAlignment );
			}
		}
	}

	sets[count++] = allWritten ? fLastSet : buffer.fSet;
}
	
void
BufferDescriptor::TryToAddDynamicOffset( uint32_t offsets[], size_t & count )
{
	if (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC == fType || VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC == fType)
	{
		offsets[count++] = (fWritten & fDirty) == fDirty ? fLastOffset : fOffset;
	}
}

static VkDescriptorPool
AddPool( VulkanState * state, const VkDescriptorPoolSize * sizes, uint32_t sizeCount, U32 maxSets, VkDescriptorPoolCreateFlags flags = 0 )
{
	VkDescriptorPoolCreateInfo poolInfo = {};

	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = flags;
	poolInfo.maxSets = maxSets;
	poolInfo.poolSizeCount = sizeCount;
	poolInfo.pPoolSizes = sizes;

	VkDescriptorPool pool = VK_NULL_HANDLE;

	if (VK_SUCCESS == vkCreateDescriptorPool( state->GetDevice(), &poolInfo, state->GetAllocator(), &pool ))
	{
		return pool;
	}

	else
	{
		CoronaLog( "Failed to create descriptor pool!" );

		return VK_NULL_HANDLE;
	}
}

TexturesDescriptor::TexturesDescriptor( VulkanState * state, VkDescriptorSetLayout setLayout )
:	Descriptor( setLayout )
{
	const U32 arrayCount = 1024U; // TODO: is this how to allocate this? (maybe arrays are just too complex / wasteful for the common case)
	const U32 descriptorCount = arrayCount * 5U; // 2 + 3 masks (TODO: but could be more flexible? e.g. already reflected in VulkanProgram)

	VkDescriptorPoolSize poolSize;

	poolSize.descriptorCount = descriptorCount;
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	fPool = AddPool( state, &poolSize, 1U, arrayCount );
}

void
TexturesDescriptor::Reset( VkDevice device )
{
	vkResetDescriptorPool( device, fPool, 0 );

	fDirty = 0U;
}

static void
WipeDescriptorPool( VkDevice device, VkDescriptorPool pool, const VkAllocationCallbacks * allocator )
{
	if (VK_NULL_HANDLE != pool)
	{
		vkResetDescriptorPool( device, pool, 0 );
		vkDestroyDescriptorPool( device, pool, allocator );
	}
}

void
TexturesDescriptor::Wipe( VkDevice device, const VkAllocationCallbacks * allocator )
{
	WipeDescriptorPool( device, fPool, allocator );
}

VulkanRenderer::VulkanRenderer( Rtt_Allocator* allocator, VulkanState * state )
:   Super( allocator ),
	fState( state ),
    fSwapchainTexture( NULL ),
	fPrimaryFBO( NULL ),
	fFirstPipeline( VK_NULL_HANDLE ),
	fPool( VK_NULL_HANDLE ),
	fUniformsLayout( VK_NULL_HANDLE ),
	fUserDataLayout( VK_NULL_HANDLE ),
	fTextureLayout( VK_NULL_HANDLE ),
	fPipelineLayout( VK_NULL_HANDLE )
{
	fFrontCommandBuffer = Rtt_NEW( allocator, VulkanCommandBuffer( allocator, *this ) );
	fBackCommandBuffer = Rtt_NEW( allocator, VulkanCommandBuffer( allocator, *this ) );

	VkPushConstantRange pushConstantRange;

	pushConstantRange.offset = 0U;
	pushConstantRange.size = sizeof( VulkanPushConstants );
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo createDescriptorSetLayoutInfo = {};
	VkDescriptorSetLayoutBinding bindings[2] = {};

	createDescriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	createDescriptorSetLayoutInfo.bindingCount = 1U;
	createDescriptorSetLayoutInfo.pBindings = bindings;

	bindings[0].descriptorCount = 1U;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	if (VK_SUCCESS == vkCreateDescriptorSetLayout( state->GetDevice(), &createDescriptorSetLayoutInfo, state->GetAllocator(), &fUniformsLayout ))
	{
	}

	else
	{
		CoronaLog( "Failed to create UBO descriptor set layout!" );
	}

	if (VK_SUCCESS == vkCreateDescriptorSetLayout( state->GetDevice(), &createDescriptorSetLayoutInfo, state->GetAllocator(), &fUserDataLayout ))
	{
	}

	else
	{
		CoronaLog( "Failed to create uniform user data descriptor set layout!" );
	}

	// if samplerIndexing...
		//	createDescriptorSetLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT; TODO: this seems right?
	bindings[0].descriptorCount = 5U; // TODO: locks in texture count, maybe later we'll want a higher value?
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	// ^^ this will be MUCH different if the hardware can support push constant-indexed samplers, e.g. something like
		// easy to do this without post-bind update?
		// bindingCount = 2U
		// binging 0: 1 (VK_DESCRIPTOR_TYPE_SAMPLER) descriptor
		// binding 1: 4096 (VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) descriptors

	if (VK_SUCCESS == vkCreateDescriptorSetLayout( state->GetDevice(), &createDescriptorSetLayoutInfo, state->GetAllocator(), &fTextureLayout ))
	{
	}

	else
	{
		CoronaLog( "Failed to create texture descriptor set layout!" );
	}
	
	VkPipelineLayoutCreateInfo createPipelineLayoutInfo = {};
	VkDescriptorSetLayout layouts[] = { fUniformsLayout, fUserDataLayout, fTextureLayout };

	createPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	createPipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
	createPipelineLayoutInfo.pSetLayouts = layouts;
	createPipelineLayoutInfo.pushConstantRangeCount = 1U;
	createPipelineLayoutInfo.setLayoutCount = 3U;

	if (VK_SUCCESS == vkCreatePipelineLayout( state->GetDevice(), &createPipelineLayoutInfo, state->GetAllocator(), &fPipelineLayout ))
	{
	}

	else
	{
		CoronaLog( "Failed to create pipeline layout!" );
	}

	fSwapchainTexture = Rtt_NEW( allocator, TextureSwapchain( allocator, state ) );
}

VulkanRenderer::~VulkanRenderer()
{
	vkQueueWaitIdle( fState->GetGraphicsQueue() );

	TearDownSwapchain();

	Rtt_DELETE( fSwapchainTexture );

	WipeDescriptors();

	auto ci = GetState()->GetCommonInfo();

	WipeDescriptorPool( ci.device, fPool, ci.allocator );

	for (auto & pipeline : fBuiltPipelines)
	{
		vkDestroyPipeline( ci.device, pipeline.second, ci.allocator );
	}

	vkDestroyPipelineLayout( ci.device, fPipelineLayout, ci.allocator );
	vkDestroyDescriptorSetLayout( ci.device, fUniformsLayout, ci.allocator );
	vkDestroyDescriptorSetLayout( ci.device, fUserDataLayout, ci.allocator );
	vkDestroyDescriptorSetLayout( ci.device, fTextureLayout, ci.allocator );

	// ^^ might want more descriptor set layouts, e.g. for compute or to allow different kinds of buffer inputs
	// make vectors of both layout types
}

void
VulkanRenderer::BeginFrame( Real totalTime, Real deltaTime, Real contentScaleX, Real contentScaleY, bool isCapture )
{
	InitializePipelineState();

	VulkanCommandBuffer * vulkanCommandBuffer = static_cast< VulkanCommandBuffer * >( fBackCommandBuffer );
	VkResult result = vulkanCommandBuffer->GetExecuteResult();

	vulkanCommandBuffer->BeginFrame();

	bool canContinue = VK_SUCCESS == result;
	VkSwapchainKHR swapchain = fState->GetSwapchain();
	
	if (!isCapture && VK_NULL_HANDLE == swapchain)
	{
		VulkanState::PopulateSwapchainDetails( *fState );

		swapchain = MakeSwapchain();

		if (swapchain != VK_NULL_HANDLE)
		{
			BuildUpSwapchain( swapchain );

			fPrimaryFBO = Rtt_NEW( fAllocator, FrameBufferObject( fAllocator, fSwapchainTexture ) );
		}

		else
		{
			canContinue = false;
		}
	}

	uint32_t index;

	if (canContinue)
	{
		if (isCapture)
		{
			result = vulkanCommandBuffer->Wait( fState->GetDevice() );

			index = fCommandBuffers.size() - 1U;
		}

		else
		{
			result = vulkanCommandBuffer->WaitAndAcquire( fState->GetDevice(), swapchain, index );
		}

		canContinue = VK_SUCCESS == result || VK_SUBOPTIMAL_KHR == result;
	}

	if (!isCapture && fPrimaryFBO)
	{
		SetFrameBufferObject( fPrimaryFBO );
	}

	Super::BeginFrame( totalTime, deltaTime, contentScaleX, contentScaleY );

	if (canContinue)
	{
		vulkanCommandBuffer->BeginRecording( fCommandBuffers[index], fDescriptors.data() + 3U * index );
	}

	else
	{
		if (VK_ERROR_OUT_OF_DATE_KHR == result)
		{
		//	fState->RebuildSwapChain();
			// TODO: should we then try again?
			// or can we just do that straight out?
				// if an acquire fails, then what????
			RecreateSwapchain();
			CoronaLog( "Out of date" );
		}
	
		else
		{
			CoronaLog( "Failed to acquire swap chain image!" );
		}
	}

	vulkanCommandBuffer->ClearExecuteResult();
}

void
VulkanRenderer::EndFrame()
{
	Super::EndFrame();

	SetFrameBufferObject(NULL);
	// TODO: this always precedes Swap(), so hook up resources for Create() / Update()
	// yank them in Execute()
}

void
VulkanRenderer::CaptureFrameBuffer( RenderingStream & stream, BufferBitmap & bitmap, S32 x_in_pixels, S32 y_in_pixels, S32 w_in_pixels, S32 h_in_pixels )
{
	// TODO

	// https://community.khronos.org/t/readpixels-on-vulkan/6797

	/*
	
	
	renderpass etc. etc.):


	VkImage imageSource;
	VkImageCreateInfo isci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UINT, {width,height, 1},
	                   1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 1, &queueFamily };
	VkResult errorCode = vkCreateImage( device, &isci, nullptr, &imageSource ); RESULT_HANDLER( errorCode, "vkCreateImage" );

	VkMemoryRequirements ismr;
	vkGetImageMemoryRequirements( device, imageSource, &ismr );

	uint32_t memoryType = 0; bool found = false;
	for( uint32_t i = 0; i < 32; ++i ){
		if(  ( ismr.memoryTypeBits & (0x1 << i) )  &&  physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  ){
				memoryType = i; found = true; break;
		}
	}
	if( !found ) throw "Can't find compatible mappable memory for image";

	VkDeviceMemory memorySource;
	VkMemoryAllocateInfo memoryInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, ismr.size, memoryType };
	errorCode = vkAllocateMemory( device, &memoryInfo, nullptr, &memorySource ); RESULT_HANDLER( errorCode, "vkAllocateMemory" );
	errorCode = vkBindImageMemory( device, imageSource, memorySource, 0 ); RESULT_HANDLER( errorCode, "vkBindImageMemory" );

	VkImageView imageSourceView;
	VkImageViewCreateInfo isvci{  VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, imageSource, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
	                              { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY },
	                              { VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS }  };
	errorCode = vkCreateImageView( device, &isvci, nullptr, &imageSourceView ); RESULT_HANDLER( errorCode, "vkCreateImageView" );

	VkFramebuffer framebufferSource;
	VkFramebufferCreateInfo fsci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, renderPass, 1, &imageSourceView, width, height, 1 };
	errorCode = vkCreateFramebuffer( device, &fsci, nullptr, &framebufferSource ); RESULT_HANDLER( errorCode, "vkCreateFramebuffer" );


	VkCommandBuffer renderCommandBuffer;
	VkCommandBufferAllocateInfo rcbai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
	errorCode = vkAllocateCommandBuffers( device, &rcbai, &renderCommandBuffer ); RESULT_HANDLER( errorCode, "vkAllocateCommandBuffers" );

	beginCommandBuffer( renderCommandBuffer );
		VkImageMemoryBarrier predrawBarrier{  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                                      VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, imageSource, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }  };
		vkCmdPipelineBarrier( renderCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &predrawBarrier );

		VkRenderPassBeginInfo rpbi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr, renderPass, framebufferSource, {{0,0}, {width,height}}, 1, &clearColor };
		vkCmdBeginRenderPass( renderCommandBuffer, &rpbi, VK_SUBPASS_CONTENTS_INLINE );

		vkCmdBindPipeline( renderCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		vkCmdBindDescriptorSets( renderCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr );
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers( renderCommandBuffer, vertexBufferBinding, 1, &vertexBuffer, offsets );

		VkViewport viewport{ 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
		vkCmdSetViewport( renderCommandBuffer, 0, 1, &viewport );
		VkRect2D scissor{ {0, 0}, {width, height} };
		vkCmdSetScissor( renderCommandBuffer, 0, 1, &scissor );

		vkCmdDraw( renderCommandBuffer, triangle.size(), 1, 0, 0 );

		vkCmdEndRenderPass( renderCommandBuffer );

		VkImageMemoryBarrier premapBarrier{  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
		                                      VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, imageSource, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }  };
		vkCmdPipelineBarrier( renderCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &premapBarrier );
	endCommandBuffer( renderCommandBuffer );

	vkDeviceWaitIdle( device );
	VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &renderCommandBuffer, 0, nullptr };
	errorCode = vkQueueSubmit( queue, 1, &submit, VK_NULL_HANDLE ); RESULT_HANDLER( errorCode, "vkQueueSubmit" );
	vkQueueWaitIdle( queue );

	void* data;
	errorCode = vkMapMemory( device, memorySource, 0, VK_WHOLE_SIZE, 0, &data ); RESULT_HANDLER( errorCode, "vkMapMemory" );
	std::ofstream ofs( "out.raw", std::ostream::binary );
	ofs.write( (char*)data, width * height * 4 );
	vkUnmapMemory( device, memorySource );

	vkDestroyFramebuffer( device, framebufferSource, nullptr );
	vkDestroyImageView( device, imageSourceView, nullptr );
	vkFreeMemory( device, memorySource, nullptr );
	vkDestroyImage( device, imageSource, nullptr );
	*/

	/*
	* 
	* Please don’t use VK_IMAGE_TILING_LINEAR or HOST_VISIBLE unless absolutely required, as there will be performance hits depending on the GPU.

For the use-case of “readpixels” it is much better to keep the framebuffer images “fast” with VK_IMAGE_TILING_OPTIMAL and not in CPU-memory (HOST_VISIBLE).

Just like when working with PBOs in OpenGL, use a separate “staging” image or buffer (which is host visible for mapping) that you copy your “fast” render
framebuffer image into. That way the actual rendering is not affected by non-optimal formats/memory type, and that is where you will access the memory
fore-most. The cost to copy the data and potentially transform the format will not be as bad, as the permanent hit you get when rendering.
	* 
	* 
	* 
	* 
	* 
	* 
	* 
	* 
	* Yeah, there is an error in the example. Definitely don’t want host allocated mamory.
Bad: propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
right: propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)


	this here won’t work correctly, as it will return “true” if one of the bits is set, not both
propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)

	for both bits need
(propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	*/
}

VkSwapchainKHR
VulkanRenderer::MakeSwapchain()
{
    const VulkanState::SwapchainDetails & details = fState->GetSwapchainDetails();
    auto & queueFamilies = fState->GetQueueFamilies();
	VkSwapchainCreateInfoKHR swapchainCreateInfo = {};

	swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCreateInfo.clipped = VK_TRUE;
	swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainCreateInfo.imageArrayLayers = 1U;
	swapchainCreateInfo.imageColorSpace = details.fFormat.colorSpace;
	swapchainCreateInfo.imageExtent = details.fExtent;
	swapchainCreateInfo.imageFormat = details.fFormat.format;
	swapchainCreateInfo.imageSharingMode = 1U == queueFamilies.size() ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
	swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainCreateInfo.minImageCount = details.fImageCount;
	swapchainCreateInfo.oldSwapchain = fState->GetSwapchain();
	swapchainCreateInfo.presentMode = details.fPresentMode;
	swapchainCreateInfo.preTransform = details.fTransformFlagBits; // TODO: relevant to portrait, landscape, etc?
	swapchainCreateInfo.surface = fState->GetSurface();

	if (VK_SHARING_MODE_CONCURRENT == swapchainCreateInfo.imageSharingMode)
	{
		swapchainCreateInfo.pQueueFamilyIndices = queueFamilies.data();
		swapchainCreateInfo.queueFamilyIndexCount = queueFamilies.size();
	}

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;

	if (VK_SUCCESS == vkCreateSwapchainKHR( fState->GetDevice(), &swapchainCreateInfo, fState->GetAllocator(), &swapchain ))
	{
		return swapchain;
	}

	else
	{
		CoronaLog( "Failed to create swap chain!" );

		return VK_NULL_HANDLE;
	}
}

void
VulkanRenderer::BuildUpSwapchain( VkSwapchainKHR swapchain )
{
	fState->SetSwapchain( swapchain );

	VkDevice device = fState->GetDevice();
	uint32_t imageCount;

	vkGetSwapchainImagesKHR( device, swapchain, &imageCount, NULL );
	
	fCommandBuffers.resize( imageCount + 1U );
	fSwapchainImages.resize( imageCount );

	VulkanState * state = GetState();

	VkDescriptorPoolSize size;

	size.descriptorCount = imageCount * 64U;
	size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

	fPool = AddPool( state, &size, 1U, imageCount * 80U ); // TODO: wild guess this many uniform buffers can satisfy 256 "render targets" / 1024 uniform blocks, cf. below

	for (uint32_t i = 0; i <= imageCount; ++i) // add one extra for captures
	{
		static_assert( Descriptor::kUniforms < Descriptor::kUserData, "Uniforms / buffer in unexpected order" );
		static_assert( Descriptor::kUserData < Descriptor::kTexture, "Buffer / textures in unexpected order" );

		fDescriptors.push_back( Rtt_NEW( NULL, BufferDescriptor( state, fPool, fUniformsLayout, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 256U, sizeof( VulkanUniforms ) ) ) );
		fDescriptors.push_back( Rtt_NEW( NULL, BufferDescriptor( state, fPool, fUserDataLayout, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024U, sizeof( VulkanUserData ) ) ) );
		fDescriptors.push_back( Rtt_NEW( NULL, TexturesDescriptor( state, fTextureLayout ) ) );
	}

	VkCommandBufferAllocateInfo allocInfo = {};

    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandBufferCount = imageCount + 1U;
	allocInfo.commandPool = fState->GetCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    if (VK_SUCCESS == vkAllocateCommandBuffers( device, &allocInfo, fCommandBuffers.data() ))
	{
		vkGetSwapchainImagesKHR( device, swapchain, &imageCount, fSwapchainImages.data() );

		// TODO: descriptors? etc.
	}

	else
	{
        CoronaLog( "Failed to allocate command buffers!" );
    }
}

void
VulkanRenderer::RecreateSwapchain()
{
	vkQueueWaitIdle( fState->GetGraphicsQueue() );

	VulkanState::UpdateSwapchainDetails( *fState );

	VkSwapchainKHR newSwapchain = MakeSwapchain();

	TearDownSwapchain();
	WipeDescriptorPool( fState->GetDevice(), fPool, fState->GetAllocator() );

	if (newSwapchain != VK_NULL_HANDLE )
	{
		BuildUpSwapchain( newSwapchain );
	}

	fPrimaryFBO = Rtt_NEW( fAllocator, FrameBufferObject( fAllocator, fSwapchainTexture ) );
}

void
VulkanRenderer::TearDownSwapchain()
{
    auto ci = fState->GetCommonInfo();

	vkDestroySwapchainKHR( ci.device, fState->GetSwapchain(), ci.allocator );

	WipeDescriptors();

	vkFreeCommandBuffers( ci.device, fState->GetCommandPool(), fCommandBuffers.size(), fCommandBuffers.data() );

    fState->SetSwapchain( VK_NULL_HANDLE );

	Rtt_DELETE( fPrimaryFBO );
}

const size_t kFinalBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
const size_t kFinalBlendOp = VK_BLEND_OP_MAX; // n.b. this is the max() operation; its use in this way is a coincidence

constexpr int BitsNeeded( int x ) // n.b. x > 0
{
	int result = 0;

	for (int power = 1; power <= x; power *= 2)
	{
		++result;
	}

	return result;
}

struct PackedBlendAttachment {
	U32 fEnable : 1;
	U32 fSrcColorFactor : BitsNeeded( kFinalBlendFactor );
	U32 fDstColorFactor : BitsNeeded( kFinalBlendFactor );
	U32 fColorOp : BitsNeeded( kFinalBlendOp );
	U32 fSrcAlphaFactor : BitsNeeded( kFinalBlendFactor );
	U32 fDstAlphaFactor : BitsNeeded( kFinalBlendFactor );
	U32 fAlphaOp : BitsNeeded( kFinalBlendOp );
	U32 fColorWriteMask : 4;
};

const size_t kFinalDynamicState = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
const size_t kDynamicStateCountRoundedUp = (kFinalDynamicState + 7U) & ~7U;
const size_t kDynamicStateByteCount = kDynamicStateCountRoundedUp / 8U;

const size_t kFinalCompareOp = VK_COMPARE_OP_ALWAYS;
const size_t kFinalFrontFace = VK_FRONT_FACE_CLOCKWISE;
const size_t kFinalLogicOp = VK_LOGIC_OP_SET;
const size_t kFinalPolygonMode = VK_POLYGON_MODE_POINT;
const size_t kFinalPrimitiveTopology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
const size_t kFinalStencilOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;

struct PackedPipeline {
    U64 fTopology : BitsNeeded( kFinalPrimitiveTopology );
    U64 fPrimitiveRestartEnable : 1;
	U64 fRasterizerDiscardEnable : 1;
	U64 fPolygonMode : BitsNeeded( kFinalPolygonMode );
	U64 fLineWidth : 4; // lineWidth = (X + 1) / 16
	U64 fCullMode : 2;
	U64 fFrontFace : BitsNeeded( kFinalFrontFace );
	U64 fRasterSamplesFlags : 7;
	U64 fSampleShadingEnable : 1;
	U64 fSampleShading : 5; // minSampleShading = X / 32
	U64 fAlphaToCoverageEnable : 1;
	U64 fAlphaToOneEnable : 1;
	U64 fDepthTestEnable : 1;
	U64 fDepthWriteEnable : 1;
	U64 fDepthCompareOp : BitsNeeded( kFinalCompareOp );
	U64 fDepthBoundsTestEnable : 1;
	U64 fStencilTestEnable : 1;
	U64 fFront : BitsNeeded( kFinalStencilOp );
	U64 fBack : BitsNeeded( kFinalStencilOp );
	U64 fMinDepthBounds : 5; // minDepthBounds = X / 32
	U64 fMaxDepthBounds : 5; // maxDepthBounds = (X + 1) / 32
	U64 fLogicOpEnable : 1;
	U64 fLogicOp : BitsNeeded( kFinalLogicOp );
	U64 fBlendConstant1 : 4; // blendConstants = X / 15
	U64 fBlendConstant2 : 4;
	U64 fBlendConstant3 : 4;
	U64 fBlendConstant4 : 4;
	U64 fLayoutID : 4;
	U64 fRenderPassID : 4;
	U64 fShaderID : 16;
	U64 fAttributeDescriptionID : 3;
	U64 fBindingDescriptionID : 3;
	U64 fBlendAttachmentCount : 3; // 0-7
	PackedBlendAttachment fBlendAttachments[8];
	uint8_t fDynamicStates[kDynamicStateByteCount];
};

static PackedPipeline &
GetPackedPipeline( std::vector< U64 > & contents )
{
    return *reinterpret_cast< PackedPipeline * >( contents.data() );
}

void
VulkanRenderer::EnableBlend( bool enabled )
{
    fPipelineCreateInfo.fColorBlendAttachments.front().blendEnable = enabled ? VK_TRUE : VK_FALSE;

	GetPackedPipeline( fWorkingKey.fContents ).fBlendAttachments[0].fEnable = enabled;
}

void
VulkanRenderer::SetAttributeDescriptions( U32 id, const std::vector< VkVertexInputAttributeDescription > & descriptions )
{
	fPipelineCreateInfo.fVertexAttributeDescriptions = descriptions;

	GetPackedPipeline( fWorkingKey.fContents ).fAttributeDescriptionID = id;
}

void
VulkanRenderer::SetBindingDescriptions( U32 id, const std::vector< VkVertexInputBindingDescription > & descriptions )
{
	fPipelineCreateInfo.fVertexBindingDescriptions = descriptions;

	GetPackedPipeline( fWorkingKey.fContents ).fBindingDescriptionID = id;
}

void
VulkanRenderer::SetBlendEquations( VkBlendOp color, VkBlendOp alpha )
{
	VkPipelineColorBlendAttachmentState & attachment = fPipelineCreateInfo.fColorBlendAttachments.front();

    attachment.alphaBlendOp = alpha;
	attachment.colorBlendOp = color;
    	
    PackedPipeline & packedPipeline = GetPackedPipeline( fWorkingKey.fContents );

	packedPipeline.fBlendAttachments[0].fAlphaOp = alpha;
	packedPipeline.fBlendAttachments[0].fColorOp = color;
}

void
VulkanRenderer::SetBlendFactors( VkBlendFactor srcColor, VkBlendFactor srcAlpha, VkBlendFactor dstColor, VkBlendFactor dstAlpha )
{
	VkPipelineColorBlendAttachmentState & attachment = fPipelineCreateInfo.fColorBlendAttachments.front();

	attachment.srcColorBlendFactor = srcColor;
	attachment.dstColorBlendFactor = dstColor;
	attachment.srcAlphaBlendFactor = srcAlpha;
	attachment.dstAlphaBlendFactor = dstAlpha;
    	
    PackedPipeline & packedPipeline = GetPackedPipeline( fWorkingKey.fContents );

	packedPipeline.fBlendAttachments[0].fSrcColorFactor = srcColor;
	packedPipeline.fBlendAttachments[0].fDstColorFactor = dstColor;
	packedPipeline.fBlendAttachments[0].fSrcAlphaFactor = srcAlpha;
	packedPipeline.fBlendAttachments[0].fDstAlphaFactor = dstAlpha;
}

void
VulkanRenderer::SetMultisample( VkSampleCountFlagBits sampleCount )
{
	fPipelineCreateInfo.fMultisample.rasterizationSamples = sampleCount;
    	
    PackedPipeline & packedPipeline = GetPackedPipeline( fWorkingKey.fContents );

	packedPipeline.fRasterSamplesFlags = sampleCount;
}

void
VulkanRenderer::SetPrimitiveTopology( VkPrimitiveTopology topology )
{
    fPipelineCreateInfo.fInputAssembly.topology = topology;

	GetPackedPipeline( fWorkingKey.fContents ).fTopology = topology;
}

void
VulkanRenderer::SetRenderPass( U32 id, VkRenderPass renderPass )
{
	fPipelineCreateInfo.fRenderPass = renderPass;

	GetPackedPipeline( fWorkingKey.fContents ).fRenderPassID = id;
}

void
VulkanRenderer::SetShaderStages( U32 id, const std::vector< VkPipelineShaderStageCreateInfo > & stages )
{
	fPipelineCreateInfo.fShaderStages = stages;

	GetPackedPipeline( fWorkingKey.fContents ).fShaderID = id;
}

static uint8_t &
WithDynamicByte( uint8_t states[], uint8_t value, uint8_t & bit )
{
	uint8_t byteIndex = value / 8U;

	bit = 1U << (value - byteIndex * 8U);

	return states[byteIndex];
}

static void
SetDynamicStateBit( uint8_t states[], uint8_t value )
{
	uint8_t bit, & byte = WithDynamicByte( states, value, bit );

	byte |= bit;
}

static bool
IsDynamicBitSet( uint8_t states[], uint8_t value )
{
	uint8_t bit, & byte = WithDynamicByte( states, value, bit );

	return !!(byte & bit);
}

VkPipeline
VulkanRenderer::ResolvePipeline()
{
	auto iter = fBuiltPipelines.find( fWorkingKey );
	VkPipeline pipeline = VK_NULL_HANDLE;

	if (iter == fBuiltPipelines.end())
	{
		PackedPipeline & packedPipeline = GetPackedPipeline( fWorkingKey.fContents );

		std::vector< VkDynamicState > dynamicStates;

		for (uint8_t i = 0; i < kFinalDynamicState; ++i)
		{
			if (IsDynamicBitSet( packedPipeline.fDynamicStates, i ))
			{
				dynamicStates.push_back( VkDynamicState( i ) );
			}
		}

		VkPipelineDynamicStateCreateInfo createDynamicStateInfo = {};

		createDynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		createDynamicStateInfo.dynamicStateCount = dynamicStates.size();
		createDynamicStateInfo.pDynamicStates = dynamicStates.data();

		VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};

        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.pVertexAttributeDescriptions = fPipelineCreateInfo.fVertexAttributeDescriptions.data();
        vertexInputInfo.pVertexBindingDescriptions = fPipelineCreateInfo.fVertexBindingDescriptions.data();
        vertexInputInfo.vertexAttributeDescriptionCount = fPipelineCreateInfo.fVertexAttributeDescriptions.size();
        vertexInputInfo.vertexBindingDescriptionCount = fPipelineCreateInfo.fVertexBindingDescriptions.size();

		VkPipelineViewportStateCreateInfo viewportInfo = {};

		viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportInfo.scissorCount = 1U;
		viewportInfo.viewportCount = 1U;

		fPipelineCreateInfo.fColorBlend.attachmentCount = fPipelineCreateInfo.fColorBlendAttachments.size();
		fPipelineCreateInfo.fColorBlend.pAttachments = fPipelineCreateInfo.fColorBlendAttachments.data();

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};

        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCreateInfo.basePipelineHandle = fFirstPipeline;
		pipelineCreateInfo.basePipelineIndex = -1;
		pipelineCreateInfo.flags = fFirstPipeline != VK_NULL_HANDLE ? VK_PIPELINE_CREATE_DERIVATIVE_BIT : VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
		pipelineCreateInfo.layout = fPipelineLayout;
        pipelineCreateInfo.pColorBlendState = &fPipelineCreateInfo.fColorBlend;
        pipelineCreateInfo.pDepthStencilState = &fPipelineCreateInfo.fDepthStencil;
		pipelineCreateInfo.pDynamicState = &createDynamicStateInfo;
        pipelineCreateInfo.pInputAssemblyState = &fPipelineCreateInfo.fInputAssembly;
        pipelineCreateInfo.pMultisampleState = &fPipelineCreateInfo.fMultisample;
        pipelineCreateInfo.pRasterizationState = &fPipelineCreateInfo.fRasterization;
        pipelineCreateInfo.pStages = fPipelineCreateInfo.fShaderStages.data();
        pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
		pipelineCreateInfo.pViewportState = &viewportInfo;
		pipelineCreateInfo.renderPass = fPipelineCreateInfo.fRenderPass;
        pipelineCreateInfo.stageCount = fPipelineCreateInfo.fShaderStages.size();

		auto ci = fState->GetCommonInfo();
// TODO: can we use another queue to actually build this while still recording commands?
        if (VK_SUCCESS == vkCreateGraphicsPipelines( ci.device, fState->GetPipelineCache(), 1U, &pipelineCreateInfo, ci.allocator, &pipeline ))
		{
			if (VK_NULL_HANDLE == fFirstPipeline)
			{
				fFirstPipeline = pipeline;
			}

			fBuiltPipelines[fWorkingKey] = pipeline;
		}

		else
		{
			CoronaLog( "Failed to create pipeline!" );
        }
	}

	else
	{
		pipeline = iter->second;
	}

	return pipeline;
}

void
VulkanRenderer::ResetPipelineInfo()
{
	fWorkingKey = fDefaultKey;

	RestartWorkingPipeline();
}

GPUResource* 
VulkanRenderer::Create( const CPUResource* resource )
{
	switch( resource->GetType() )
	{
		case CPUResource::kFrameBufferObject: return new VulkanFrameBufferObject( *this );
		case CPUResource::kGeometry: return new VulkanGeometry( fState );
		case CPUResource::kProgram: return new VulkanProgram( fState );
		case CPUResource::kTexture: return new VulkanTexture( fState );
		case CPUResource::kUniform: return NULL;
		default: Rtt_ASSERT_NOT_REACHED(); return NULL;
	}
}

void
VulkanRenderer::InitializePipelineState()
{
/*
	glDisable( GL_SCISSOR_TEST );
	// based on framebuffers, looks like we do NOT want this, but rather a full-screen scissor as default
*/
	PackedPipeline packedPipeline = {};

	// TODO: this should be fleshed out with defaults
	// these need not be relevant, but should properly handle being manually updated...

	packedPipeline.fRasterSamplesFlags = VK_SAMPLE_COUNT_1_BIT;
	packedPipeline.fBlendAttachmentCount = 1U;
	packedPipeline.fBlendAttachments[0].fEnable = VK_TRUE;
	packedPipeline.fBlendAttachments[0].fColorWriteMask = 0xF;
	packedPipeline.fBlendAttachments[0].fSrcColorFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	packedPipeline.fBlendAttachments[0].fSrcAlphaFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	packedPipeline.fBlendAttachments[0].fDstColorFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	packedPipeline.fBlendAttachments[0].fDstAlphaFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

	SetDynamicStateBit( packedPipeline.fDynamicStates, VK_DYNAMIC_STATE_SCISSOR );
	SetDynamicStateBit( packedPipeline.fDynamicStates, VK_DYNAMIC_STATE_VIEWPORT );

	memcpy( fDefaultKey.fContents.data(), &packedPipeline, sizeof( PackedPipeline ) );

	ResetPipelineInfo();

	fColorBlendState = fPipelineCreateInfo.fColorBlendAttachments[0];
}

void
VulkanRenderer::RestartWorkingPipeline()
{
    new (&fPipelineCreateInfo) PipelineCreateInfo;
}

void
VulkanRenderer::WipeDescriptors()
{
	auto ci = GetState()->GetCommonInfo();

	for (Descriptor * desc : fDescriptors)
	{
		desc->Wipe( ci.device, ci.allocator );

		Rtt_DELETE( desc );
	}

	fDescriptors.clear();
}

VulkanRenderer::PipelineCreateInfo::PipelineCreateInfo()
{
	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

	fInputAssembly = inputAssembly;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};

	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.lineWidth = 1.0f;

	fRasterization = rasterizer;

	VkPipelineMultisampleStateCreateInfo multisampling = {};

	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VkSampleCountFlagBits( VK_SAMPLE_COUNT_1_BIT );

    fMultisample = multisampling;

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};

	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

	fDepthStencil = depthStencil;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};

	colorBlendAttachment.srcAlphaBlendFactor = colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstAlphaBlendFactor = colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	fColorBlendAttachments.clear();
	fColorBlendAttachments.push_back( colorBlendAttachment );

	VkPipelineColorBlendStateCreateInfo colorBlending = {};

	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	fColorBlend = colorBlending;

	fRenderPass = VK_NULL_HANDLE;
}

const size_t kByteCountRoundedUp = (sizeof( PackedPipeline ) + 7U) & ~7U;
const size_t kU64Count = kByteCountRoundedUp / 8U;

VulkanRenderer::PipelineKey::PipelineKey()
:   fContents( kU64Count, U64{} )
{
}

bool
VulkanRenderer::PipelineKey::operator < ( const PipelineKey & other ) const
{
	return fContents < other.fContents;
}

bool
VulkanRenderer::PipelineKey::operator == ( const PipelineKey & other ) const
{
	return fContents == other.fContents;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
