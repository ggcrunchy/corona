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
#include "Renderer/Rtt_VulkanTexture.h"/*
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

VulkanCommandBuffer::VulkanCommandBuffer( Rtt_Allocator* allocator )
:	CommandBuffer( allocator ),
	fCurrentPrepVersion( Program::kMaskCount0 ),
	fCurrentDrawVersion( Program::kMaskCount0 ),/*
	fProgram( NULL ),
	fDefaultFBO( 0 ),*/
	fTimeTransform( NULL ),/*
	fTimerQueries( new U32[kTimerQueryCount] ),
	fTimerQueryIndex( 0 ),*/
	fElapsedTimeGPU( 0.0f ),
	fFirstPipeline( VK_NULL_HANDLE )

{
	for(U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i)
	{
		fUniformUpdates[i].uniform = NULL;
		fUniformUpdates[i].timestamp = 0;
	}
}

VulkanCommandBuffer::~VulkanCommandBuffer()
{
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

	InitializePipelineState();
	
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
VulkanCommandBuffer::BindFrameBufferObject(FrameBufferObject* fbo)
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
	VulkanGeometry::VertexDescription description = vulkanGeometry->Bind();

	fWorkingPipeline.fBindingDescriptionID = description.fID;
	fVertexBindingDescriptions = description.fDescriptions;
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
	VulkanProgram::PipelineStages shaderStages = vulkanProgram->Bind( version );

	fWorkingPipeline.fShaderID = shaderStages.fID;
	fShaderStageCreateInfo = shaderStages.fStages;
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
	fColorBlendAttachments.front().blendEnable = enabled ? VK_TRUE : VK_FALSE;
	fWorkingPipeline.fBlendAttachments[0].fEnable = enabled;
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

	auto attachment = fColorBlendAttachments.front();

	attachment.srcColorBlendFactor = srcColor;
	attachment.dstColorBlendFactor = dstColor;
	attachment.srcAlphaBlendFactor = srcAlpha;
	attachment.dstAlphaBlendFactor = dstAlpha;

	fWorkingPipeline.fBlendAttachments[0].fSrcColorFactor = srcColor;
	fWorkingPipeline.fBlendAttachments[0].fDstColorFactor = dstColor;
	fWorkingPipeline.fBlendAttachments[0].fSrcAlphaFactor = srcAlpha;
	fWorkingPipeline.fBlendAttachments[0].fDstAlphaFactor = dstAlpha;
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

	auto attachment = fColorBlendAttachments.front();

	attachment.alphaBlendOp = attachment.colorBlendOp = equation;

	fWorkingPipeline.fBlendAttachments[0].fAlphaOp = equation;
	fWorkingPipeline.fBlendAttachments[0].fColorOp = equation;
}

void
VulkanCommandBuffer::SetViewport( int x, int y, int width, int height )
{
/*
	WRITE_COMMAND( kCommandSetViewport );
	Write<GLint>(x);
	Write<GLint>(y);
	Write<GLsizei>(width);
	Write<GLsizei>(height);
*/
	/*
					GLint x = Read<GLint>();
				GLint y = Read<GLint>();
				GLsizei width = Read<GLsizei>();
				GLsizei height = Read<GLsizei>();
				glViewport( x, y, width, height );
				DEBUG_PRINT( "Set viewport: x=%i, y=%i, width=%i, height=%i", x, y, width, height );
				CHECK_ERROR_AND_BREAK;
				*/
}

void 
VulkanCommandBuffer::SetScissorEnabled( bool enabled )
{
//	WRITE_COMMAND( enabled ? kCommandEnableScissor : kCommandDisableScissor );
	/*
				glEnable( GL_SCISSOR_TEST );
				DEBUG_PRINT( "Enable scissor test" );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandDisableScissor:
			{
				glDisable( GL_SCISSOR_TEST );
				DEBUG_PRINT( "Disable scissor test" );
				CHECK_ERROR_AND_BREAK;
			}
		*/
}

void 
VulkanCommandBuffer::SetScissorRegion(int x, int y, int width, int height)
{
/*
	WRITE_COMMAND( kCommandSetScissorRegion );
	Write<GLint>(x);
	Write<GLint>(y);
	Write<GLsizei>(width);
	Write<GLsizei>(height);
*/
	/*
			case kCommandSetScissorRegion:
			{
				GLint x = Read<GLint>();
				GLint y = Read<GLint>();
				GLsizei width = Read<GLsizei>();
				GLsizei height = Read<GLsizei>();
				glScissor( x, y, width, height );
				DEBUG_PRINT( "Set scissor window x=%i, y=%i, width=%i, height=%i", x, y, width, height );
				CHECK_ERROR_AND_BREAK;
				*/
}

void
VulkanCommandBuffer::SetMultisampleEnabled( bool enabled )
{
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
VulkanCommandBuffer::Clear(Real r, Real g, Real b, Real a)
{
//	vkCmdClearColorImage( fCommands, IMAgE?, IMAGE_LAYOUT?, vkClearColorValue, 1U, full size );
// URGH... might be unnecessary since also part of rendering?
/*
	WRITE_COMMAND( kCommandClear );
	Write<GLfloat>(r);
	Write<GLfloat>(g);
	Write<GLfloat>(b);
	Write<GLfloat>(a);
*/

	/*
					GLfloat r = Read<GLfloat>();
				GLfloat g = Read<GLfloat>();
				GLfloat b = Read<GLfloat>();
				GLfloat a = Read<GLfloat>();
				glClearColor( r, g, b, a );
				glClear( GL_COLOR_BUFFER_BIT );
				DEBUG_PRINT( "Clear: r=%f, g=%f, b=%f, a=%f", r, g, b, a );
				CHECK_ERROR_AND_BREAK;
	*/
}

void 
VulkanCommandBuffer::Draw( U32 offset, U32 count, Geometry::PrimitiveType type )
{
	switch( type )
	{
		case Geometry::kTriangleStrip:
			fInputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			
			break;
		case Geometry::kTriangleFan:
			fInputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
			
			break;
		case Geometry::kTriangles:
			fInputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			
			break;
		case Geometry::kLines:
			fInputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

			break;
		case Geometry::kLineLoop:
			fInputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			
			break;
		default: Rtt_ASSERT_NOT_REACHED(); break;
	}

	fWorkingPipeline.fTopology = fInputAssemblyStateCreateInfo.topology;

	ResolvePipeline();
/*
	Rtt_ASSERT( fProgram && fProgram->GetGPUResource() );
	ApplyUniforms( fProgram->GetGPUResource() );
*/
	vkCmdDraw( fCommands, count, 1U, offset, 0U );
}

void 
VulkanCommandBuffer::DrawIndexed( U32, U32 count, Geometry::PrimitiveType type )
{
	switch( type )
	{
		case Geometry::kIndexedTriangles:
			fInputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			break;
		default: Rtt_ASSERT_NOT_REACHED(); break;
	}

	fWorkingPipeline.fTopology = fInputAssemblyStateCreateInfo.topology;

	ResolvePipeline();
/*
	// The first argument, offset, is currently unused. If support for non-
	// VBO based indexed rendering is added later, an offset may be needed.

	Rtt_ASSERT( fProgram && fProgram->GetGPUResource() );
	ApplyUniforms( fProgram->GetGPUResource() );
*/
	vkCmdDrawIndexed( fCommands, count, 1U, 0U, 0U, 0U );
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
			case kCommandApplyUniformFromPointerScalar:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Real );
				glUniform1f( location, value );
				DEBUG_PRINT( "Set Uniform: value=%f location=%i", value, location );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformFromPointerVec2:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Vec2 );
				glUniform2fv( location, 1, &value.data[0] );
				DEBUG_PRINT( "Set Uniform: value=(%f, %f) location=%i", value.data[0], value.data[1], location);
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformFromPointerVec3:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Vec3 );
				glUniform3fv( location, 1, &value.data[0] );
				DEBUG_PRINT( "Set Uniform: value=(%f, %f, %f) location=%i", value.data[0], value.data[1], value.data[2], location);
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformFromPointerVec4:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Vec4 );
				glUniform4fv( location, 1, &value.data[0] );
				DEBUG_PRINT( "Set Uniform: value=(%f, %f, %f, %f) location=%i", value.data[0], value.data[1], value.data[2], value.data[3], location);
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformFromPointerMat3:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Mat3 );
				glUniformMatrix3fv( location, 1, GL_FALSE, &value.data[0] );
				DEBUG_PRINT_MATRIX( "Set Uniform: value=", value.data, 9 );
				CHECK_ERROR_AND_BREAK;
			}
			case kCommandApplyUniformFromPointerMat4:
			{
				READ_UNIFORM_DATA_WITH_PROGRAM( Mat4 );
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
	return fElapsedTimeGPU;
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
	/*
	GLProgram* glProgram = static_cast<GLProgram*>( resource );
	glProgram->SetUniformTimestamp( index, fCurrentPrepVersion, update.timestamp );

	GLint location = glProgram->GetUniformLocation( Uniform::kViewProjectionMatrix + index, fCurrentPrepVersion );*/
	if( false )//glProgram->GetHandle() && location >= 0 )
	{
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
		WriteUniform( uniform );
	}
	else if( false )//!glProgram->GetHandle() )
	{
		// The OpenGL program has not yet been created. Assume it uses the uniform
		Uniform* uniform = update.uniform;
		switch( uniform->GetDataType() )
		{/*
			case Uniform::kScalar:	WRITE_COMMAND( kCommandApplyUniformFromPointerScalar );	break;
			case Uniform::kVec2:	WRITE_COMMAND( kCommandApplyUniformFromPointerVec2 );	break;
			case Uniform::kVec3:	WRITE_COMMAND( kCommandApplyUniformFromPointerVec3 );	break;
			case Uniform::kVec4:	WRITE_COMMAND( kCommandApplyUniformFromPointerVec4 );	break;
			case Uniform::kMat3:	WRITE_COMMAND( kCommandApplyUniformFromPointerMat3 );	break;
			case Uniform::kMat4:	WRITE_COMMAND( kCommandApplyUniformFromPointerMat4 );	break;*/
			default:				Rtt_ASSERT_NOT_REACHED();								break;
		}
	//	Write<GLProgram*>( glProgram );
	//	Write<U32>( index ); 
		WriteUniform( uniform );
	}
}

void VulkanCommandBuffer::WriteUniform( Uniform* uniform )
{
	switch( uniform->GetDataType() )
	{/*
		case Uniform::kScalar:	Write<Real>(*reinterpret_cast<Real*>(uniform->GetData()));	break;
		case Uniform::kVec2:	Write<Vec2>(*reinterpret_cast<Vec2*>(uniform->GetData()));	break;
		case Uniform::kVec3:	Write<Vec3>(*reinterpret_cast<Vec3*>(uniform->GetData()));	break;
		case Uniform::kVec4:	Write<Vec4>(*reinterpret_cast<Vec4*>(uniform->GetData()));	break;
		case Uniform::kMat3:	Write<Mat3>(*reinterpret_cast<Mat3*>(uniform->GetData()));	break;
		case Uniform::kMat4:	Write<Mat4>(*reinterpret_cast<Mat4*>(uniform->GetData()));	break;*/
		default:				Rtt_ASSERT_NOT_REACHED();									break;
	}
}

static void
SetDynamicStateBit( uint8_t states[], uint8_t value )
{
	uint8_t offset = value;
	uint8_t byteIndex = offset / 8U;

	states[byteIndex] |= 1U << (offset - byteIndex * 8U);
}

void
VulkanCommandBuffer::InitializePipelineState()
{
/*
	glDisable( GL_SCISSOR_TEST );
*/
	PackedPipeline packedPipeline = {};

	// TODO: this should be fleshed out with defaults
	// these need not be relevant, but should properly handle being manually updated...

	packedPipeline.fBlendAttachmentCount = 1U;
	packedPipeline.fBlendAttachments[0].fEnable = VK_TRUE;
	packedPipeline.fBlendAttachments[0].fColorWriteMask = 0x0F;
	packedPipeline.fBlendAttachments[0].fSrcColorFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	packedPipeline.fBlendAttachments[0].fSrcAlphaFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	packedPipeline.fBlendAttachments[0].fDstColorFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	packedPipeline.fBlendAttachments[0].fDstAlphaFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

	SetDynamicStateBit( packedPipeline.fDynamicStates, VK_DYNAMIC_STATE_SCISSOR );
	SetDynamicStateBit( packedPipeline.fDynamicStates, VK_DYNAMIC_STATE_VIEWPORT );

	fDefaultPipeline = packedPipeline;

	RestartWorkingPipeline();
}

void
VulkanCommandBuffer::RestartWorkingPipeline()
{
	fShaderStageCreateInfo.clear();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

	fInputAssemblyStateCreateInfo = inputAssembly;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};

	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	fRasterizationStateCreateInfo = rasterizer;

	VkPipelineMultisampleStateCreateInfo multisampling = {};

	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

    fMultisampleStateCreateInfo = multisampling;

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};

	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

	fDepthStencilStateCreateInfo = depthStencil;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};

	colorBlendAttachment.srcAlphaBlendFactor = colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstAlphaBlendFactor = colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	fColorBlendAttachments.clear();
	fColorBlendAttachments.push_back( colorBlendAttachment );

	VkPipelineColorBlendStateCreateInfo colorBlending = {};

	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1U;
	fColorBlendStateCreateInfo = colorBlending;
}

void
VulkanCommandBuffer::ResolvePipeline()
{
	auto iter = fBuiltPipelines.find( fWorkingPipeline );
	VkPipeline pipeline = VK_NULL_HANDLE;

	if (iter == fBuiltPipelines.end())
	{
	/*
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
	*/
		VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};

        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		
        vertexInputInfo.pVertexAttributeDescriptions = fVertexAttributeDescriptions.data();
        vertexInputInfo.pVertexBindingDescriptions = fVertexBindingDescriptions.data();
        vertexInputInfo.vertexAttributeDescriptionCount = fVertexAttributeDescriptions.size();
        vertexInputInfo.vertexBindingDescriptionCount = fVertexBindingDescriptions.size();

		VkPipelineViewportStateCreateInfo viewportInfo = {};

		viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};

        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCreateInfo.basePipelineHandle = fFirstPipeline;
		pipelineCreateInfo.flags = fFirstPipeline != VK_NULL_HANDLE ? VK_PIPELINE_CREATE_DERIVATIVE_BIT : VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
        pipelineCreateInfo.pInputAssemblyState = &fInputAssemblyStateCreateInfo;
        pipelineCreateInfo.pColorBlendState = &fColorBlendStateCreateInfo;
        pipelineCreateInfo.pDepthStencilState = &fDepthStencilStateCreateInfo;
        pipelineCreateInfo.pMultisampleState = &fMultisampleStateCreateInfo;
        pipelineCreateInfo.pRasterizationState = &fRasterizationStateCreateInfo;
        pipelineCreateInfo.pStages = fShaderStageCreateInfo.data();
        pipelineCreateInfo.stageCount = fShaderStageCreateInfo.size();
        pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
		pipelineCreateInfo.pViewportState = &viewportInfo;
//      pipelineCreateInfo.layout = pipelineLayout;
//      pipelineCreateInfo.renderPass = renderPass;

		const VkAllocationCallbacks * allocator = fState->GetAllocationCallbacks();

        if (VK_SUCCESS == vkCreateGraphicsPipelines( fState->GetDevice(), fState->GetPipelineCache(), 1U, &pipelineCreateInfo, allocator, &pipeline ))
		{
			if (VK_NULL_HANDLE == fFirstPipeline)
			{
				fFirstPipeline = pipeline;
			}

			fBuiltPipelines[fWorkingPipeline] = pipeline;
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

	if (pipeline != VK_NULL_HANDLE && (VK_NULL_HANDLE == fBoundPipeline || pipeline != fBoundPipeline))
	{
		vkCmdBindPipeline( fCommands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	}

	fBoundPipeline = pipeline;
	fWorkingPipeline = fDefaultPipeline;
}

bool
VulkanCommandBuffer::PackedPipeline::operator < ( const PackedPipeline & other ) const
{
	return memcmp( this, &other, sizeof( PackedPipeline ) ) < 0;
}

bool
VulkanCommandBuffer::PackedPipeline::operator == ( const PackedPipeline & other ) const
{
	return !(*this < other) && !(other < *this);
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
