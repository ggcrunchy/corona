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

class VulkanRenderer;
class VulkanState;
struct DescriptorPoolList;
struct TimeTransform;

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
		
		void BeginRecording( VkCommandBuffer commandBuffer, DescriptorPoolList * descriptorPoolList );
		void ClearExecuteResult() { fExecuteResult = VK_SUCCESS; }
		void PrepareDraw( VkPrimitiveTopology topology );

	public:
		void AddGraphicsPipeline( VkPipeline pipeline );

	private:
		virtual void InitializeFBO();
		virtual void InitializeCachedParams();
		virtual void CacheQueryParam( CommandBuffer::QueryableParams param );

	private:
		struct UniformUpdate
		{
			Uniform* uniform;
			U32 timestamp;
		};
		
		void ApplyUniforms( GPUResource* resource );
		void ApplyUniform( GPUResource* resource, U32 index );

		UniformUpdate fUniformUpdates[Uniform::kNumBuiltInVariables];

		Program::Version fCurrentPrepVersion;
		
	private:/*
		Program* fProgram;
		S32 fDefaultFBO;
		U32* fTimerQueries;
		U32 fTimerQueryIndex;*/
		Real fElapsedTimeGPU;
		TimeTransform* fTimeTransform;
		S32 fCachedQuery[kNumQueryableParams];
		VulkanRenderer & fRenderer;
		VkSemaphore fImageAvailableSemaphore;
		VkSemaphore fRenderFinishedSemaphore;
		VkFence fInFlight;
		std::vector< U8 > fDynamicUBO;
		std::vector< U8 > fDynamicUserData;
		size_t fDynamicBufferAlignment;
		uint32_t fUBOIndex;
		uint32_t fUserDataIndex;

		// reset at start of frame and after draws:
		bool fUpdatedUniformBuffer;
		bool fUpdatedUserData;
		S32 fLowerPushConstantVector;
		S32 fUpperPushConstantVector;

		// non-owned, retained only for frame:
		DescriptorPoolList * fDescriptorPoolList;
		VkCommandBuffer fCommandBuffer;
		VkSwapchainKHR fSwapchain;
		VkResult fExecuteResult;
		uint32_t fImageIndex;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanCommandBuffer_H__
