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

/*
#include "Renderer/Rtt_FrameBufferObject.h"
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
	fCurrentDrawVersion( Program::kMaskCount0 ),/*
	fProgram( NULL ),
	fDefaultFBO( 0 ),*/
	fTimeTransform( NULL ),/*
	fTimerQueries( new U32[kTimerQueryCount] ),
	fTimerQueryIndex( 0 ),*/
	fElapsedTimeGPU( 0.0f ),
	fRenderer( renderer ),
	fInFlight( VK_NULL_HANDLE ),
	fImageAvailableSemaphore( VK_NULL_HANDLE ),
	fRenderFinishedSemaphore( VK_NULL_HANDLE ),
	fDescriptorPoolList( NULL ),
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

		vkDestroyFence( device, fInFlight, vulkanAllocator );
		vkDestroySemaphore( device, fImageAvailableSemaphore, vulkanAllocator );
		vkDestroySemaphore( device, fRenderFinishedSemaphore, vulkanAllocator );

		fInFlight = VK_NULL_HANDLE;
		fImageAvailableSemaphore = VK_NULL_HANDLE;
		fRenderFinishedSemaphore = VK_NULL_HANDLE;
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
	//CacheQueryParam(kMaxTextureSize);
	
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
/*
	GLenum glQueryParam = GL_MAX_TEXTURE_SIZE;
	switch (param)
	{
		case CommandBuffer::kMaxTextureSize:
			glQueryParam = GL_MAX_TEXTURE_SIZE;
			break;
		default:
			break;
	}
	
	GLint retVal = -1;
	glGetIntegerv( glQueryParam, &retVal );
	fCachedQuery[param] = retVal;
	
	GL_CHECK_ERROR();
*/
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
	//	WRITE_COMMAND( kCommandBindFrameBufferObject );
	//	Write<GPUResource*>( fbo->GetGPUResource() );
	}
	else
	{
	//	WRITE_COMMAND( kCommandUnBindFrameBufferObject );
	}
}

void 
VulkanCommandBuffer::BindGeometry( Geometry* geometry )
{
	VulkanGeometry * vulkanGeometry = static_cast< VulkanGeometry * >( geometry->GetGPUResource() );
	VulkanGeometry::VertexDescription vertexData = vulkanGeometry->Bind();

	fRenderer.SetBindingDescriptions( vertexData.fID, vertexData.fDescriptions );
//	WRITE_COMMAND( kCommandBindGeometry );
//	Write<GPUResource*>( geometry->GetGPUResource() );
	
//                vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

//                vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer, 0, VK_INDEX_TYPE_UINT32);
}

void 
VulkanCommandBuffer::BindTexture( Texture* texture, U32 unit )
{
//	WRITE_COMMAND( kCommandBindTexture );
//	Write<U32>( unit );
//	Write<GPUResource*>( texture->GetGPUResource() );
//	vkUpdateDescriptorSets(device, 1, WRITES, 1, 0);
//                vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[i], 0, nullptr);
}

void 
VulkanCommandBuffer::BindProgram( Program* program, Program::Version version )
{
	VulkanProgram * vulkanProgram = static_cast< VulkanProgram * >( program->GetGPUResource() );
	VulkanProgram::PipelineStages stageData = vulkanProgram->Bind( version );

	fRenderer.SetShaderStages( stageData.fID, stageData.fStages );
/*
	WRITE_COMMAND( kCommandBindProgram );
	Write<Program::Version>( version );
	Write<GPUResource*>( program->GetGPUResource() );
	*/
	fCurrentPrepVersion = version;
//	fProgram = program;

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

	fRenderer.SetPrimitiveTopology( topology );
/*
	Rtt_ASSERT( fProgram && fProgram->GetGPUResource() );
	ApplyUniforms( fProgram->GetGPUResource() );
*/
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
	
		fRenderer.SetPrimitiveTopology( topology );
	/*
		// The first argument, offset, is currently unused. If support for non-
		// VBO based indexed rendering is added later, an offset may be needed.

		Rtt_ASSERT( fProgram && fProgram->GetGPUResource() );
		ApplyUniforms( fProgram->GetGPUResource() );
	*/
		vkCmdDrawIndexed( fCommandBuffer, count, 1U, 0U, 0U, 0U );
	}
}

S32
VulkanCommandBuffer::GetCachedParam( CommandBuffer::QueryableParams param )
{
	S32 result = -1;

	if (param < kNumQueryableParams)
	{
//		result = fCachedQuery[param];
	}

	Rtt_ASSERT_MSG(result != -1, "Parameter not cached");
	
	return result;
}

Real 
VulkanCommandBuffer::Execute( bool measureGPU )
{
/*
	DEBUG_PRINT( "--Begin Rendering: GLCommandBuffer --" );

//TODO - make this a property that can be invalidated for specific platforms
//The Mac platform needs to set the default FBO for resize, sleep, zoom functionality
#ifdef Rtt_MAC_ENV
	InitializeFBO();
	//printf("DEFAULTFBO: %d", fDefaultFBO);
#endif

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
#endif

	// Reset the offset pointer to the start of the buffer.
	// This is safe to do here, as preparation work is done
	// on another CommandBuffer while this one is executing.
	fOffset = fBuffer;

	//GL_CHECK_ERROR();

	for( U32 i = 0; i < fNumCommands; ++i )
	{
		Command command = Read<Command>();

// printf( "GLCommandBuffer::Execute [%d/%d] %d\n", i, fNumCommands, command );

		Rtt_ASSERT( command < kNumCommands );
		switch( command )
		{
			case kCommandBindFrameBufferObject:
			{
				GLFrameBufferObject* fbo = Read<GLFrameBufferObject*>();
				fbo->Bind();
				DEBUG_PRINT( "Bind FrameBufferObject: OpenGL name: %i, OpenGL Texture name, if any: %d",
								fbo->GetName(),
								fbo->GetTextureName() );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandUnBindFrameBufferObject:
			{
				glBindFramebuffer( GL_FRAMEBUFFER, fDefaultFBO );
				DEBUG_PRINT( "Unbind FrameBufferObject: OpenGL name: %i (fDefaultFBO)", fDefaultFBO );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandBindGeometry:
			{
				GLGeometry* geometry = Read<GLGeometry*>();
				geometry->Bind();
				DEBUG_PRINT( "Bind Geometry %p", geometry );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandBindTexture:
			{
				U32 unit = Read<U32>();
				GLTexture* texture = Read<GLTexture*>();
				texture->Bind( unit );
				DEBUG_PRINT( "Bind Texture: texture=%p unit=%i OpenGL name=%d",
								texture,
								unit,
								texture->GetName() );
				CHECK_ERROR_AND_BREAK;
			}

			case kCommandApplyUniformScalar:
			{
				READ_UNIFORM_DATA( Real );
				glUniform1f( location, value );
				DEBUG_PRINT( "Set Uniform: value=%f location=%i", value, location );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformVec2:
			{
				READ_UNIFORM_DATA( Vec2 );
				glUniform2fv( location, 1, &value.data[0] );
				DEBUG_PRINT( "Set Uniform: value=(%f, %f) location=%i", value.data[0], value.data[1], location);
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformVec3:
			{
				READ_UNIFORM_DATA( Vec3 );
				glUniform3fv( location, 1, &value.data[0] );
				DEBUG_PRINT( "Set Uniform: value=(%f, %f, %f) location=%i", value.data[0], value.data[1], value.data[2], location);
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformVec4:
			{
				READ_UNIFORM_DATA( Vec4 );
				glUniform4fv( location, 1, &value.data[0] );
				DEBUG_PRINT( "Set Uniform: value=(%f, %f, %f, %f) location=%i", value.data[0], value.data[1], value.data[2], value.data[3], location);
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformMat3:
			{
				READ_UNIFORM_DATA( Mat3 );
				glUniformMatrix3fv( location, 1, GL_FALSE, &value.data[0] );
				DEBUG_PRINT_MATRIX( "Set Uniform: value=", value.data, 9 );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformMat4:
			{
				READ_UNIFORM_DATA( Mat4 );
				glUniformMatrix4fv( location, 1, GL_FALSE, &value.data[0] );
				DEBUG_PRINT_MATRIX( "Set Uniform: value=", value.data, 16 );
				CHECK_ERROR_AND_BREAK;
			}

			default:
				DEBUG_PRINT( "Unknown command(%d)", command );
				Rtt_ASSERT_NOT_REACHED();
				break;
		}
	}

	fBytesUsed = 0;
	fNumCommands = 0;
	
#ifdef ENABLE_GPU_TIMER_QUERIES
	if( measureGPU )
	{
		glEndQuery( GL_TIME_ELAPSED );
	}
#endif
	
	DEBUG_PRINT( "--End Rendering: GLCommandBuffer --\n" );

	GL_CHECK_ERROR();
*/
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
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[i];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;

    std::array<VkClearValue, 2> clearValues = {};
    clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        VkBuffer vertexBuffers[] = {vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

        vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[i], 0, nullptr);

        vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(commandBuffers[i]);

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

void VulkanCommandBuffer::PrepareToExecute( VkCommandBuffer commandBuffer, DescriptorPoolList * descriptorPoolList )
{
	if (commandBuffer != VK_NULL_HANDLE && descriptorPoolList)
	{
		VkDevice device = fRenderer.GetState()->GetDevice();

		for (uint32_t i = 0; i < descriptorPoolList->fPools.size() && i <= descriptorPoolList->fIndex; ++i)
		{
//			vkResetDescriptorPool( device, descriptorPoolList->fPools[i], 0 );
		}

		descriptorPoolList->fIndex = 0U;
		
		VkCommandBufferBeginInfo beginInfo = {};

		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		if (VK_SUCCESS == vkBeginCommandBuffer( commandBuffer, &beginInfo ))
		{
			fCommandBuffer = commandBuffer;
			fDescriptorPoolList = descriptorPoolList;
		}

		else
		{
			CoronaLog( "Failed to begin recording command buffer!" );
		}
	}
}

void VulkanCommandBuffer::AddGraphicsPipeline( VkPipeline pipeline )
{
	if (fCommandBuffer != VK_NULL_HANDLE)
	{
		vkCmdBindPipeline( fCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	}
}

void VulkanCommandBuffer::ApplyUniforms( GPUResource* resource )
{
	VulkanProgram* vulkanProgram = static_cast< VulkanProgram * >(resource);

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

	for( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i)
	{
		const UniformUpdate& update = fUniformUpdates[i];
		if( update.uniform && update.timestamp != vulkanProgram->GetUniformTimestamp( i, fCurrentPrepVersion ) )
		{		
			ApplyUniform( resource, i );
		}
	}

	if (transformed)
	{
		fUniformUpdates[Uniform::kTotalTime].uniform->SetValue(rawTotalTime);
	}
}

void VulkanCommandBuffer::ApplyUniform( GPUResource* resource, U32 index )
{
	const UniformUpdate& update = fUniformUpdates[index];
// write memory OR update push constant(s)...
	/*
	GLProgram* glProgram = static_cast<GLProgram*>( resource );
	glProgram->SetUniformTimestamp( index, fCurrentPrepVersion, update.timestamp );

	GLint location = glProgram->GetUniformLocation( Uniform::kViewProjectionMatrix + index, fCurrentPrepVersion );*/
		// The OpenGL program already exists and actually uses the specified uniform
		Uniform* uniform = update.uniform;
		switch( uniform->GetDataType() )
		{/*
			case Uniform::kScalar:	WRITE_COMMAND( kCommandApplyUniformScalar );	break;
			case Uniform::kVec2:	WRITE_COMMAND( kCommandApplyUniformVec2 );		break;
			case Uniform::kVec3:	WRITE_COMMAND( kCommandApplyUniformVec3 );		break;
			case Uniform::kVec4:	WRITE_COMMAND( kCommandApplyUniformVec4 );		break;
			case Uniform::kMat3:	WRITE_COMMAND( kCommandApplyUniformMat3 );		break;
			case Uniform::kMat4:	WRITE_COMMAND( kCommandApplyUniformMat4 );		break;
			default:				Rtt_ASSERT_NOT_REACHED();						break;*/
		}
	//	Write<GLint>( location );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
