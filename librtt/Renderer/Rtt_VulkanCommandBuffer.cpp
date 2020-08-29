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
#include <malloc.h>

#include "CoronaLog.h"

// From Vulkan example on dynamic uniform buffers, by Sascha Willems:
void* alignedAlloc(size_t size, size_t alignment)
{
	void *data = nullptr;
#if defined(_MSC_VER) || defined(__MINGW32__)
	data = _aligned_malloc(size, alignment);
#else
	int res = posix_memalign(&data, alignment, size);
	if (res != 0)
		data = nullptr;
#endif
	return data;
}

void alignedFree(void* data)
{
#if	defined(_MSC_VER) || defined(__MINGW32__)
	_aligned_free(data);
#else
	free(data);
#endif
}

// ----------------------------------------------------------------------------

namespace /*anonymous*/
{
	enum Command
	{
		kCommandBindFrameBufferObject,
		kCommandUnBindFrameBufferObject,
		kCommandBeginRenderPass,
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
		kCommandApplyPushConstantFromPointerScalar,
		kCommandApplyPushConstantFromPointerMaskTransform,
		kCommandApplyUniformFromPointerScalar,
		kCommandApplyUniformFromPointerVec2,
		kCommandApplyUniformFromPointerVec3,
		kCommandApplyUniformFromPointerVec4,
		kCommandApplyUniformFromPointerMat3,
		kCommandApplyUniformFromPointerMat4,
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
#define ENABLE_DEBUG_PRINT 1
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
	fUniforms( NULL ),
	fUserData( NULL ),
	fPushConstants( NULL ),
	fCommandBuffer( VK_NULL_HANDLE ),
	fPipeline( VK_NULL_HANDLE ),
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

	fUniforms = (VulkanUniforms *)alignedAlloc( sizeof( VulkanUniforms ), 16U );
	fUserData = (VulkanUserData *)alignedAlloc( sizeof( VulkanUserData ), 16U );
	fPushConstants = (PushConstantState *)alignedAlloc( sizeof( PushConstantState ), 16U );

	new (fPushConstants) PushConstantState;

	if ( VK_NULL_HANDLE == fInFlight || VK_NULL_HANDLE == fImageAvailableSemaphore || VK_NULL_HANDLE == fRenderFinishedSemaphore)
	{
		CoronaLog( "Failed to create some synchronziation objects!" );

		vkDestroySemaphore( device, fImageAvailableSemaphore, vulkanAllocator );
		vkDestroySemaphore( device, fRenderFinishedSemaphore, vulkanAllocator );
		vkDestroyFence( device, fInFlight, vulkanAllocator );

		fImageAvailableSemaphore = VK_NULL_HANDLE;
		fRenderFinishedSemaphore = VK_NULL_HANDLE;
		fInFlight = VK_NULL_HANDLE;

		alignedFree( fUniforms );
		alignedFree( fUserData );
		alignedFree( fPushConstants );

		fUniforms = NULL;
		fUserData = NULL;
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
	alignedFree( fUniforms );
	alignedFree( fUserData );
	alignedFree( fPushConstants );
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
VulkanCommandBuffer::PrepareTexturesPool( VulkanState * state )
{
	const U32 arrayCount = 1024U; // TODO: is this how to allocate this? (maybe arrays are just too complex / wasteful for the common case)
	const U32 descriptorCount = arrayCount * 5U; // 2 + 3 masks (TODO: but could be more flexible? e.g. already reflected in VulkanProgram)

	return fLists[DescriptorLists::kTexture].AddPool( state, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, descriptorCount, arrayCount );
}

void
VulkanCommandBuffer::BindFrameBufferObject( FrameBufferObject* fbo )
{
	if( fbo )
	{
		WRITE_COMMAND( kCommandBindFrameBufferObject );
		Write<GPUResource*>( fbo->GetGPUResource() );
		// TODO: record this position on a stack...
		// redirect if necessary...
			// 
	}
	else
	{
		WRITE_COMMAND( kCommandUnBindFrameBufferObject );
		// TODO: assuming this always happens before a new bind (probably not true, but for reasoning purposes)
			// leave pointer (or space, rather) to next instruction
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
	Write<int>(height - y);
	Write<int>(width);
	Write<int>(-height);
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

	VkClearValue value;

	// TODO: allow this to accommodate float targets?

	value.color.uint32[0] = uint32_t( 255. * r );
	value.color.uint32[1] = uint32_t( 255. * g );
	value.color.uint32[2] = uint32_t( 255. * b );
	value.color.uint32[3] = uint32_t( 255. * a );

	Write<VkClearValue>(value);

	// TODO: write others, if necessary...

	WRITE_COMMAND( kCommandBeginRenderPass );
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
	std::vector< VkDescriptorImageInfo > descriptorImageInfo( 5U, VkDescriptorImageInfo{} );
	std::vector< VkClearValue > clearValues;
	VkViewport viewport;

	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	VkRenderPassBeginInfo renderPassBeginInfo = {};

	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

	// TODO: initialize FBO... (or come up with better way to do set up swapchain one...)
	if (VK_NULL_HANDLE == fCommandBuffer)
	{
		fNumCommands = 0U;
	}
	CoronaLog("RENDERER, %u", fNumCommands);
	for( U32 i = 0; i < fNumCommands; ++i )
	{
		Command command = Read<Command>();
CoronaLog( "%u, %i", i, command );
// printf( "GLCommandBuffer::Execute [%d/%d] %d\n", i, fNumCommands, command );
		Rtt_ASSERT( command < kNumCommands );
		switch( command )
		{
			case kCommandBindFrameBufferObject:
			{
// TODO: if (fFBO)
				VulkanFrameBufferObject* fbo = Read<VulkanFrameBufferObject*>();

				fFBO = fbo;

				fbo->Bind( fRenderer, fImageIndex, renderPassBeginInfo );

				clearValues.clear();
/*
				DEBUG_PRINT( "Bind FrameBufferObject: Vulkan ID: %i, Vulkan Texture ID, if any: %d",
								fbo->GetName(),
								fbo->GetTextureName() );*/

				CHECK_ERROR_AND_BREAK;
			}
			case kCommandUnBindFrameBufferObject:
			{
// TODO:
			//	glBindFramebuffer( GL_FRAMEBUFFER, fDefaultFBO );
				DEBUG_PRINT( "Unbind FrameBufferObject: Vulkan name: %p (fDefaultFBO)", fDefaultFBO );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandBeginRenderPass:
			{
				renderPassBeginInfo.clearValueCount = clearValues.size();
				renderPassBeginInfo.pClearValues = clearValues.data();
		
				vkCmdBeginRenderPass( fCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );
				vkCmdSetViewport( fCommandBuffer, 0U, 1U, &viewport );
				vkCmdSetScissor( fCommandBuffer, 0U, 1U, &renderPassBeginInfo.renderArea );

				DEBUG_PRINT( "BEGIN RENDER PASS " );
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
				texture->Bind( fLists[DescriptorLists::kTexture], descriptorImageInfo[unit] );

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
				Real value = Read<Real>();
				fPushConstants->Write( offset, &value, sizeof( Real ) );
				DEBUG_PRINT( "Set Push Constant: value=%f location=%i", value, offset );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyPushConstantMaskTransform:
			{
				U32 maskOffset = Read<U32>();
				Vec4 maskMatrix = Read<Vec4>();
				U32 translationOffset = Read<U32>();
				Vec2 maskTranslation = Read<Vec2>();
				fPushConstants->Write( maskOffset, &maskMatrix, sizeof( Vec4 ));
				fPushConstants->Write( translationOffset, &maskTranslation, sizeof( Vec2 ) );
				DEBUG_PRINT( "Set Push Constant, mask matrix: value=(%f, %f, %f, %f) location=%i", maskMatrix.data[0], maskMatrix.data[1], maskMatrix.data[2], maskMatrix.data[3], maskOffset );
				DEBUG_PRINT( "Set Push Constant, mask translation: value=(%f, %f) location=%i", maskTranslation.data[0], maskTranslation.data[1], translationOffset );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformScalar:
			{
				READ_UNIFORM_DATA( Real );
				U32 index = Read<U32>();
				U8 * data = PointToUniform( index, location.fOffset );
				memcpy( data, &value, sizeof( Real ) );
				DEBUG_PRINT( "Set Uniform: value=%f location=%i", value, location.fOffset );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformVec2:
			{
				READ_UNIFORM_DATA( Vec2 );
				U32 index = Read<U32>();
				U8 * data = PointToUniform( index, location.fOffset );
				memcpy( data, &value, sizeof( Vec2 ) );
				DEBUG_PRINT( "Set Uniform: value=(%f, %f) location=%i", value.data[0], value.data[1], location.fOffset );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformVec3:
			{
				READ_UNIFORM_DATA( Vec3 );
				U32 index = Read<U32>();
				U8 * data = PointToUniform( index, location.fOffset );
				memcpy( data, &value, sizeof( Vec3 ) );
				DEBUG_PRINT( "Set Uniform: value=(%f, %f, %f) location=%i", value.data[0], value.data[1], value.data[2], location.fOffset );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformVec4:
			{
				READ_UNIFORM_DATA( Vec4 );
				U32 index = Read<U32>();
				U8 * data = PointToUniform( index, location.fOffset );
				memcpy( data, &value, sizeof( Vec4 ) );
				DEBUG_PRINT( "Set Uniform: value=(%f, %f, %f, %f) location=%i", value.data[0], value.data[1], value.data[2], value.data[3], location.fOffset );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformMat3:
			{
				READ_UNIFORM_DATA( Mat3 );
				U32 index = Read<U32>();
				U8 * data = PointToUniform( index, location.fOffset );
				for (int i = 0; i < 3; ++i)
				{
					memcpy( data, &value.data[i * 3], sizeof( Vec3 ) );

					data += sizeof( float ) * 4;
				}
				DEBUG_PRINT_MATRIX( "Set Uniform: value=", value.data, 9 );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformMat4:
			{
				READ_UNIFORM_DATA( Mat4 );
				U32 index = Read<U32>();
				U8 * data = PointToUniform( index, location.fOffset );
				memcpy( data, &value, sizeof( Mat4 ) );
				DEBUG_PRINT_MATRIX( "Set Uniform: value=", value.data, 16 );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyPushConstantFromPointerScalar:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Real );
				if (location.IsValid())
				{
					fPushConstants->Write( location.fOffset, &value, sizeof( Real ) );
				}
				DEBUG_PRINT( "Set Push Constant: value=%f location=%i", value, location.fOffset );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyPushConstantFromPointerMaskTransform:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Vec4 );
				VulkanProgram::Location translationLocation = program->GetTranslationLocation( Uniform::kViewProjectionMatrix + index, fCurrentDrawVersion );
				Vec2 maskTranslation = Read<Vec2>();
				if (location.IsValid())
				{
					fPushConstants->Write( location.fOffset, &value, sizeof( Vec4 ));
					fPushConstants->Write( translationLocation.fOffset, &maskTranslation, sizeof( Vec2 ) );
				}
				DEBUG_PRINT( "Set Push Constant, mask matrix: value=(%f, %f, %f, %f) location=%i", value.data[0], value.data[1], value.data[2], value.data[3], location.fOffset );
				DEBUG_PRINT( "Set Push Constant, mask translation: value=(%f, %f) location=%i", maskTranslation.data[0], maskTranslation.data[1], translationLocation.fOffset );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformFromPointerScalar:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Real );
				if (location.IsValid())
				{
					U8 * data = PointToUniform( index, location.fOffset );
					memcpy( data, &value, sizeof( Real ) );
				}
				DEBUG_PRINT( "Set Uniform: value=%f location=%i", value, location.fOffset );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformFromPointerVec2:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Vec2 );
				if (location.IsValid())
				{
					U8 * data = PointToUniform( index, location.fOffset );
					memcpy( data, &value, sizeof( Vec2 ) );
				}
				DEBUG_PRINT( "Set Uniform: value=(%f, %f) location=%i", value.data[0], value.data[1], location.fOffset );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformFromPointerVec3:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Vec3 );
				if (location.IsValid())
				{
					U8 * data = PointToUniform( index, location.fOffset );
					memcpy( data, &value, sizeof( Vec3 ) );
				}
				DEBUG_PRINT( "Set Uniform: value=(%f, %f, %f) location=%i", value.data[0], value.data[1], value.data[2], location.fOffset );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformFromPointerVec4:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Vec4 );
				if (location.IsValid())
				{
					U8 * data = PointToUniform( index, location.fOffset );
					memcpy( data, &value, sizeof( Vec4 ) );
				}
				DEBUG_PRINT( "Set Uniform: value=(%f, %f, %f, %f) location=%i", value.data[0], value.data[1], value.data[2], value.data[3], location.fOffset );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformFromPointerMat3:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Mat3 );
				if (location.IsValid())
				{
					U8 * data = PointToUniform( index, location.fOffset );
					for (int i = 0; i < 3; ++i)
					{
						memcpy( data, &value.data[i * 3], sizeof( Vec3 ) );

						data += sizeof( float ) * 4;
					}
				}
				DEBUG_PRINT_MATRIX( "Set Uniform: value=", value.data, 9 );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformFromPointerMat4:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Mat4 );
				if (location.IsValid())
				{
					U8 * data = PointToUniform( index, location.fOffset );
					memcpy( data, &value, sizeof( Mat4 ) );
				}
				DEBUG_PRINT_MATRIX( "Set Uniform: value=", value.data, 16 );
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

				viewport.x = float( x );
				viewport.y = float( y );
				viewport.width = float( width );
				viewport.height = float( height );
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
				VkClearValue value = Read<VkClearValue>();

				clearValues.push_back( value );

				DEBUG_PRINT( "Clear " );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandDraw:
			{
				VkPrimitiveTopology mode = Read<VkPrimitiveTopology>();
				U32 offset = Read<U32>();
				U32 count = Read<U32>();

				if (PrepareDraw( mode, descriptorImageInfo ))
				{
					vkCmdDraw( fCommandBuffer, count, 1U, offset, 0U );
				}

				DEBUG_PRINT( "Draw: mode=%i, offset=%u, count=%u", mode, offset, count );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandDrawIndexed:
			{
				VkPrimitiveTopology mode = Read<VkPrimitiveTopology>();
				U32 count = Read<U32>();

				if (PrepareDraw( mode, descriptorImageInfo ))
				{

					// The first argument, offset, is currently unused. If support for non-
					// VBO based indexed rendering is added later, an offset may be needed.

					vkCmdDrawIndexed( fCommandBuffer, count, 1U, 0U, 0U, 0U );
				}

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
	const VulkanState * state = fRenderer.GetState();
	CoronaLog( "ERR: %s", fCommandBuffer ? "YES" : "NO" );
	VkResult endResult = VK_SUCCESS, submitResult = VK_SUCCESS;
	bool okok=false;
	if (fCommandBuffer != VK_NULL_HANDLE && fSwapchain != VK_NULL_HANDLE)
	{okok=true;
		if (renderPassBeginInfo.renderPass ) // dangling render pass?
		{
			vkCmdEndRenderPass( fCommandBuffer );
		}

		fFBO = NULL;
		CoronaLog("ENDING");
		endResult = vkEndCommandBuffer( fCommandBuffer );

		if (VK_SUCCESS == endResult)
		{CoronaLog("ENDED");
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

			vkResetFences( state->GetDevice(), 1U, &fInFlight );

			submitResult = vkQueueSubmit( state->GetGraphicsQueue(), 1, &submitInfo, fInFlight );
			CoronaLog( "SUBMIT %i", submitResult );
		}
	}
			if (true)//VK_SUCCESS == submitResult)
			{CoronaLog("PRESENTING");
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
					CoronaLog("MUTTER: %i", presentResult);
				//    framebufferResized = false;
				}

				else if (presentResult != VK_SUCCESS)
				{
					CoronaLog( "Failed to present swap chain image!" );
				}

				else
				{
					CoronaLog("WOO");
				}

				fExecuteResult = presentResult;
			}

			else
			{
				CoronaLog( "Failed to submit draw command buffer!" );

				fExecuteResult = submitResult;
			}
	//	}

		if (endResult != VK_SUCCESS)//else
		{
			CoronaLog( "Failed to record command buffer!" );

			fExecuteResult = endResult;
		}
//	}

	if (!okok)//else
	{
		fExecuteResult = VK_ERROR_INITIALIZATION_FAILED;
	}

	fCommandBuffer = VK_NULL_HANDLE;
	fPipeline = VK_NULL_HANDLE;
	fSwapchain = VK_NULL_HANDLE;

	return fElapsedTimeGPU;
}

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

		lists[DescriptorLists::kUniforms].Reset( device, fUniforms );
		lists[DescriptorLists::kUserData].Reset( device, fUserData );
		lists[DescriptorLists::kTexture].Reset( device );
		
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

bool VulkanCommandBuffer::PrepareDraw( VkPrimitiveTopology topology, std::vector< VkDescriptorImageInfo > & descriptorImageInfo )
{
	bool canDraw = fCommandBuffer != VK_NULL_HANDLE;

	if (canDraw)
	{
		fRenderer.SetPrimitiveTopology( topology );

        VkPipeline pipeline = fRenderer.ResolvePipeline();

		if (VK_NULL_HANDLE == pipeline)
		{
			return false;
		}

		else if (pipeline != fPipeline)
		{
			vkCmdBindPipeline( fCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );

			fPipeline = pipeline;
		}

		Rtt_ASSERT( fProgram && fProgram->GetGPUResource() );

		ApplyUniforms( fProgram->GetGPUResource() );

		VkDevice device = fRenderer.GetState()->GetDevice();
		VkDescriptorSet sets[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
		uint32_t dynamicOffsets[2] = {}, count = 0U, nsets = 0U;

		std::vector< VkMappedMemoryRange > memoryRanges;

		static_assert( DescriptorLists::kUniforms < DescriptorLists::kUserData, "UBOs in unexpected order" );
		static_assert( DescriptorLists::kUserData < DescriptorLists::kTexture, "UBO / textures in unexpected order" );

		U32 first = 2U; // try to do better

		for (U32 i = 0; i < 2; ++i)
		{
			if (fLists[i].fDirty)
			{
				if (i < first)
				{
					first = i;
				}

				DescriptorLists & lists = fLists[i];

				sets[nsets++] = lists.fSets[lists.fBufferIndex];
				dynamicOffsets[count++] = lists.fOffset;

				DynamicUniformData & uniforms = lists.fDynamicUniforms[lists.fBufferIndex];

				memcpy( static_cast< U8 * >( uniforms.fMapped ) + lists.fOffset, lists.fWorkspace, lists.fRawSize );
			
				VkMappedMemoryRange range = {};

				range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
				range.memory = uniforms.fBufferData->GetMemory();
				range.offset = lists.fOffset;
				range.size = lists.fRawSize;

				memoryRanges.push_back( range );

				lists.fOffset += U32( lists.fDynamicAlignment );
			}
		}

		if (fLists[DescriptorLists::kTexture].fDirty)
		{
			sets[nsets++] = AddTextureSet( descriptorImageInfo );
		}

		VkPipelineLayout pipelineLayout = fRenderer.GetPipelineLayout();

		if (nsets > 0U)
		{
			if (count)
			{
				vkFlushMappedMemoryRanges( device, memoryRanges.size(), memoryRanges.data() );
			}

			if (2U == nsets && !fLists[DescriptorLists::kUserData].fDirty) // split?
			{
				Rtt_ASSERT( 0U == first );
				Rtt_ASSERT( 1U == count );

				vkCmdBindDescriptorSets( fCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0U, 1U, &sets[0], 1U, dynamicOffsets );
				vkCmdBindDescriptorSets( fCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 2U, 1U, &sets[1], 0U, NULL );
			}

			else
			{
				vkCmdBindDescriptorSets( fCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, first, nsets, sets, count, dynamicOffsets );
			}
		}

		for (int i = 0; i < 3; ++i)
		{
			fLists[i].fDirty = false;
		}

		if (fPushConstants->IsValid())
		{
			U32 offset = fPushConstants->Offset(), size = fPushConstants->Range();

			vkCmdPushConstants( fCommandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, offset, size, fPushConstants->GetData( offset ) );
			// TODO: want fragment bit if using sampler index...
			fPushConstants->Reset();
		}
	}

	return canDraw;
}

VkDescriptorSet VulkanCommandBuffer::AddTextureSet( const std::vector< VkDescriptorImageInfo > & imageInfo )
{
	VulkanState * state = fRenderer.GetState();
	DescriptorLists & list = fLists[DescriptorLists::kTexture];

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
				
	VkDescriptorSet set = VK_NULL_HANDLE;
	bool doRetry = false;

	do {
		allocInfo.descriptorPool = list.fPools.back();
		VkResult result = vkAllocateDescriptorSets( state->GetDevice(), &allocInfo, &set );

		if (VK_ERROR_OUT_OF_POOL_MEMORY == result)
		{
			Rtt_ASSERT( !doRetry ); // this should never happen

			doRetry = PrepareTexturesPool( state );
		}

		else if (VK_SUCCESS == result)
		{
			std::vector< VkWriteDescriptorSet > writes;

			bool wasValid = false;

			for (size_t i = 0; i < imageInfo.size(); ++i)
			{
				if (VK_NULL_HANDLE == imageInfo[i].imageView)
				{
					wasValid = false;
				}

				else
				{
					if (!wasValid)
					{
						VkWriteDescriptorSet wds = {};

						wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
						wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
						wds.dstArrayElement = i;
						wds.pImageInfo = imageInfo.data() + i;
						wds.dstSet = set;

						writes.push_back( wds );

						wasValid = true;
					}

					++writes.back().descriptorCount;
				}
			}
         
			vkUpdateDescriptorSets( state->GetDevice(), writes.size(), writes.data(), 0U, NULL );

			return set;
		}
	} while (doRetry);

	CoronaLog( "Failed to allocate texture descriptor set!" );

	return VK_NULL_HANDLE;
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

void VulkanCommandBuffer::ApplyPushConstant( Uniform * uniform, size_t offset, size_t translationOffset, VulkanProgram * program, U32 index )
{
	Uniform::DataType dataType = uniform->GetDataType();

	if ( Uniform::kScalar == dataType )
	{
		if (program)
		{
			WRITE_COMMAND(kCommandApplyPushConstantFromPointerScalar);
			Write<VulkanProgram*>(program);
			Write<U32>(index);
		}

		else
		{
			WRITE_COMMAND(kCommandApplyPushConstantScalar);
			Write<U32>(offset);
		}

		Write<Real>(*reinterpret_cast<Real *>(uniform->GetData()));
	}

	else
	{
		Rtt_ASSERT( Uniform::kMat3 == dataType );

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

		if (program)
		{
			WRITE_COMMAND(kCommandApplyPushConstantFromPointerMaskTransform);
			Write<VulkanProgram*>(program);
			Write<U32>(index);
			Write<Vec4>(maskMatrix);
		}

		else
		{
			WRITE_COMMAND(kCommandApplyPushConstantMaskTransform);
			Write<U32>(offset);
			Write<Vec4>(maskMatrix);
			Write<U32>(translationOffset);
		}

		Vec2 maskTranslation = { src[6], src[7] };

		Write<Vec2>(maskTranslation);
	}
}

void VulkanCommandBuffer::WriteUniform( Uniform* uniform )
{
	switch( uniform->GetDataType() )
	{
		case Uniform::kScalar:	Write<Real>(*reinterpret_cast<Real*>(uniform->GetData()));	break;
		case Uniform::kVec2:	Write<Vec2>(*reinterpret_cast<Vec2*>(uniform->GetData()));	break;
		case Uniform::kVec3:	Write<Vec3>(*reinterpret_cast<Vec3*>(uniform->GetData()));	break;
		case Uniform::kVec4:	Write<Vec4>(*reinterpret_cast<Vec4*>(uniform->GetData()));	break;
		case Uniform::kMat3:	Write<Mat3>(*reinterpret_cast<Mat3*>(uniform->GetData()));	break;
		case Uniform::kMat4:	Write<Mat4>(*reinterpret_cast<Mat4*>(uniform->GetData()));	break;
		default:				Rtt_ASSERT_NOT_REACHED();									break;
	}
}

void VulkanCommandBuffer::ApplyUniform( VulkanProgram & vulkanProgram, U32 index )
{
	const UniformUpdate& update = fUniformUpdates[index];
	vulkanProgram.SetUniformTimestamp( index, fCurrentPrepVersion, update.timestamp );

	bool isValid = vulkanProgram.IsValid( fCurrentPrepVersion );
	VulkanProgram::Location location = vulkanProgram.GetUniformLocation( Uniform::kViewProjectionMatrix + index, fCurrentPrepVersion );
	Uniform* uniform = update.uniform;

	if (isValid && location.IsValid())
	{
		if (DescriptorLists::IsPushConstant( index ))
		{
			if (DescriptorLists::IsMaskPushConstant( index ))
			{
				VulkanProgram::Location translationLocation = vulkanProgram.GetTranslationLocation( Uniform::kViewProjectionMatrix + index, fCurrentPrepVersion );

				ApplyPushConstant( uniform, location.fOffset, translationLocation.fOffset );
			}

			else
			{
				ApplyPushConstant( uniform, location.fOffset, 0U );
			}
		}

		else
		{
			switch( uniform->GetDataType() )
			{
				case Uniform::kScalar:
					WRITE_COMMAND(kCommandApplyUniformScalar); break;
				case Uniform::kVec2:
					WRITE_COMMAND(kCommandApplyUniformVec2); break;
				case Uniform::kVec3:
					WRITE_COMMAND(kCommandApplyUniformVec3); break;
				case Uniform::kVec4:
					WRITE_COMMAND(kCommandApplyUniformVec4); break;
				case Uniform::kMat3:
					WRITE_COMMAND(kCommandApplyUniformMat3); break;
				case Uniform::kMat4:
					WRITE_COMMAND(kCommandApplyUniformMat4); break;
				default:
					Rtt_ASSERT_NOT_REACHED();
			}
			
			Write<VulkanProgram::Location>(location);
			WriteUniform( uniform );
			Write<U32>(index);

			ListsForIndex( index ).fDirty = true;
		}
	}

	else if (!isValid)
	{
		Uniform* uniform = update.uniform;

		if (DescriptorLists::IsPushConstant( index ))
		{
			ApplyPushConstant( uniform, 0U, 0U, &vulkanProgram, index );
		}

		else
		{
			switch( uniform->GetDataType() )
			{
				case Uniform::kScalar:
					WRITE_COMMAND(kCommandApplyUniformFromPointerScalar);	break;
				case Uniform::kVec2:
					WRITE_COMMAND(kCommandApplyUniformFromPointerVec2);	break;
				case Uniform::kVec3:
					WRITE_COMMAND(kCommandApplyUniformFromPointerVec3);	break;
				case Uniform::kVec4:
					WRITE_COMMAND(kCommandApplyUniformFromPointerVec4);	break;
				case Uniform::kMat3:
					WRITE_COMMAND(kCommandApplyUniformFromPointerMat3);	break;
				case Uniform::kMat4:
					WRITE_COMMAND(kCommandApplyUniformFromPointerMat4);	break;
				default:
					Rtt_ASSERT_NOT_REACHED();
			}
			
			Write<VulkanProgram *>(&vulkanProgram);
			Write<U32>(index);
			WriteUniform(uniform);

			ListsForIndex( index ).fDirty = true; // cf. ApplyUniforms()
		}
	}
}

U8 * VulkanCommandBuffer::PointToUniform( U32 index, size_t offset )
{
	DescriptorLists & lists = ListsForIndex( index );
	bool ok = lists.EnsureAvailability( fRenderer.GetState() );

	return ok ? lists.fWorkspace + offset : NULL;
}

DescriptorLists & VulkanCommandBuffer::ListsForIndex( U32 index )
{
	return DescriptorLists::IsUserData( index ) ? fLists[DescriptorLists::kUserData] : fLists[DescriptorLists::kUniforms];
}

void VulkanCommandBuffer::PushConstantState::Reset()
{
	upperOffset = 0U;
	lowerOffset = 1U;
}

void VulkanCommandBuffer::PushConstantState::Write( U32 offset, const void * src, size_t size )
{
	memcpy( GetData( offset ), src, size );

	offset &= 0xF0;

	if (IsValid())
	{
		if (offset < lowerOffset)
		{
			lowerOffset = offset;
		}

		if (offset > upperOffset)
		{
			upperOffset = offset;
		}
	}

	else
	{
		lowerOffset = upperOffset = offset;
	}
}

float * VulkanCommandBuffer::PushConstantState::GetData( U32 offset )
{
	U8 * bytes = reinterpret_cast< U8 * >( fData );

	return reinterpret_cast< float * >( bytes + offset );
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
