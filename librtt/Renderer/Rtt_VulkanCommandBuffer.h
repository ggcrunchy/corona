//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_VulkanCommandBuffer_H__
#define _Rtt_VulkanCommandBuffer_H__

#include "Renderer/Rtt_CommandBuffer.h"
#include "Renderer/Rtt_Uniform.h"

#include <map>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

constexpr int BitsNeeded( int x ) // n.b. x > 0
{
	int result = 0;

	for (int power = 1; power <= x; power *= 2)
	{
		++result;
	}

	return result;
}

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

struct TimeTransform;

// 
class VulkanCommandBuffer : public CommandBuffer
{
	public:
		typedef CommandBuffer Super;
		typedef VulkanCommandBuffer Self;

	public:
		VulkanCommandBuffer( Rtt_Allocator* allocator );
		virtual ~VulkanCommandBuffer();

		virtual void Initialize();
		
		virtual void Denitialize();

		virtual void ClearUserUniforms();

		// Generate the appropriate buffered OpenGL commands to accomplish the
		// specified state changes.
		virtual void BindFrameBufferObject( FrameBufferObject* fbo );
		virtual void BindGeometry( Geometry* geometry );
		virtual void BindTexture( Texture* texture, U32 unit );
		virtual void BindUniform( Uniform* uniform, U32 unit );
		virtual void BindProgram( Program* program, Program::Version version);
		virtual void SetBlendEnabled( bool enabled );
		virtual void SetBlendFunction( const BlendMode& mode );
		virtual void SetBlendEquation( RenderTypes::BlendEquation mode );
		virtual void SetViewport( int x, int y, int width, int height );
		virtual void SetScissorEnabled( bool enabled );
		virtual void SetScissorRegion( int x, int y, int width, int height );
		virtual void SetMultisampleEnabled( bool enabled );
		virtual void Clear( Real r, Real g, Real b, Real a );
		virtual void Draw( U32 offset, U32 count, Geometry::PrimitiveType type );
		virtual void DrawIndexed( U32 offset, U32 count, Geometry::PrimitiveType type );
		virtual S32 GetCachedParam( CommandBuffer::QueryableParams param );
		
		// Execute all buffered commands. A valid OpenGL context must be active.
		virtual Real Execute( bool measureGPU );
	
	private:
		virtual void InitializeFBO();
		virtual void InitializeCachedParams();
		virtual void CacheQueryParam( CommandBuffer::QueryableParams param );
		
	private:
	/*
		// Templatized helper function for reading an arbitrary argument from
		// the command buffer.
		template <typename T>
		T Read();

		// Templatized helper function for writing an arbitrary argument to the
		// command buffer.
		template <typename T>
		void Write(T);
		*/
		struct UniformUpdate
		{
			Uniform* uniform;
			U32 timestamp;
		};
		
		void ApplyUniforms( GPUResource* resource );
		void ApplyUniform( GPUResource* resource, U32 index );
		void WriteUniform( Uniform* uniform );

		UniformUpdate fUniformUpdates[Uniform::kNumBuiltInVariables];

		Program::Version fCurrentPrepVersion;
		Program::Version fCurrentDrawVersion;
	/*
		Program* fProgram;
		S32 fDefaultFBO;
		U32* fTimerQueries;
		U32 fTimerQueryIndex;*/
		Real fElapsedTimeGPU;
		TimeTransform* fTimeTransform;/*
		S32 fCachedQuery[kNumQueryableParams];
		*/

		struct PackedBlendAttachment {
			U32 fEnable : 1;
			U32 fSrcColorFactor : BitsNeeded( VK_BLEND_FACTOR_RANGE_SIZE );
			U32 fDstColorFactor : BitsNeeded( VK_BLEND_FACTOR_RANGE_SIZE );
			U32 fColorOp : BitsNeeded( VK_BLEND_OP_RANGE_SIZE );
			U32 fSrcAlphaFactor : BitsNeeded( VK_BLEND_FACTOR_RANGE_SIZE );
			U32 fDstAlphaFactor : BitsNeeded( VK_BLEND_FACTOR_RANGE_SIZE );
			U32 fAlphaOp : BitsNeeded( VK_BLEND_OP_RANGE_SIZE );
			U32 fColorWriteMask : 4;
		};

		struct PackedVertexAttributeDescription {
			U64 fLocation : 8; // TODO: compare these against device limits?
			U64 fBinding: 8;
			U64 fFormat : BitsNeeded( VK_FORMAT_RANGE_SIZE );
			U64 fOffset : 8;
		};

		struct PackedVertexBindingDescription {
			U32 fBinding : 8; // TODO: ditto...
			U32 fStride : 10;
			U32 fInputRate : BitsNeeded( VK_VERTEX_INPUT_RATE_RANGE_SIZE );
		};

		struct PackedPipeline {
			U64 fDynamicStates : BitsNeeded( VK_DYNAMIC_STATE_RANGE_SIZE );
            U64 fTopology : BitsNeeded( VK_PRIMITIVE_TOPOLOGY_RANGE_SIZE );
            U64 fPrimitiveRestartEnable : 1;
			U64 fRasterizerDiscardEnable : 1;
			U64 fPolygonMode : BitsNeeded( VK_POLYGON_MODE_RANGE_SIZE );
			U64 fLineWidth : 4; // lineWidth = (X + 1) / 16
			U64 fCullMode : 2;
			U64 fFrontFace : BitsNeeded( VK_FRONT_FACE_RANGE_SIZE );
			U64 fRasterSamplesFlags : 7;
			U64 fSampleShadingEnable : 1;
			U64 fSampleShading : 5; // minSampleShading = X / 32
			U64 fAlphaToCoverageEnable : 1;
			U64 fAlphaToOneEnable : 1;
			U64 fDepthTestEnable : 1;
			U64 fDepthWriteEnable : 1;
			U64 fDepthCompareOp : BitsNeeded( VK_COMPARE_OP_RANGE_SIZE );
			U64 fDepthBoundsTestEnable : 1;
			U64 fStencilTestEnable : 1;
			U64 fFront : BitsNeeded( VK_STENCIL_OP_RANGE_SIZE );
			U64 fBack : BitsNeeded( VK_STENCIL_OP_RANGE_SIZE );
			U64 fMinDepthBounds : 5; // minDepthBounds = X / 32
			U64 fMaxDepthBounds : 5; // maxDepthBounds = (X + 1) / 32
			U64 fLogicOpEnable : 1;
			U64 fLogicOp : BitsNeeded( VK_LOGIC_OP_RANGE_SIZE );
			U64 fBlendConstant1 : 4; // blendConstants = X / 15
			U64 fBlendConstant2 : 4;
			U64 fBlendConstant3 : 4;
			U64 fBlendConstant4 : 4;
			U64 fBlendAttachmentCount : 3; // 0-7
			U64 fVertexAttributeDescriptionCount : 2; // 0-3
			U64 fVertexAttributeBindingCount : 2; // 0-3
			PackedBlendAttachment fBlendAttachments[8];
			PackedVertexAttributeDescription fAttributeDescriptions[4];
			PackedVertexBindingDescription fBindingDescriptions[4];

			bool operator < ( const PackedPipeline & other );
		};

		std::map< PackedPipeline, VkPipeline > fCachedPipelines;
		std::vector< VkDynamicState > fDynamicState;
		std::vector< VkPipelineShaderStageCreateInfo > fShaderStageCreateInfo;
		PackedPipeline fDefaultPipeline;
		PackedPipeline fWorkingPipeline;
	/*
		std::vector< VkSpecializationInfo > fSpecializationInfo;
		std::vector< VkSpecializationMapEntry > fSpecializationMapEntry;
	*/
		std::vector< VkVertexInputAttributeDescription> fVertexAttributeDescriptions;
        std::vector< VkVertexInputBindingDescription > fVertexBindingDescriptions;
        VkRect2D fScissorRect;
        VkViewport fViewport;
};

/*

    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<VkDescriptorSet> descriptorSets;

    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    size_t currentFrame = 0;
*/

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanCommandBuffer_H__
