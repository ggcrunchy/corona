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

#include "Renderer/Rtt_FrameBufferObject.h"
#include "Renderer/Rtt_VulkanFrameBufferObject.h"
#include "Renderer/Rtt_VulkanGeometry.h"
#include "Renderer/Rtt_VulkanProgram.h"
#include "Renderer/Rtt_VulkanTexture.h"
#include "Renderer/Rtt_VulkanRenderer.h"
#include "Display/Rtt_ShaderResource.h"

#include <cinttypes> // https://stackoverflow.com/questions/8132399/how-to-printf-uint64-t-fails-with-spurious-trailing-in-format#comment9979590_8132440
#include <limits>

#include "CoronaLog.h"

// ----------------------------------------------------------------------------

namespace /*anonymous*/
{
	enum Command
	{
		kCommandBindFrameBufferObject,
		kCommandUnBindFrameBufferObject,
		kCommandBindGeometry,
		kCommandBindTexture,
		kCommandBindProgram,
		kCommandApplyPushConstantScalar,
		kCommandApplyPushConstantMaskTransform,
		kCommandApplyUniformScalar,
		kCommandApplyUniformVec2,
		kCommandApplyUniformVec3,
		kCommandApplyUniformVec4,
		kCommandApplyUniformMat3,
		kCommandApplyUniformMat4,
		kCommandEnableBlend,
		kCommandDisableBlend,
		kCommandSetBlendFunction,
		kCommandSetBlendEquation,
		kCommandSetViewport,
		kCommandEnableScissor,
		kCommandDisableScissor,
		kCommandSetScissorRegion,
		kCommandEnableMultisample,
		kCommandDisableMultisample,
		kCommandClear,
		kCommandDraw,
		kCommandDrawIndexed,
		kNumCommands
	};

	// To ease reading/writing of arrays
	struct Vec2 { Rtt::Real data[2]; };
	struct Vec3 { Rtt::Real data[3]; };
	struct Vec4 { Rtt::Real data[4]; };
	struct Mat3 { Rtt::Real data[9]; };
	struct Mat4 { Rtt::Real data[16]; };

	// NOT USED: const Rtt::Real kNanosecondsToMilliseconds = 1.0f / 1000000.0f;
	const U32 kTimerQueryCount = 3;
	
	// The Uniform timestamp counter must be the same for both the
	// front and back CommandBuffers, though only one CommandBuffer
	// will ever write the timestamp on any given frame. If it were
	// ever the case that more than two CommandBuffers were used,
	// this would need to be made a shared member variable.
	static U32 gUniformTimestamp = 0;

	// Extract location and data from buffer
	#define READ_UNIFORM_DATA( Type ) \
		VulkanProgram::Location location = Read<VulkanProgram::Location>(); \
		Type value = Read<Type>();

	// Extract data but query for location
	#define READ_UNIFORM_DATA_WITH_PROGRAM( Type ) \
				Rtt::VulkanProgram* program = Read<Rtt::VulkanProgram*>(); \
				U32 index = Read<U32>(); \
				VulkanProgram::Location location = program->GetUniformLocation( index, fCurrentDrawVersion ); \
				Type value = Read<Type>();
	
	#define CHECK_ERROR_AND_BREAK /* something... */ break;

	// Ensure command count is incremented
	#define WRITE_COMMAND( command ) Write<Command>( command ); ++fNumCommands;
	
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
}

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

VulkanCommandBuffer::VulkanCommandBuffer( Rtt_Allocator* allocator, VulkanRenderer & renderer )
:	CommandBuffer( allocator ),
	fCurrentPrepVersion( Program::kMaskCount0 ),
	fCurrentDrawVersion( Program::kMaskCount0 ),
	fProgram( NULL ),
	fDefaultFBO( NULL ),
	fFBO( NULL ),
	fTimeTransform( NULL ),
//	fTimerQueries( new U32[kTimerQueryCount] ),
//	fTimerQueryIndex( 0 ),
	fElapsedTimeGPU( 0.0f ),
	fRenderer( renderer ),
	fImageAvailableSemaphore( VK_NULL_HANDLE ),
	fRenderFinishedSemaphore( VK_NULL_HANDLE ),
	fInFlight( VK_NULL_HANDLE ),
	fLists( NULL ),
	fPushConstants( NULL ),
	fCommandBuffer( VK_NULL_HANDLE ),
	fTextures( VK_NULL_HANDLE ),
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

	fPushConstants = Rtt_NEW( NULL, PushConstantState() );

	if ( VK_NULL_HANDLE == fInFlight || VK_NULL_HANDLE == fImageAvailableSemaphore || VK_NULL_HANDLE == fRenderFinishedSemaphore)
	{
		CoronaLog( "Failed to create some synchronziation objects!" );

		vkDestroySemaphore( device, fImageAvailableSemaphore, vulkanAllocator );
		vkDestroySemaphore( device, fRenderFinishedSemaphore, vulkanAllocator );
		vkDestroyFence( device, fInFlight, vulkanAllocator );

		fImageAvailableSemaphore = VK_NULL_HANDLE;
		fRenderFinishedSemaphore = VK_NULL_HANDLE;
		fInFlight = VK_NULL_HANDLE;

		Rtt_DELETE( fPushConstants );

		fPushConstants = NULL;
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
	Rtt_DELETE( fPushConstants );
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
	// TODO: probably a no-op?
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

bool 
VulkanCommandBuffer::PreparePool( VulkanState * state, DescriptorLists & lists )
{
	return !lists.fPools.empty() || lists.AddPool( state, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1U, 1U );
}

bool 
VulkanCommandBuffer::PrepareTexturesPool( VulkanState * state )
{
	const U32 descriptorCount = 1024U * 5U; // TODO: is this how to allocate this? (maybe arrays are just too complex / wasteful for the common case)

	return fLists[DescriptorLists::eTexture].AddPool( state, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, descriptorCount, 1024U );
}

void
VulkanCommandBuffer::BindFrameBufferObject( FrameBufferObject* fbo )
{
	if( fbo )
	{
		WRITE_COMMAND( kCommandBindFrameBufferObject );
		Write<GPUResource*>( fbo->GetGPUResource() );
	}
	else
	{
		WRITE_COMMAND( kCommandUnBindFrameBufferObject );
	}
}

void 
VulkanCommandBuffer::BindGeometry( Geometry* geometry )
{
	WRITE_COMMAND( kCommandBindGeometry );
	Write<GPUResource*>( geometry->GetGPUResource() );
}

void 
VulkanCommandBuffer::BindTexture( Texture* texture, U32 unit )
{
	WRITE_COMMAND( kCommandBindTexture );
	Write<U32>( unit );
	Write<GPUResource*>( texture->GetGPUResource() );
}

void 
VulkanCommandBuffer::BindProgram( Program* program, Program::Version version )
{
	WRITE_COMMAND( kCommandBindProgram );
	Write<Program::Version>( version );
	Write<GPUResource*>( program->GetGPUResource() );

	fCurrentPrepVersion = version;
	fProgram = program;

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
	WRITE_COMMAND( enabled ? kCommandEnableBlend : kCommandDisableBlend );
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
	WRITE_COMMAND( kCommandSetBlendFunction );

	VkBlendFactor srcColor = VulkanFactorForBlendParam( mode.fSrcColor );
	VkBlendFactor dstColor = VulkanFactorForBlendParam( mode.fDstColor );

	VkBlendFactor srcAlpha = VulkanFactorForBlendParam( mode.fSrcAlpha );
	VkBlendFactor dstAlpha = VulkanFactorForBlendParam( mode.fDstAlpha );

	Write<VkBlendFactor>( srcColor );
	Write<VkBlendFactor>( dstColor );
	Write<VkBlendFactor>( srcAlpha );
	Write<VkBlendFactor>( dstAlpha );
}

void 
VulkanCommandBuffer::SetBlendEquation( RenderTypes::BlendEquation mode )
{
	WRITE_COMMAND( kCommandSetBlendEquation );

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
	
	Write<VkBlendOp>( equation );
}

void
VulkanCommandBuffer::SetViewport( int x, int y, int width, int height )
{
	WRITE_COMMAND( kCommandSetViewport );
	Write<int>(x);
	Write<int>(y);
	Write<int>(width);
	Write<int>(height);
}

void 
VulkanCommandBuffer::SetScissorEnabled( bool enabled )
{
	WRITE_COMMAND( enabled ? kCommandEnableScissor : kCommandDisableScissor );
	// No-op: always want scissor, possibly fullscreen
}

void 
VulkanCommandBuffer::SetScissorRegion( int x, int y, int width, int height )
{
	WRITE_COMMAND( kCommandSetScissorRegion );/*
	Write<GLint>(x);
	Write<GLint>(y);
	Write<GLsizei>(width);
	Write<GLsizei>(height);*/
	// TODO? seems to be dead code
}

void
VulkanCommandBuffer::SetMultisampleEnabled( bool enabled )
{
	WRITE_COMMAND( enabled ? kCommandEnableMultisample : kCommandDisableMultisample );
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
	WRITE_COMMAND( kCommandClear );
	Write<Real>(r);
	Write<Real>(g);
	Write<Real>(b);
	Write<Real>(a);
}

void 
VulkanCommandBuffer::Draw( U32 offset, U32 count, Geometry::PrimitiveType type )
{
	Rtt_ASSERT( fProgram && fProgram->GetGPUResource() );
	ApplyUniforms( fProgram->GetGPUResource() );
	
	WRITE_COMMAND( kCommandDraw );
	switch( type )
	{
		case Geometry::kTriangleStrip:	Write<VkPrimitiveTopology>(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);	break;
		case Geometry::kTriangleFan:	Write<VkPrimitiveTopology>(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN);		break;
		case Geometry::kTriangles:		Write<VkPrimitiveTopology>(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);		break;
		case Geometry::kLines:			Write<VkPrimitiveTopology>(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);			break;
		case Geometry::kLineLoop:		Write<VkPrimitiveTopology>(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);		break;
		default: Rtt_ASSERT_NOT_REACHED(); break;
	}
	Write<U32>(offset);
	Write<U32>(count);
}

void 
VulkanCommandBuffer::DrawIndexed( U32, U32 count, Geometry::PrimitiveType type )
{
	// The first argument, offset, is currently unused. If support for non-
	// VBO based indexed rendering is added later, an offset may be needed.

	Rtt_ASSERT( fProgram && fProgram->GetGPUResource() );
	ApplyUniforms( fProgram->GetGPUResource() );
	
	WRITE_COMMAND( kCommandDrawIndexed );
	switch( type )
	{
		case Geometry::kIndexedTriangles:	Write<VkPrimitiveTopology>(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);	break;
		default: Rtt_ASSERT_NOT_REACHED(); break;
	}
	Write<U32>(count);
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
	DEBUG_PRINT( "--Begin Rendering: VulkanCommandBuffer --" );

	InitializeFBO();

#ifdef ENABLE_GPU_TIMER_QUERIES
	if( measureGPU )
	{/*
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
	*/}

	if( measureGPU )
	{
	//	glEndQuery( GL_TIME_ELAPSED );
	}
#endif
	// Reset the offset pointer to the start of the buffer.
	// This is safe to do here, as preparation work is done
	// on another CommandBuffer while this one is executing.
	fOffset = fBuffer;

	//GL_CHECK_ERROR();
	VkRenderPassBeginInfo renderPassBeginInfo = {}, * pendingPass = NULL;

	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

	// TODO: initialize FBO... (or come up with better way to do set up swapchain one...)

	for( U32 i = 0; i < fNumCommands; ++i )
	{
		Command command = Read<Command>();

// printf( "GLCommandBuffer::Execute [%d/%d] %d\n", i, fNumCommands, command );

		Rtt_ASSERT( command < kNumCommands );
		switch( command )
		{
			case kCommandBindFrameBufferObject:
			{
				VulkanFrameBufferObject* fbo = Read<VulkanFrameBufferObject*>();

				U32 id;

				fFBO = fbo;
				pendingPass = &renderPassBeginInfo;

				fbo->Bind( fImageIndex, *pendingPass, id );
				fRenderer.SetRenderPass( id, pendingPass->renderPass );
/*
				DEBUG_PRINT( "Bind FrameBufferObject: Vulkan ID: %i, Vulkan Texture ID, if any: %d",
								fbo->GetName(),
								fbo->GetTextureName() );*/

				CHECK_ERROR_AND_BREAK;
			}
			case kCommandUnBindFrameBufferObject:
			{
/****** SubmitFBO( vulkanFBO ) */
			//	glBindFramebuffer( GL_FRAMEBUFFER, fDefaultFBO );
				DEBUG_PRINT( "Unbind FrameBufferObject: Vulkan name: %p (fDefaultFBO)", fDefaultFBO );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandBindGeometry:
			{
				VulkanGeometry* geometry = Read<VulkanGeometry*>();
				geometry->Bind( fRenderer, fCommandBuffer );
				DEBUG_PRINT( "Bind Geometry %p", geometry );
				CHECK_ERROR_AND_BREAK;

			}
			case kCommandBindTexture:
			{
				U32 unit = Read<U32>();
				VulkanTexture* texture = Read<VulkanTexture*>();
				texture->Bind( *this, unit );

				DEBUG_PRINT( "Bind Texture: texture=%p unit=%i Vulkan ID=%" PRIx64,
								texture,
								unit,
								texture->GetImage() );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandBindProgram:
			{
				fCurrentDrawVersion = Read<Program::Version>();
				VulkanProgram* program = Read<VulkanProgram*>();
				program->Bind( fRenderer, fCurrentDrawVersion );
				DEBUG_PRINT( "Bind Program: program=%p version=%i", program, fCurrentDrawVersion );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyPushConstantScalar:
			{
				U32 offset = Read<U32>();
				fPushConstants->ClaimOffsets(offset, offset);
				*fPushConstants->GetData(offset) = Read<Real>();
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyPushConstantMaskTransform:
			{
				U32 maskOffset = Read<U32>();
				Vec4 maskMatrix = Read<Vec4>();
				U32 translationOffset = Read<U32>();
				Vec2 maskTranslation = Read<Vec2>();
				fPushConstants->ClaimOffsets(maskOffset, translationOffset);
				memcpy(fPushConstants->GetData(maskOffset), &maskMatrix, sizeof( Vec4 ));
				memcpy(fPushConstants->GetData(translationOffset), &maskTranslation, sizeof( Vec2 ));
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformScalar:
			{
				READ_UNIFORM_DATA( Real );
				U32 index = Read<U32>();
				size_t offset = Read<size_t>();
				UniformsToWrite utw = PointToUniform( index, offset );
				ReadUniform( utw, &value, offset, sizeof( Real ) );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformVec2:
			{
				READ_UNIFORM_DATA( Vec2 );
				U32 index = Read<U32>();
				size_t offset = Read<size_t>();
				UniformsToWrite utw = PointToUniform( index, offset );
				ReadUniform( utw, &value, offset, sizeof( Vec2 ) );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformVec3:
			{
				READ_UNIFORM_DATA( Vec3 );
				U32 index = Read<U32>();
				size_t offset = Read<size_t>();
				UniformsToWrite utw = PointToUniform( index, offset );
				ReadUniform( utw, &value, offset, sizeof( Vec3 ) );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformVec4:
			{
				READ_UNIFORM_DATA( Vec4 );
				U32 index = Read<U32>();
				size_t offset = Read<size_t>();
				UniformsToWrite utw = PointToUniform( index, offset );
				ReadUniform( utw, &value, offset, sizeof( Vec4 ) );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformMat3:
			{
				READ_UNIFORM_DATA( Mat3 );
				U32 index = Read<U32>();
				size_t offset = Read<size_t>();
				UniformsToWrite utw = PointToUniform( index, offset );
				for (int i = 0; i < 3; ++i)
				{
					ReadUniform( utw, &value.data[i * 3], offset + i * 4, sizeof( Vec3 ) );
				}
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformMat4:
			{
				READ_UNIFORM_DATA( Mat4 );
				U32 index = Read<U32>();
				size_t offset = Read<size_t>();
				UniformsToWrite utw = PointToUniform( index, offset );
				ReadUniform( utw, &value, offset, sizeof( Mat4 ) );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandEnableBlend:
			{
				fRenderer.EnableBlend( true );
				DEBUG_PRINT( "Enable blend" );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandDisableBlend:
			{
				fRenderer.EnableBlend( false );
				DEBUG_PRINT( "Disable blend" );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandSetBlendFunction:
			{
				VkBlendFactor srcColor = Read<VkBlendFactor>();
				VkBlendFactor dstColor = Read<VkBlendFactor>();

				VkBlendFactor srcAlpha = Read<VkBlendFactor>();
				VkBlendFactor dstAlpha = Read<VkBlendFactor>();

				fRenderer.SetBlendFactors( srcColor, srcAlpha, dstColor, dstAlpha );
				DEBUG_PRINT(
					"Set blend function: srcColor=%i, dstColor=%i, srcAlpha=%i, dstAlpha=%i",
					srcColor, dstColor, srcAlpha, dstAlpha );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandSetBlendEquation:
			{
				VkBlendOp equation = Read<VkBlendOp>();
				fRenderer.SetBlendEquations( equation, equation );
				DEBUG_PRINT( "Set blend equation: equation=%i", equation );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandSetViewport:
			{
				int x = Read<int>();
				int y = Read<int>();
				int width = Read<int>();
				int height = Read<int>();
				
				VkViewport viewport;

				viewport.x = float( x );
				viewport.y = float( y );
				viewport.width = float( width );
				viewport.height = float( height );
				viewport.minDepth = 0.f;
				viewport.maxDepth = 1.f;
				vkCmdSetViewport( fCommandBuffer, 0U, 1U, &viewport ); // TODO: given usage, might just follow lead of kCommandClear...
				DEBUG_PRINT( "Set viewport: x=%i, y=%i, width=%i, height=%i", x, y, width, height );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandEnableScissor:
			{
				// TODO?
				DEBUG_PRINT( "Enable scissor test" );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandDisableScissor:
			{
				// TODO?
				DEBUG_PRINT( "Disable scissor test" );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandSetScissorRegion:
			{
				// TODO?
				/*
				GLint x = Read<GLint>();
				GLint y = Read<GLint>();
				GLsizei width = Read<GLsizei>();
				GLsizei height = Read<GLsizei>();
				glScissor( x, y, width, height );
				DEBUG_PRINT( "Set scissor window x=%i, y=%i, width=%i, height=%i", x, y, width, height );*/
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandEnableMultisample:
			{
				// TODO: Rtt_glEnableMultisample();
				DEBUG_PRINT( "Enable multisample test" );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandDisableMultisample:
			{
				// TODO: Rtt_glDisableMultisample();
				DEBUG_PRINT( "Disable multisample test" );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandClear:
			{
				Real r = Read<Real>();
				Real g = Read<Real>();
				Real b = Read<Real>();
				Real a = Read<Real>();

				VkClearValue value;

				// TODO: allow this to accommodate float targets?

				value.color.uint32[0] = uint32_t( 255. * r );
				value.color.uint32[1] = uint32_t( 255. * g );
				value.color.uint32[2] = uint32_t( 255. * b );
				value.color.uint32[3] = uint32_t( 255. * a );

				std::vector< VkClearValue > & clearValues = fFBO->GetClearValues();
				U32 index = 0U; // n.b. for future use

				if (index >= clearValues.size())
				{
					clearValues.resize( index + 1U, VkClearValue{} );
				}

				clearValues[index] = value;

				DEBUG_PRINT( "Clear: r=%f, g=%f, b=%f, a=%f", r, g, b, a );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandDraw:
			{
				VkPrimitiveTopology mode = Read<VkPrimitiveTopology>();
				U32 offset = Read<U32>();
				U32 count = Read<U32>();

				PrepareDraw( mode, pendingPass );
				vkCmdDraw( fCommandBuffer, count, 1U, offset, 0U );

				pendingPass = NULL;

				DEBUG_PRINT( "Draw: mode=%i, offset=%u, count=%u", mode, offset, count );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandDrawIndexed:
			{
				VkPrimitiveTopology mode = Read<VkPrimitiveTopology>();
				U32 count = Read<U32>();

				PrepareDraw( mode, pendingPass );

				// The first argument, offset, is currently unused. If support for non-
				// VBO based indexed rendering is added later, an offset may be needed.

				vkCmdDrawIndexed( fCommandBuffer, count, 1U, 0U, 0U, 0U );

				pendingPass = NULL;

				DEBUG_PRINT( "Draw indexed: mode=%i, count=%u", mode, count );
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
	//	glEndQuery( GL_TIME_ELAPSED );
	}
#endif
	
	DEBUG_PRINT( "--End Rendering: VulkanCommandBuffer --\n" );

//	VULKAN_CHECK_ERROR();

	if (fCommandBuffer != VK_NULL_HANDLE && fSwapchain != VK_NULL_HANDLE)
	{
		if (fFBO)
		{
			Rtt_ASSERT( !pendingPass );

			vkCmdEndRenderPass( fCommandBuffer );
		}

		fFBO = NULL;

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
			fTextures = VK_NULL_HANDLE;

			fTextureState.assign( 5U, VkDescriptorImageInfo{} );
		}

		else
		{
			CoronaLog( "Failed to begin recording command buffer!" );
		}
	}
}

void VulkanCommandBuffer::PrepareDraw( VkPrimitiveTopology topology, VkRenderPassBeginInfo * renderPassBeginInfo )
{
	CommitFBO( renderPassBeginInfo );

	fRenderer.SetPrimitiveTopology( topology );

	Rtt_ASSERT( fProgram && fProgram->GetGPUResource() );
	
	ApplyUniforms( fProgram->GetGPUResource() );

	VkDevice device = fRenderer.GetState()->GetDevice();
	VkDescriptorSet sets[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
	int uboLists[] = { DescriptorLists::eUBO, DescriptorLists::eUserDataUBO };
	uint32_t dynamicOffsets[2], count = 0U;

	for (int i = 0; i < 2; ++i)
	{
		int listIndex = uboLists[i];

		if (fLists[listIndex].fDirty)
		{
			DescriptorLists & lists = fLists[listIndex];

			sets[listIndex] = lists.fSets[lists.fBufferIndex];
			dynamicOffsets[count++] = lists.fOffset;

			lists.fOffset += U32( lists.fDynamicAlignment );
		}
	}

	if (fLists[DescriptorLists::eTexture].fDirty)
	{
		sets[DescriptorLists::eTexture] = fTextures;
	}

	if (count || fLists[DescriptorLists::eTexture].fDirty)
	{
		if (count)
		{
			vkFlushMappedMemoryRanges( device, fMappedMemoryRanges.size(), fMappedMemoryRanges.data() );

			fMappedMemoryRanges.clear();
		}

		vkCmdBindDescriptorSets( fCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, fRenderer.GetPipelineLayout(), 0U, 3U, sets, count, dynamicOffsets );
	}

	for (int i = 0; i < 3; ++i)
	{
		fLists[i].fDirty = false;
	}

	if (fPushConstants->IsValid())
	{
		U32 offset = fPushConstants->Offset(), size = fPushConstants->Range() * sizeof( float );

		vkCmdPushConstants( fCommandBuffer, fRenderer.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, offset * sizeof( float ), size, fPushConstants->GetData( offset ) );

		fPushConstants->Reset();
	}
}

void VulkanCommandBuffer::CommitFBO( VkRenderPassBeginInfo * renderPassBeginInfo )
{
	if (fFBO && renderPassBeginInfo)
	{
		const std::vector< VkClearValue > & clearValues = fFBO->GetClearValues();

		renderPassBeginInfo->clearValueCount = clearValues.size();
		renderPassBeginInfo->pClearValues = clearValues.data();

		vkCmdBeginRenderPass( fCommandBuffer, renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );
	}
}

void VulkanCommandBuffer::AddGraphicsPipeline( VkPipeline pipeline )
{
	if (fCommandBuffer != VK_NULL_HANDLE)
	{
		vkCmdBindPipeline( fCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	}
}


VkDescriptorSet VulkanCommandBuffer::AddTexture( U32 unit, const VkDescriptorImageInfo & imageInfo )
{
	VulkanState * state = fRenderer.GetState();
	DescriptorLists & list = fLists[DescriptorLists::eTexture];
	bool isNew = memcmp( &imageInfo, &fTextureState[unit], sizeof( VkDescriptorImageInfo ) ) != 0;
	bool becameDirty = isNew && !list.fDirty;

	if (becameDirty)
	{
		fTextures = VK_NULL_HANDLE;

		if (list.fPools.empty() && !PrepareTexturesPool( state ))
		{
			CoronaLog( "Failed to create initial descriptor texture pool!" );

			return VK_NULL_HANDLE;
		}

		VkDescriptorSetAllocateInfo allocInfo = {};
		VkDescriptorSetLayout layout = fRenderer.GetTextureLayout();

		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorSetCount = 1U;
		allocInfo.pSetLayouts = &layout;
				
		VkResult result = VK_ERROR_UNKNOWN;
		bool doRetry = false;

		do {
			allocInfo.descriptorPool = list.fPools.back();
			result = vkAllocateDescriptorSets( state->GetDevice(), &allocInfo, &fTextures );

			if (VK_ERROR_OUT_OF_POOL_MEMORY == result)
			{
				Rtt_ASSERT( !doRetry ); // this should never happen

				doRetry = PrepareTexturesPool( state );
			}
		} while (doRetry);

		if (result != VK_SUCCESS)
		{
			CoronaLog( "Failed to allocate texture descriptor set!" );

			return VK_NULL_HANDLE;
		}
	}

	if (isNew && fTextures != VK_NULL_HANDLE)
	{
		list.fDirty = true;
		fTextureState[unit] = imageInfo;
	}

	return fTextures;
}

template <typename T>
T 
VulkanCommandBuffer::Read()
{
	Rtt_ASSERT( fOffset < fBuffer + fBytesAllocated );
	T result = reinterpret_cast<T*>( fOffset )[0];
	fOffset += sizeof( T );
	return result;
}

template <typename T>
void 
VulkanCommandBuffer::Write( T value )
{
	U32 size = sizeof(T);
	U32 bytesNeeded = fBytesUsed + size;
	if( bytesNeeded > fBytesAllocated )
	{
		U32 doubleSize = fBytesUsed ? 2 * fBytesUsed : 4;
		U32 newSize = Max( bytesNeeded, doubleSize );
		U8* newBuffer = new U8[newSize];

		memcpy( newBuffer, fBuffer, fBytesUsed );
		delete [] fBuffer;

		fBuffer = newBuffer;
		fBytesAllocated = newSize;
	}

	memcpy( fBuffer + fBytesUsed, &value, size );
	fBytesUsed += size;
}

void VulkanCommandBuffer::ApplyUniforms( GPUResource* resource )
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

	for( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i)
	{
		const UniformUpdate& update = fUniformUpdates[i];
		if( update.uniform && update.timestamp != vulkanProgram->GetUniformTimestamp( i, fCurrentPrepVersion ) )
		{		
			ApplyUniform( *vulkanProgram, i );
		}
	}

	if (transformed)
	{
		fUniformUpdates[Uniform::kTotalTime].uniform->SetValue(rawTotalTime);
	}
}

void VulkanCommandBuffer::ApplyPushConstant( Uniform * uniform, size_t offset, size_t translationOffset )
{
	Uniform::DataType dataType = uniform->GetDataType();

	if ( Uniform::kScalar == dataType )
	{
//		dst = pushConstants.fData + location.fOffset;
		WRITE_COMMAND(kCommandApplyPushConstantScalar);
		Write<U32>(offset);
		Write<Real>(*reinterpret_cast<Real *>(uniform->GetData()));
	}

	else
	{
		Rtt_ASSERT( Uniform::kMat3 == dataType );

		WRITE_COMMAND(kCommandApplyPushConstantMaskTransform);

		// Mindful of the difficulties mentioned re. kVec3 in https://stackoverflow.com/questions/38172696/should-i-ever-use-a-vec3-inside-of-a-uniform-buffer-or-shader-storage-buffer-o,
		// mask matrices are decomposed as a vec2[2] and vec2. Three components are always constant and thus omitted.
		// The vec2 array avoids consuming two vectors, cf. https://www.khronos.org/opengl/wiki/Layout_Qualifier_(GLSL).
		// The two elements should be columns, to allow mat2(vec[0], vec[1]) on the shader side.
		float * src = reinterpret_cast< float * >( uniform->GetData() );
		Vec4 maskMatrix = {
			src[0], // row 1, col 1
			src[3], // row 2, col 1
			src[1], // row 1, col 2
			src[4]  // row 2, col 2
		};

	//	dst = pushConstants.fData + location.fOffset;
		Write<U32>(offset);
		Write<Vec4>(maskMatrix);
/*
( if possible, avoid this in favor of what Cross gives us )
		S32 maskTranslationOffset, maskMatrixVectorOffset = S32( offset ) & 0xF0; // cf. shell_default_vulkan

		switch (maskMatrixVectorOffset)
		{
		case 0x10: // mask matrix 0 = vector #2; translation = vector #1, offset #0
		case 0x40: // mask matrix 2 = vector #5; translation = vector #4, offset #2
			low = maskMatrixVectorOffset - 0x10;
			high = maskMatrixVectorOffset;
			maskTranslationOffset = low;
					
			if (0x40 == maskMatrixVectorOffset)
			{
				maskTranslationOffset += 0x8;
			}

			break;
		case 0x20: // mask matrix 1 = vector #3; translation = vector #4, offset #0
			low = maskMatrixVectorOffset;
			high = maskMatrixVectorOffset + 0x10;
			maskTranslationOffset = high;

			break;
		default:
			Rtt_ASSERT_NOT_REACHED();
		}
*/
		Vec2 translation = { src[2], src[5] };
		Write<U32>(translationOffset);
		Write<Vec2>(translation);
	}
}

void VulkanCommandBuffer::ApplyUniform( VulkanProgram & vulkanProgram, U32 index )
{
	const UniformUpdate& update = fUniformUpdates[index];
	vulkanProgram.SetUniformTimestamp( index, fCurrentPrepVersion, update.timestamp );

	VulkanProgram::Location location = vulkanProgram.GetUniformLocation( Uniform::kViewProjectionMatrix + index, fCurrentPrepVersion );
	Uniform* uniform = update.uniform;

	if (index >= Uniform::kMaskMatrix0 && index <= Uniform::kMaskMatrix2)
	{
		VulkanProgram::Location translationLocation = vulkanProgram.GetTranslationLocation( Uniform::kViewProjectionMatrix + index, fCurrentPrepVersion );

		ApplyPushConstant( uniform, location.fOffset, translationLocation.fOffset );
	}

	else if (Uniform::kTotalTime == index) // TODO: other push constants
	{
		ApplyPushConstant( uniform, location.fOffset, 0U );
	}

	else
	{
		Uniform::DataType dataType = uniform->GetDataType();

		switch( dataType )
		{
			case Uniform::kScalar:
				WRITE_COMMAND(kCommandApplyUniformScalar);
				Write<Real>(*reinterpret_cast<Real *>( uniform->GetData() ) );

				break;
			case Uniform::kVec2:
				WRITE_COMMAND(kCommandApplyUniformVec2);
				Write<Vec2>(*reinterpret_cast<Vec2 *>( uniform->GetData() ) );

				break;
			case Uniform::kVec3:
				WRITE_COMMAND(kCommandApplyUniformVec3);
				Write<Vec3>(*reinterpret_cast<Vec3 *>( uniform->GetData() ) );

				break;
			case Uniform::kVec4:
				WRITE_COMMAND(kCommandApplyUniformVec4);
				Write<Vec4>(*reinterpret_cast<Vec4 *>( uniform->GetData() ) );

				break;
			case Uniform::kMat3:
				WRITE_COMMAND(kCommandApplyUniformMat3);
				Write<Mat3>(*reinterpret_cast<Mat3 *>( uniform->GetData() ) );

				break;
			case Uniform::kMat4:
				WRITE_COMMAND(kCommandApplyUniformMat4);
				Write<Mat4>(*reinterpret_cast<Mat4 *>( uniform->GetData() ) );

				break;
			default:
				Rtt_ASSERT_NOT_REACHED();
			
				break;
		}

		Write<U32>(index);
		Write<U32>(location.fOffset);
	}
}

void VulkanCommandBuffer::ReadUniform( const UniformsToWrite & utw, const void * value, size_t offset, size_t size )
{
	if (utw.data)
	{
		memcpy( utw.data, value, size );

		VkMappedMemoryRange range;

		range.memory = utw.memory;
		range.offset = utw.offset + offset;
		range.size = size;

		fMappedMemoryRanges.push_back( range );
	}
}

VulkanCommandBuffer::UniformsToWrite VulkanCommandBuffer::PointToUniform( U32 index, size_t offset )
{
	UniformsToWrite uniformsToWrite = {};
	bool isUniformUserData = index >= Uniform::kUserData0;
	int uboIndex = isUniformUserData ? DescriptorLists::eUserDataUBO : DescriptorLists::eUBO;
	DescriptorLists & lists = fLists[uboIndex];
	
	bool ok = PreparePool( fRenderer.GetState(), lists );

	if (ok && (lists.fSets.empty() || lists.fOffset == lists.fBufferSize))
	{
		ok = lists.AddBuffer( fRenderer.GetState() );

		if (ok)
		{
			lists.fOffset = 0U;
		}
	}

	if (ok)
	{
		DynamicUniformData & data = lists.fBufferData[lists.fBufferIndex];

		uniformsToWrite.memory = data.fData->GetMemory();
		uniformsToWrite.offset = lists.fOffset;
		
		void * mapped = static_cast< U8 * >( data.fMapped ) + lists.fOffset;
		float * dst;

		if (isUniformUserData)
		{
			dst = reinterpret_cast< VulkanUserDataUBO * >( mapped )->UserData[index - Uniform::kUserData0];
		}

		else
		{
			dst = reinterpret_cast< VulkanUBO * >( mapped )->fData;
		}

		uniformsToWrite.data = reinterpret_cast< U8 *>( dst ) + offset;
	}

	return uniformsToWrite;
}

void VulkanCommandBuffer::PushConstantState::Reset()
{
	lowerOffset = ~0U;
}

void VulkanCommandBuffer::PushConstantState::ClaimOffsets( U32 offset1, U32 offset2 )
{
	if (offset2 < offset1)
	{
		U32 temp = offset2;

		offset2 = offset1;
		offset1 = temp;
	}

	offset1 = VectorOffset( offset1 );
	offset2 = VectorOffset( offset2 );

	if (IsValid())
	{
		if (offset1 < lowerOffset)
		{
			lowerOffset = offset1;
		}

		if (offset2 > upperOffset)
		{
			upperOffset = offset2;
		}
	}

	else
	{
		lowerOffset = offset1;
		upperOffset = offset2;
	}
}

U32 VulkanCommandBuffer::PushConstantState::VectorOffset( U32 offset )
{
	return offset & 0xF0;
}

float * VulkanCommandBuffer::PushConstantState::GetData( U32 offset )
{
	U8 * data = reinterpret_cast< U8 * >( fData );

	return reinterpret_cast< float * >( data + offset );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

#undef READ_UNIFORM_DATA
#undef READ_UNIFORM_DATA_WITH_PROGRAM
#undef CHECK_ERROR_AND_BREAK
#undef WRITE_COMMAND
#undef DEBUG_PRINT
#undef DEBUG_PRINT_MATRIX

// ----------------------------------------------------------------------------
