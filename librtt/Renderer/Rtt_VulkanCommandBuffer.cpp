//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_VulkanState.h"
#include "Renderer/Rtt_VulkanCommandBuffer.h"

#include "Renderer/Rtt_FrameBufferObject.h"/*
#include "Renderer/Rtt_Geometry_Renderer.h"*/
#include "Renderer/Rtt_VulkanFrameBufferObject.h"
#include "Renderer/Rtt_VulkanGeometry.h"
#include "Renderer/Rtt_VulkanProgram.h"
#include "Renderer/Rtt_VulkanTexture.h"
#include "Renderer/Rtt_VulkanRenderer.h"
/*
#include "Renderer/Rtt_Program.h"/*
#include "Renderer/Rtt_Texture.h"
#include "Renderer/Rtt_Uniform.h"*/
#include "Display/Rtt_ShaderResource.h"/*
#include "Core/Rtt_Config.h"
#include "Core/Rtt_Allocator.h"
#include "Core/Rtt_Assert.h"
#include "Core/Rtt_Math.h"
#include <cstdio>
#include <string.h>
#include "Core/Rtt_String.h"
*/
#include <limits>

#include "CoronaLog.h"

// ----------------------------------------------------------------------------

namespace /*anonymous*/
{
/*
	// To ease reading/writing of arrays
	struct Vec2 { Rtt::Real data[2]; };
	struct Vec3 { Rtt::Real data[3]; };
	struct Vec4 { Rtt::Real data[4]; };
	struct Mat3 { Rtt::Real data[9]; };
	struct Mat4 { Rtt::Real data[16]; };

	// NOT USED: const Rtt::Real kNanosecondsToMilliseconds = 1.0f / 1000000.0f;
	const U32 kTimerQueryCount = 3;
	*/
	// The Uniform timestamp counter must be the same for both the
	// front and back CommandBuffers, though only one CommandBuffer
	// will ever write the timestamp on any given frame. If it were
	// ever the case that more than two CommandBuffers were used,
	// this would need to be made a shared member variable.
	static U32 gUniformTimestamp = 0;
	/*
	// Extract location and data from buffer
	#define READ_UNIFORM_DATA( Type ) \
		GLint location = Read<GLint>(); \
		Type value = Read<Type>();

	// Extract data but query for location
	#define READ_UNIFORM_DATA_WITH_PROGRAM( Type ) \
				Rtt::GLProgram* program = Read<Rtt::GLProgram*>(); \
				U32 index = Read<U32>(); \
				GLint location = program->GetUniformLocation( index, fCurrentDrawVersion ); \
				Type value = Read<Type>();
	
	#define CHECK_ERROR_AND_BREAK GL_CHECK_ERROR(); break;

	// Used to validate that the appropriate OpenGL commands
	// are being generated and that their arguments are correct
	#if ENABLE_DEBUG_PRINT 
		#define DEBUG_PRINT( ... ) Rtt_LogException( __VA_ARGS__ ); Rtt_LogException("\n");
		#define DEBUG_PRINT_MATRIX( message, data, count ) \
			Rtt_LogException( "%s\n", message ); \
			Rtt_LogException( "[ %.3f", data[0] ); \
			for( U32 i = 1; i < count; ++i ) \
				Rtt_LogException( ", %.3f", data[i] ); \
			Rtt_LogException ("]\n" );
	#else 
		#define DEBUG_PRINT( ... )
		#define DEBUG_PRINT_MATRIX( message, data, count )
	#endif
*/
}

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

VulkanCommandBuffer::VulkanCommandBuffer( Rtt_Allocator* allocator, VulkanRenderer & renderer )
:	CommandBuffer( allocator ),
	fCurrentPrepVersion( Program::kMaskCount0 ),
	fProgram( NULL ),
	fDefaultFBO( NULL ),
	fTimeTransform( NULL ),/*
	fTimerQueries( new U32[kTimerQueryCount] ),
	fTimerQueryIndex( 0 ),*/
	fElapsedTimeGPU( 0.0f ),
	fRenderer( renderer ),
	fImageAvailableSemaphore( VK_NULL_HANDLE ),
	fRenderFinishedSemaphore( VK_NULL_HANDLE ),
	fInFlight( VK_NULL_HANDLE ),
	fLists( NULL ),
	fCommandBuffer( VK_NULL_HANDLE ),
	fSwapchain( VK_NULL_HANDLE )
{
	for(U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i)
	{
		fUniformUpdates[i].uniform = NULL;
		fUniformUpdates[i].timestamp = 0;
	}

	ClearExecuteResult();

	VulkanState * state = fRenderer.GetState();
	const VkAllocationCallbacks * vulkanAllocator = state->GetAllocator();
	VkDevice device = state->GetDevice();
	VkFenceCreateInfo createFenceInfo = {};

    createFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    createFenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	vkCreateFence( device, &createFenceInfo, vulkanAllocator, &fInFlight );

	VkSemaphoreCreateInfo createSemaphoreInfo = {};

	createSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	vkCreateSemaphore( device, &createSemaphoreInfo, vulkanAllocator, &fImageAvailableSemaphore );
	vkCreateSemaphore( device, &createSemaphoreInfo, vulkanAllocator, &fRenderFinishedSemaphore );

	if ( VK_NULL_HANDLE == fInFlight || VK_NULL_HANDLE == fImageAvailableSemaphore || VK_NULL_HANDLE == fRenderFinishedSemaphore)
	{
		CoronaLog( "Failed to create some synchronziation objects!" );

		vkDestroySemaphore( device, fImageAvailableSemaphore, vulkanAllocator );
		vkDestroySemaphore( device, fRenderFinishedSemaphore, vulkanAllocator );
		vkDestroyFence( device, fInFlight, vulkanAllocator );

		fImageAvailableSemaphore = VK_NULL_HANDLE;
		fRenderFinishedSemaphore = VK_NULL_HANDLE;
		fInFlight = VK_NULL_HANDLE;
	}
}

VulkanCommandBuffer::~VulkanCommandBuffer()
{
	VulkanState * state = fRenderer.GetState();
	const VkAllocationCallbacks * allocator = state->GetAllocator();
	VkDevice device = state->GetDevice();

	vkDestroyFence( device, fInFlight, allocator );
	vkDestroySemaphore( device, fImageAvailableSemaphore, allocator );
	vkDestroySemaphore( device, fRenderFinishedSemaphore, allocator );
//	delete [] fTimerQueries;
}

void
VulkanCommandBuffer::Initialize()
{
#ifdef ENABLE_GPU_TIMER_QUERIES
	// Used to measure GPU execution time
	glGenQueries( kTimerQueryCount, fTimerQueries );
	for( U32 i = 0; i < kTimerQueryCount; ++i)
	{
		glBeginQuery( GL_TIME_ELAPSED, fTimerQueries[i] );
		glEndQuery( GL_TIME_ELAPSED );
	}
	GL_CHECK_ERROR();
#endif
	InitializeFBO();
	InitializeCachedParams();
	CacheQueryParam( kMaxTextureSize );
	
	GetMaxTextureSize();

}
void
VulkanCommandBuffer::InitializeFBO()
{
   // Some platforms render to an FBO by default
	//glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fDefaultFBO ); 
	/*
	GLint curFBO = 0;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, & curFBO );
	fDefaultFBO = curFBO;
	Rtt_STATIC_ASSERT( sizeof( curFBO ) == sizeof( fDefaultFBO ) );
	*/
}

void 
VulkanCommandBuffer::InitializeCachedParams()
{
	for (int i = 0; i < kNumQueryableParams; i++)
	{
		fCachedQuery[i] = -1;
	}
}

void 
VulkanCommandBuffer::CacheQueryParam( CommandBuffer::QueryableParams param )
{
	const VkPhysicalDeviceProperties & properties = fRenderer.GetState()->GetProperties();

	if (CommandBuffer::kMaxTextureSize == param)
	{
		fCachedQuery[param] = S32( properties.limits.maxImageDimension2D );
	}
}

void 
VulkanCommandBuffer::Denitialize()
{
#ifdef ENABLE_GPU_TIMER_QUERIES
//	glDeleteQueries( kTimerQueryCount, fTimerQueries );
#endif
}

void
VulkanCommandBuffer::ClearUserUniforms()
{
	fUniformUpdates[Uniform::kMaskMatrix0].uniform = NULL;
	fUniformUpdates[Uniform::kMaskMatrix1].uniform = NULL;
	fUniformUpdates[Uniform::kMaskMatrix2].uniform = NULL;
	fUniformUpdates[Uniform::kUserData0].uniform = NULL;
	fUniformUpdates[Uniform::kUserData1].uniform = NULL;
	fUniformUpdates[Uniform::kUserData2].uniform = NULL;
	fUniformUpdates[Uniform::kUserData3].uniform = NULL;
}

void
VulkanCommandBuffer::BindFrameBufferObject( FrameBufferObject* fbo )
{
	if( fbo )
	{
		VulkanFrameBufferObject * vulkanFBO = static_cast< VulkanFrameBufferObject * >( fbo->GetGPUResource() );

		vulkanFBO->Bind();	// TODO: stuff to plug in to render pass begin...
	}
	else
	{
/*
glBindFramebuffer( GL_FRAMEBUFFER, fDefaultFBO );
DEBUG_PRINT( "Unbind FrameBufferObject: OpenGL name: %i (fDefaultFBO)", fDefaultFBO );
CHECK_ERROR_AND_BREAK;
*/
	}
}

void 
VulkanCommandBuffer::BindGeometry( Geometry* geometry )
{
	VulkanGeometry * vulkanGeometry = static_cast< VulkanGeometry * >( geometry->GetGPUResource() );
	VulkanGeometry::Binding binding = vulkanGeometry->Bind();

	fRenderer.SetBindingDescriptions( binding.fID, binding.fDescriptions );

	VkDeviceSize offset = 0U;

	vkCmdBindVertexBuffers( fCommandBuffer, 0U, 1U, &binding.fVertexBuffer, &offset );

	if (binding.fIndexBuffer != VK_NULL_HANDLE)
	{
		vkCmdBindIndexBuffer( fCommandBuffer, binding.fIndexBuffer, 0U, binding.fIndexType );
	}
}

void 
VulkanCommandBuffer::BindTexture( Texture* texture, U32 unit )
{
	VulkanTexture * vulkanTexture = static_cast< VulkanTexture * >( texture->GetGPUResource() );
	VulkanTexture::Binding binding = vulkanTexture->Bind();
	VkDescriptorImageInfo imageInfo;

	imageInfo.imageView = binding.view;
	imageInfo.sampler = binding.sampler;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet descriptorWrite = {};

	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.descriptorCount = 1U;
	descriptorWrite.pImageInfo = &imageInfo;

	if (true) // TODO: no indexing... this will presumably occur before binding
	{
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite.dstArrayElement = unit;
		//	descriptorWrite.dstSet = 2U;
		
		vkUpdateDescriptorSets( fRenderer.GetState()->GetDevice(), 1U, &descriptorWrite, 0U, NULL );
	}

	else // TODO: do one bind at start of frame, update here?
	{
		VkWriteDescriptorSet write2 = descriptorWrite;

		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		write2.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
//		write2.dstArrayElement = unit + offset;
		write2.dstBinding = 1U;
//		write2.dstSet = set;
		write2.pImageInfo = &imageInfo; // TODO: is this right?? (also for write 0)

		VkWriteDescriptorSet writes[] = { descriptorWrite, write2 };

		vkUpdateDescriptorSets( fRenderer.GetState()->GetDevice(), 2U, writes, 0U, NULL );
	}
}

void 
VulkanCommandBuffer::BindProgram( Program* program, Program::Version version )
{
	VulkanProgram * vulkanProgram = static_cast< VulkanProgram * >( program->GetGPUResource() );
	VulkanProgram::PipelineStages stageData = vulkanProgram->Bind( version );

	fRenderer.SetShaderStages( stageData.fID, stageData.fStages );

	fProgram = program;
	fCurrentPrepVersion = version;

	fTimeTransform = program->GetShaderResource()->GetTimeTransform();
}

void
VulkanCommandBuffer::BindUniform( Uniform* uniform, U32 unit )
{
	Rtt_ASSERT( unit < Uniform::kNumBuiltInVariables );

	UniformUpdate& update = fUniformUpdates[ unit ];
	update.uniform = uniform;
	update.timestamp = gUniformTimestamp++;
}

void
VulkanCommandBuffer::SetBlendEnabled( bool enabled )
{
	fRenderer.EnableBlend( enabled );
}

static VkBlendFactor
VulkanFactorForBlendParam( BlendMode::Param param )
{
	VkBlendFactor result = VK_BLEND_FACTOR_SRC_ALPHA;

	switch ( param )
	{
		case BlendMode::kZero:
			result = VK_BLEND_FACTOR_ZERO;
			break;
		case BlendMode::kOne:
			result = VK_BLEND_FACTOR_ONE;
			break;
		case BlendMode::kSrcColor:
			result = VK_BLEND_FACTOR_SRC_COLOR;
			break;
		case BlendMode::kOneMinusSrcColor:
			result = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
			break;
		case BlendMode::kDstColor:
			result = VK_BLEND_FACTOR_DST_COLOR;
			break;
		case BlendMode::kOneMinusDstColor:
			result = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
			break;
		case BlendMode::kSrcAlpha:
			result = VK_BLEND_FACTOR_SRC_ALPHA;
			break;
		case BlendMode::kOneMinusSrcAlpha:
			result = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			break;
		case BlendMode::kDstAlpha:
			result = VK_BLEND_FACTOR_DST_ALPHA;
			break;
		case BlendMode::kOneMinusDstAlpha:
			result = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
			break;
		case BlendMode::kSrcAlphaSaturate:
			result = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
			break;
		default:
			Rtt_ASSERT_NOT_REACHED();
			break;
	}

	return result;
}

void
VulkanCommandBuffer::SetBlendFunction( const BlendMode& mode )
{
	VkBlendFactor srcColor = VulkanFactorForBlendParam( mode.fSrcColor );
	VkBlendFactor dstColor = VulkanFactorForBlendParam( mode.fDstColor );

	VkBlendFactor srcAlpha = VulkanFactorForBlendParam( mode.fSrcAlpha );
	VkBlendFactor dstAlpha = VulkanFactorForBlendParam( mode.fDstAlpha );

	fRenderer.SetBlendFactors( srcColor, srcAlpha, dstColor, dstAlpha );
}

void 
VulkanCommandBuffer::SetBlendEquation( RenderTypes::BlendEquation mode )
{
	VkBlendOp equation = VK_BLEND_OP_ADD;

	switch( mode )
	{
		case RenderTypes::kSubtractEquation:
			equation = VK_BLEND_OP_SUBTRACT;
			break;
		case RenderTypes::kReverseSubtractEquation:
			equation = VK_BLEND_OP_REVERSE_SUBTRACT;
			break;
		default:
			break;
	}

	fRenderer.SetBlendEquations( equation, equation );
}

void
VulkanCommandBuffer::SetViewport( int x, int y, int width, int height )
{
	if (fCommandBuffer != VK_NULL_HANDLE)
	{
		VkViewport viewport;

		viewport.x = x;
		viewport.y = y;
		viewport.width = width;
		viewport.height = height;
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;

		vkCmdSetViewport( fCommandBuffer, 0U, 1U, &viewport );
	}
}

void 
VulkanCommandBuffer::SetScissorEnabled( bool enabled )
{
	// No-op: always want scissor, possibly fullscreen
}

void 
VulkanCommandBuffer::SetScissorRegion( int x, int y, int width, int height )
{
	// TODO? seems to be dead code
}

void
VulkanCommandBuffer::SetMultisampleEnabled( bool enabled )
{
// TODO: some flag to ignore these settings...
//	WRITE_COMMAND( enabled ? kCommandEnableMultisample : kCommandDisableMultisample );
//	fMultisampleStateCreateInfo.rasterizationSamples
/*
			case kCommandEnableMultisample:
			{
				Rtt_glEnableMultisample();
				DEBUG_PRINT( "Enable multisample test" );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandDisableMultisample:
			{
				Rtt_glDisableMultisample();
				DEBUG_PRINT( "Disable multisample test" );
				CHECK_ERROR_AND_BREAK;
			}
*/
}

void 
VulkanCommandBuffer::Clear( Real r, Real g, Real b, Real a )
{
	VkClearValue clearValue;

	// TODO: allow this to accommodate float targets?

	clearValue.color.uint32[0] = uint32_t( 255. * r );
	clearValue.color.uint32[1] = uint32_t( 255. * g );
	clearValue.color.uint32[2] = uint32_t( 255. * b );
	clearValue.color.uint32[3] = uint32_t( 255. * a );

	fRenderer.SetClearValue( 0U, clearValue );
}

void 
VulkanCommandBuffer::Draw( U32 offset, U32 count, Geometry::PrimitiveType type )
{
	if (fCommandBuffer != VK_NULL_HANDLE)
	{
		VkPrimitiveTopology topology;

		switch( type )
		{
			case Geometry::kTriangleStrip:
				topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			
				break;
			case Geometry::kTriangleFan:
				topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
			
				break;
			case Geometry::kTriangles:
				topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			
				break;
			case Geometry::kLines:
				topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

				break;
			case Geometry::kLineLoop:
				topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			
				break;
			default: Rtt_ASSERT_NOT_REACHED(); break;
		}

		PrepareDraw( topology );

		vkCmdDraw( fCommandBuffer, count, 1U, offset, 0U );
	}
}

void 
VulkanCommandBuffer::DrawIndexed( U32, U32 count, Geometry::PrimitiveType type )
{
	if (fCommandBuffer != VK_NULL_HANDLE)
	{
		VkPrimitiveTopology topology;

		switch( type )
		{
			case Geometry::kIndexedTriangles:
				topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

				break;
			default: Rtt_ASSERT_NOT_REACHED(); break;
		}

		PrepareDraw( topology );

		// The first argument, offset, is currently unused. If support for non-
		// VBO based indexed rendering is added later, an offset may be needed.

		vkCmdDrawIndexed( fCommandBuffer, count, 1U, 0U, 0U, 0U );
	}
}

S32
VulkanCommandBuffer::GetCachedParam( CommandBuffer::QueryableParams param )
{
	S32 result = -1;

	if (param < kNumQueryableParams)
	{
		result = fCachedQuery[param];
	}

	Rtt_ASSERT_MSG(result != -1, "Parameter not cached");
	
	return result;
}

Real 
VulkanCommandBuffer::Execute( bool measureGPU )
{
//	DEBUG_PRINT( "--Begin Rendering: VulkanCommandBuffer --" );

	InitializeFBO();
/*
#ifdef ENABLE_GPU_TIMER_QUERIES
	if( measureGPU )
	{
		GLint available = 0;
		GLint id = fTimerQueries[fTimerQueryIndex];
		while( !available )
		{
			glGetQueryObjectiv( id, GL_QUERY_RESULT_AVAILABLE, &available);
		}

		GLuint64 result = 0;
		glGetQueryObjectui64vEXT( id, GL_QUERY_RESULT, &result );
		fElapsedTimeGPU = result * kNanosecondsToMilliseconds;

		glBeginQuery( GL_TIME_ELAPSED, id);
		fTimerQueryIndex = ( fTimerQueryIndex + 1 ) % kTimerQueryCount;
	}

	if( measureGPU )
	{
		glEndQuery( GL_TIME_ELAPSED );
	}
#endif
*/
//	DEBUG_PRINT( "--End Rendering: VulkanCommandBuffer --\n" );

	if (fCommandBuffer != VK_NULL_HANDLE && fSwapchain != VK_NULL_HANDLE)
	{
		VkResult endResult = vkEndCommandBuffer( fCommandBuffer );

		if (VK_SUCCESS == endResult)
		{
			VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			VkSubmitInfo submitInfo = {};

			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1U;
			submitInfo.pCommandBuffers = &fCommandBuffer;
			submitInfo.pSignalSemaphores = &fRenderFinishedSemaphore;
			submitInfo.pWaitDstStageMask = &waitStage;
			submitInfo.pWaitSemaphores = &fImageAvailableSemaphore;
			submitInfo.signalSemaphoreCount = 1U;
			submitInfo.waitSemaphoreCount = 1U;

			const VulkanState * state = fRenderer.GetState();

			vkResetFences( state->GetDevice(), 1U, &fInFlight );

			VkResult submitResult = vkQueueSubmit( state->GetGraphicsQueue(), 1, &submitInfo, fInFlight );

			if (VK_SUCCESS == submitResult)
			{
				VkPresentInfoKHR presentInfo = {};

				presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
				presentInfo.pImageIndices = &fImageIndex;
				presentInfo.pSwapchains = &fSwapchain;
				presentInfo.pWaitSemaphores = &fRenderFinishedSemaphore;
				presentInfo.swapchainCount = 1U;
				presentInfo.waitSemaphoreCount = 1U;

				VkResult presentResult = vkQueuePresentKHR( state->GetPresentQueue(), &presentInfo );

				if (VK_ERROR_OUT_OF_DATE_KHR == presentResult || VK_SUBOPTIMAL_KHR == presentResult) // || framebufferResized)
				{
				//    framebufferResized = false;
				}

				else if (presentResult != VK_SUCCESS)
				{
					CoronaLog( "Failed to present swap chain image!" );
				}

				fExecuteResult = presentResult;
			}

			else
			{
				CoronaLog( "Failed to submit draw command buffer!" );

				fExecuteResult = submitResult;
			}
		}

		else
		{
			CoronaLog( "Failed to record command buffer!" );

			fExecuteResult = endResult;
		}
	}

	else
	{
		fExecuteResult = VK_ERROR_INITIALIZATION_FAILED;
	}

	fCommandBuffer = VK_NULL_HANDLE;
	fSwapchain = VK_NULL_HANDLE;

	return fElapsedTimeGPU;
}
/*
    // begin

    VkRenderPassBeginInfo renderPassInfo = {};

    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.clearValueCount = fClearValues.size();
    renderPassInfo.framebuffer = swapChainFramebuffers[i];
    renderPassInfo.pClearValues = fClearValues.data();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;
    renderPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	^^ FBO?

		geometry
        draw
			descriptors
			pipeline

    vkCmdEndRenderPass(commandBuffers[i]);

	^^ /FBO?

	// end
*/
VkResult VulkanCommandBuffer::WaitAndAcquire( VkDevice device, VkSwapchainKHR swapchain )
{
	if (fInFlight != VK_NULL_HANDLE)
	{
		vkWaitForFences( device, 1U, &fInFlight, VK_TRUE, std::numeric_limits< uint64_t >::max() );

		fSwapchain = swapchain;

		return vkAcquireNextImageKHR( device, swapchain, std::numeric_limits< uint64_t >::max(), fImageAvailableSemaphore, VK_NULL_HANDLE, &fImageIndex );
	}

	else
	{
		return VK_ERROR_INITIALIZATION_FAILED;
	}
}

void VulkanCommandBuffer::BeginRecording( VkCommandBuffer commandBuffer, DescriptorLists * lists )
{
	if (commandBuffer != VK_NULL_HANDLE && lists)
	{
		VkDevice device = fRenderer.GetState()->GetDevice();

		// lists = ..., ubo, user data, textures, ...

		lists[0].Reset( device );
		lists[1].Reset( device );
		lists[2].Reset( device );
		
		VkCommandBufferBeginInfo beginInfo = {};

		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		if (VK_SUCCESS == vkBeginCommandBuffer( commandBuffer, &beginInfo ))
		{
			fCommandBuffer = commandBuffer;
			fLists = lists;
		}

		else
		{
			CoronaLog( "Failed to begin recording command buffer!" );
		}
	}
}

void VulkanCommandBuffer::PrepareDraw( VkPrimitiveTopology topology )
{
	fRenderer.SetPrimitiveTopology( topology );

	Rtt_ASSERT( fProgram && fProgram->GetGPUResource() );
	
	VulkanPushConstants pushConstants;
	DrawState drawState = ApplyUniforms( fProgram->GetGPUResource(), pushConstants );
	VkDevice device = fRenderer.GetState()->GetDevice();
	uint32_t dynamicOffsets[2], count = 0U;
	VkDescriptorSet sets[3] = {};

	if (!drawState.uniformBufferRanges.empty())
	{
		vkFlushMappedMemoryRanges( device, drawState.uniformBufferRanges.size(), drawState.uniformBufferRanges.data() );

		DescriptorLists & lists = fLists[0];
//		sets[0] = ;//lists.fBufferData[lists.fBufferIndex]
		dynamicOffsets[count++] = lists.fOffset;

		lists.fOffset += lists.fDynamicAlignment;
	}

	if (!drawState.userDataRanges.empty())
	{
		vkFlushMappedMemoryRanges( device, drawState.userDataRanges.size(), drawState.userDataRanges.data() );
		
		DescriptorLists & lists = fLists[1];
//		sets[1] = &fLists[1];
		dynamicOffsets[count] = lists.fOffset;

		lists.fOffset += lists.fDynamicAlignment;
	}

	// texture set...

	if (count || false)
	{
		vkCmdBindDescriptorSets( fCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, fRenderer.GetPipelineLayout(), 0U, 3U, sets, count, dynamicOffsets );
	}

	if (drawState.upperPushConstantOffset >= 0)
	{
		uint32_t offset = uint32_t( drawState.lowerPushConstantOffset );
		uint32_t size = uint32_t( drawState.upperPushConstantOffset ) - offset + sizeof( float ) * 4U;

		vkCmdPushConstants( fCommandBuffer, fRenderer.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, offset, size, &pushConstants.fData + offset );
	}
}

void VulkanCommandBuffer::AddGraphicsPipeline( VkPipeline pipeline )
{
	if (fCommandBuffer != VK_NULL_HANDLE)
	{
		vkCmdBindPipeline( fCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	}
}

VulkanCommandBuffer::DrawState VulkanCommandBuffer::ApplyUniforms( GPUResource* resource, VulkanPushConstants & pushConstants )
{
	Real rawTotalTime;
	bool transformed = false;

	if (fTimeTransform)
	{
		const UniformUpdate& time = fUniformUpdates[Uniform::kTotalTime];
		if (time.uniform)
		{
			transformed = fTimeTransform->Apply( time.uniform, &rawTotalTime, time.timestamp );
		}
	}
	
	VulkanProgram* vulkanProgram = static_cast< VulkanProgram * >( resource );
	DrawState drawState;

	for( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i)
	{
		const UniformUpdate& update = fUniformUpdates[i];
		if( update.uniform && update.timestamp != vulkanProgram->GetUniformTimestamp( i, fCurrentPrepVersion ) )
		{		
			ApplyUniform( *vulkanProgram, i, drawState, pushConstants );
		}
	}

	if (transformed)
	{
		fUniformUpdates[Uniform::kTotalTime].uniform->SetValue(rawTotalTime);
	}

	return drawState;
}

void VulkanCommandBuffer::ApplyUniform( VulkanProgram & vulkanProgram, U32 index, DrawState & drawState, VulkanPushConstants & pushConstants )
{
	const UniformUpdate& update = fUniformUpdates[index];
	vulkanProgram.SetUniformTimestamp( index, fCurrentPrepVersion, update.timestamp );

	VulkanProgram::Location location = vulkanProgram.GetUniformLocation( Uniform::kViewProjectionMatrix + index, fCurrentPrepVersion );
	Uniform* uniform = update.uniform;

	bool isUniformUserData = index >= Uniform::kUserData0;
	bool isPushConstant = (index >= Uniform::kMaskMatrix0 && index <= Uniform::kMaskMatrix2) || Uniform::kTotalTime == index;
	S32 low, nrows = 1;

	size_t offset;
	float * dst = NULL;
	VkMappedMemoryRange range = {};

	if (isUniformUserData)
	{
		DescriptorLists & lists = fLists[1];
		DynamicUniformData & data = lists.fBufferData[lists.fBufferIndex];

		range.memory = data.fBuffer;

		void * mapped = static_cast< U8 * >( data.fMapped ) + lists.fOffset;

		dst = reinterpret_cast< VulkanUserDataUBO * >( mapped )->UserData[index - Uniform::kUserData0];
	}

	else if (!isPushConstant)
	{
		DescriptorLists & lists = fLists[0];
		DynamicUniformData & data = lists.fBufferData[lists.fBufferIndex];

		range.memory = data.fBuffer;

		void * mapped = static_cast< U8 * >( data.fMapped ) + lists.fOffset;

		dst = reinterpret_cast< VulkanUBO * >( mapped )->fData;
	}

	switch( uniform->GetDataType() )
	{
		case Uniform::kScalar:
			if (isPushConstant)
			{
				dst = pushConstants.fData + location.fOffset;
				low = location.fOffset - location.fOffset % 4;
			}

			uniform->GetValue( dst[0] );

			break;
		case Uniform::kVec2:
			Rtt_ASSERT( isUniformUserData );

			uniform->GetValue( dst[0], dst[1] );
			
			break;
		case Uniform::kVec3: // has difficulties, cf. https://stackoverflow.com/questions/38172696/should-i-ever-use-a-vec3-inside-of-a-uniform-buffer-or-shader-storage-buffer-o
			Rtt_ASSERT( isUniformUserData );

			uniform->GetValue( dst[0], dst[1], dst[2] );

			break;
		case Uniform::kVec4:
			Rtt_ASSERT( !isPushConstant );

			uniform->GetValue( dst[0], dst[1], dst[2], dst[3] );
			
			break;
		case Uniform::kMat3:
			if (isUniformUserData)
			{
				nrows = 3;

				float * src = reinterpret_cast< float * >( uniform->GetData() );

				for (int i = 0; i < 3; ++i)
				{
					memcpy( dst, src, 3U * sizeof( float ) );

					src += 3U * sizeof( float );
					dst += 4U * sizeof( float );
				}
			}
			
			else
			{
				Rtt_ASSERT( isPushConstant );

				VulkanProgram::Location translationLocation = vulkanProgram.GetTranslationLocation( Uniform::kViewProjectionMatrix + index, fCurrentPrepVersion );

				// only used by mask matrices, with three constant components
				// thus, mindful of the difficulties mentioned re. kVec3, these are decomposed as a vec2[2] and vec2
				// the vec2 array avoids consuming two vectors, cf. https://www.khronos.org/opengl/wiki/Layout_Qualifier_(GLSL)
				// the two elements should be columns, to allow mat2(vec[0], vec[1]) on the shader side
				float * src = reinterpret_cast< float * >( uniform->GetData() );


			}
			// WRITE_COMMAND( kCommandApplyUniformMat3 );

			break;
		case Uniform::kMat4:
			Rtt_ASSERT( !isPushConstant );

			if (isUniformUserData)
			{
				nrows = 4;
			}

			memcpy( dst, uniform->GetData(), 16U * sizeof( float ) );
			
			break;
		default:
			Rtt_ASSERT_NOT_REACHED();
			
			break;
	}

	if (isPushConstant)
	{
		S32 high = low + nrows - 1;

		if (low < drawState.lowerPushConstantOffset)
		{
			drawState.lowerPushConstantOffset = low;
		}

		if (high > drawState.upperPushConstantOffset)
		{
			drawState.upperPushConstantOffset = high;
		}
	}
}

VulkanCommandBuffer::DrawState::DrawState()
:	lowerPushConstantOffset( 0 ),
	upperPushConstantOffset( -1 )
{
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
