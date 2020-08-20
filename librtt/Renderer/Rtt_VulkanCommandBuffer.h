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

struct VulkanUBO {
	alignas(16) float fData[6 * 4];
};

struct VulkanUserDataUBO {
	alignas(16) float UserData[4][16];
};

struct VulkanPushConstants {
	enum { kVectorCount = 5 };

	alignas(16) float fData[kVectorCount * 4];
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

		bool PreparePool( VulkanState * state, DescriptorLists & lists );
		bool PrepareTexturesPool( VulkanState * state );

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
	
	public:
		VkResult WaitAndAcquire( VkDevice device, VkSwapchainKHR swapchain );
		VkResult GetExecuteResult() const { return fExecuteResult; }
		uint32_t GetImageIndex() const { return fImageIndex; }
		
		void BeginRecording( VkCommandBuffer commandBuffer, DescriptorLists * lists );
		void ClearExecuteResult() { fExecuteResult = VK_SUCCESS; }
		void PrepareDraw( VkPrimitiveTopology topology );

		void SubmitFBO( VulkanFrameBufferObject * fbo );
		void CommitFBO();

	public:
		void AddGraphicsPipeline( VkPipeline pipeline );

	public:
		VkDescriptorSet AddTexture( U32 unit, const VkDescriptorImageInfo & imageInfo );

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

		struct PushConstantState : public VulkanPushConstants {
			PushConstantState()
			{
				Reset();
			}

			void Reset();
			void ClaimOffsets( U32 offset1, U32 offset2 );

			static U32 VectorOffset( U32 offset );

			float * GetData( U32 offset );
			bool IsValid() const { return lowerOffset < kVectorCount * 4; }
			U32 Offset() const { return lowerOffset; }
			U32 Range() const { return IsValid() ? upperOffset - lowerOffset + 4U : 0U; }

			U32 lowerOffset;
			U32 upperOffset;
		};

		struct UniformsToWrite {
			VkDeviceMemory memory;
			U32 offset;
			U8 * data;
		};

		struct UniformUpdate
		{
			Uniform* uniform;
			U32 timestamp;
		};
		
		void ApplyUniforms( GPUResource* resource );
		void ApplyPushConstant( Uniform * uniform, size_t offset, size_t translationOffset );
		void ApplyUniform( VulkanProgram & vulkanProgram, U32 index );
		void ReadUniform( const UniformsToWrite & utw, const void * value, size_t offset, size_t size );
		UniformsToWrite PointToUniform( U32 index, size_t offset );

		UniformUpdate fUniformUpdates[Uniform::kNumBuiltInVariables];

		Program::Version fCurrentPrepVersion;
		Program::Version fCurrentDrawVersion;
		
	private:
		Program* fProgram;
		FrameBufferObject * fDefaultFBO;/*
		U32* fTimerQueries;
		U32 fTimerQueryIndex;*/
		VulkanFrameBufferObject * fFBO;
		Real fElapsedTimeGPU;
		TimeTransform* fTimeTransform;
		S32 fCachedQuery[kNumQueryableParams];
		VulkanRenderer & fRenderer;
		VkSemaphore fImageAvailableSemaphore;
		VkSemaphore fRenderFinishedSemaphore;
		VkFence fInFlight;

		// non-owned, retained only for frame:
		DescriptorLists * fLists;
		VkCommandBuffer fCommandBuffer;
		VkDescriptorSet fTextures;
		std::vector< VkDescriptorImageInfo > fTextureState;
/*
		dynamic uniform buffers - as a list?

*/
		std::vector< VkMappedMemoryRange > fMappedMemoryRanges;
		PushConstantState fPushConstants;

		VkSwapchainKHR fSwapchain;
		VkResult fExecuteResult;
		uint32_t fImageIndex;
		bool fCommitted;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanCommandBuffer_H__
