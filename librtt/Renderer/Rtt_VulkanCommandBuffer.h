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

#include <vulkan/vulkan.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class VulkanFrameBufferObject;
class VulkanProgram;
class VulkanRenderer;
class VulkanState;
struct DescriptorLists;
struct TimeTransform;

// cf. shell_default_vulkan:

struct alignas(16) VulkanUniforms
{
	float fData[6 * 4];
};

struct alignas(16) VulkanUserData
{
	float UserData[4][16];
};

struct alignas(16) VulkanPushConstants
{
	float fData[5 * 4];	// masks, time, sampler index
	float fUniforms[11 * 4];// uniform userdata (compact representation, i.e. <= 11 vectors)
};

// 
class VulkanCommandBuffer : public CommandBuffer
{
	public:
		typedef CommandBuffer Super;
		typedef VulkanCommandBuffer Self;

	public:
		VulkanCommandBuffer( Rtt_Allocator* allocator, VulkanRenderer & renderer );
		virtual ~VulkanCommandBuffer();

		virtual void Initialize();
		virtual void Denitialize();
		virtual void ClearUserUniforms();

		bool PrepareTexturesPool( VulkanState * state );

	private:
		void PopFrameBufferObject();
		void PushFrameBufferObject( FrameBufferObject * fbo );

	public:
		// Generate the appropriate buffered Vulkan commands to accomplish the
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
		
		virtual void WillRender();

		// Execute all buffered commands. A valid OpenGL context must be active.
		virtual Real Execute( bool measureGPU );
	
	private:	
		struct PushConstantState : public VulkanPushConstants {
			PushConstantState()
			{
				Reset();
			}

			void Reset();
			void UseFragmentStage() { stages |= VK_SHADER_STAGE_FRAGMENT_BIT; } // TODO: if using sampler index or "small userdata"...
			void Write( U32 offset, const void * src, size_t size );

			float * GetData( U32 offset );
			bool IsValid() const { return lowerOffset <= upperOffset; }
			U32 Offset() const { return lowerOffset; }
			U32 Range() const { return IsValid() ? upperOffset - lowerOffset + 4U * sizeof( float ) : 0U; }
			U32 Stages() const { return stages; }

			U32 lowerOffset;
			U32 upperOffset;
			U32 stages;
		};

	public:
		VkResult WaitAndAcquire( VkDevice device, VkSwapchainKHR swapchain, uint32_t & index );
		VkResult GetExecuteResult() const { return fExecuteResult; }
		
		void BeginFrame();
		void BeginRecording( VkCommandBuffer commandBuffer, DescriptorLists * lists );
		void ClearExecuteResult() { fExecuteResult = VK_SUCCESS; }
		bool PrepareDraw( VkPrimitiveTopology topology, std::vector< VkDescriptorImageInfo > & imageInfo, PushConstantState & pushConstants );

	public:
		VkDescriptorSet AddTextureSet( const std::vector< VkDescriptorImageInfo > & imageInfo );

	private:
		virtual void InitializeFBO();
		virtual void InitializeCachedParams();
		virtual void CacheQueryParam( CommandBuffer::QueryableParams param );

	private:
		// Templatized helper function for reading an arbitrary argument from
		// the command buffer.
		template <typename T>
		T Read();

		// Templatized helper function for writing an arbitrary argument to the
		// command buffer.
		template <typename T>
		void Write(T);

	private:
		struct UniformUpdate
		{
			Uniform* uniform;
			U32 timestamp;
		};

		void ApplyUniforms( GPUResource* resource );
		void ApplyPushConstant( Uniform * uniform, size_t offset, size_t translationOffset, VulkanProgram * program = NULL, U32 index = ~0U );
		void ApplyUniform( VulkanProgram & vulkanProgram, U32 index );
		void WriteUniform( Uniform* uniform );
		U8 * PointToUniform( U32 index, size_t offset );

		DescriptorLists & ListsForIndex( U32 index );

		UniformUpdate fUniformUpdates[Uniform::kNumBuiltInVariables];

		Program::Version fCurrentPrepVersion;
		Program::Version fCurrentDrawVersion;
		
	private:
		Program* fProgram;
		FrameBufferObject * fDefaultFBO;/*
		U32* fTimerQueries;
		U32 fTimerQueryIndex;*/
		Real fElapsedTimeGPU;
		TimeTransform* fTimeTransform;
		S32 fCachedQuery[kNumQueryableParams];
		VulkanRenderer & fRenderer;
		VkSemaphore fImageAvailableSemaphore;
		VkSemaphore fRenderFinishedSemaphore;
		VkFence fInFlight;

		// non-owned, retained only for frame:
		// (some of this could go on the stack)
		DescriptorLists * fLists;
		VkCommandBuffer fCommandBuffer;
		VkPipeline fPipeline;

		struct FBONode {
			FrameBufferObject * fFBO;
			U8 * fOldBuffer;
			U32 fOldBytesAllocated;
			U32 fOldBytesUsed;
			U32 fOldNumCommands;
		};

		struct OffscreenNode {
			U8 * fBuffer;
			U32 fBytesAllocated;
			U32 fNumCommands;
		};

		std::vector< FBONode > fFBOStack;
		std::vector< OffscreenNode > fOffscreenSequence;
		Geometry * fCurrentGeometry;
//		dynamic uniform buffers - as a list?
		VkSwapchainKHR fSwapchain;
		VkResult fExecuteResult;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanCommandBuffer_H__
