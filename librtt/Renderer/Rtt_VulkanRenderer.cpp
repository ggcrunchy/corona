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

DynamicUniformData::DynamicUniformData()
:	fBufferData( NULL ),
	fMapped( NULL )
{
}

DynamicUniformData::~DynamicUniformData()
{
	if (fMapped)
	{
//		vkUnmapMemory( device, fData->mMemory );
	}

	Rtt_DELETE( fBufferData );
}

DescriptorLists::DescriptorLists( VulkanState * state, VkDescriptorSetLayout setLayout, U32 count, size_t size )
:	fSetLayout( setLayout ),
	fDynamicAlignment( 0U ),
	fBufferIndex( ~0U ),
	fWorkspace( NULL ),
	fRawSize( size ),
	fResetPools( false )
{
	VkDeviceSize alignment = state->GetProperties().limits.minUniformBufferOffsetAlignment;

	fDynamicAlignment = fRawSize;

	if (alignment > 0U)
	{
		fDynamicAlignment = (fDynamicAlignment + alignment - 1) & ~(alignment - 1);
	}

	fBufferSize = U32( count * fDynamicAlignment );

	Reset( VK_NULL_HANDLE );
}

DescriptorLists::DescriptorLists( VkDescriptorSetLayout setLayout, bool resetPools )
:	fSetLayout( setLayout ),
	fDynamicAlignment( 0U ),
	fBufferIndex( ~0U ),
	fBufferSize( 0U ),
	fWorkspace( NULL ),
	fRawSize( 0U ),
	fResetPools( resetPools )
{
	Reset( VK_NULL_HANDLE );
}

bool
DescriptorLists::AddBuffer( VulkanState * state )
{
	VulkanBufferData bufferData( state->GetDevice(), state->GetAllocator() );
	
	if (state->CreateBuffer( fBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT/*VISIBLE_BIT*/, bufferData ))
	{
		fDynamicUniforms.push_back( DynamicUniformData{} );

		DynamicUniformData & uniformData = fDynamicUniforms.back();
		
		uniformData.fMapped = state->MapData( bufferData.GetMemory() );
		uniformData.fBufferData = bufferData.Extract( NULL );

		return true;
	}

	return false;
}

bool
DescriptorLists::AddPool( VulkanState * state, VkDescriptorType type, U32 descriptorCount, U32 maxSets, VkDescriptorPoolCreateFlags flags )
{
	VkDescriptorPoolSize poolSize;

	poolSize.descriptorCount = descriptorCount;
	poolSize.type = type;

	VkDescriptorPoolCreateInfo poolInfo = {};

	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = flags;
	poolInfo.maxSets = maxSets;
	poolInfo.poolSizeCount = 1U;
	poolInfo.pPoolSizes = &poolSize;

	VkDescriptorPool pool = VK_NULL_HANDLE;

	if (VK_SUCCESS == vkCreateDescriptorPool( state->GetDevice(), &poolInfo, state->GetAllocator(), &pool ))
	{
		fPools.push_back( pool );

		return true;
	}

	else
	{
		CoronaLog( "Failed to create descriptor pool!" );

		return false;
	}
}

bool
DescriptorLists::EnsureAvailability( VulkanState * state )
{
/*
	TODO (probably once working properly?)

	U32 total = fUpdateCount * fDynamicAlignment;

	do these as we record commands? (if we submit uniform commands, set flag; increment if true when recording draw command)
	we want to allocate enough new sets to accommodate this total, or fail
	doling them out should go into a separate function

	alternatively could replace with larger buffer if possible, but of course could suddenly fail...
*/

	bool ok = PreparePool( state ), newSet = false;

	if ( ok && ( NoBuffers() || IsBufferFull() ) )
	{
		ok = AddBuffer( state );

		if (ok)
		{
			++fBufferIndex; // n.b. overflows to 0 first time

			fOffset = 0U;

			newSet = fBufferIndex == fSets.size();
		}
	}
	
	if (newSet)
	{
		VkDescriptorSetAllocateInfo allocInfo = {};

		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = fPools.back();
		allocInfo.descriptorSetCount = 1U;
		allocInfo.pSetLayouts = &fSetLayout;
			
		VkDescriptorSet set = VK_NULL_HANDLE;

		vkAllocateDescriptorSets( state->GetDevice(), &allocInfo, &set );

		ok = set != VK_NULL_HANDLE;

		if (ok)
		{
			VkWriteDescriptorSet descriptorWrite = {};
			VkDescriptorBufferInfo bufferInfo = {};

			bufferInfo.buffer = fDynamicUniforms[fBufferIndex].fBufferData->GetBuffer();
			bufferInfo.range = fBufferSize;

			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.descriptorCount = 1U;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			descriptorWrite.dstSet = set;
			descriptorWrite.pBufferInfo = &bufferInfo;

			vkUpdateDescriptorSets( state->GetDevice(), 1U, &descriptorWrite, 0U, NULL );
				
			fSets.push_back( set );
		}
	}

	return ok;
}

bool 
DescriptorLists::PreparePool( VulkanState * state )
{
	return !fPools.empty() || AddPool( state, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1U, 1U );
}

void
DescriptorLists::Reset( VkDevice device, void * workspace )
{
	if (fResetPools)
	{
		for (VkDescriptorPool & pool : fPools)
		{
			vkResetDescriptorPool( device, pool, 0 );
		}

		fSets.clear();
	}

	fWorkspace = static_cast< U8 * >( workspace );
	fOffset = 0U;
	fDirty = false;

	if (!NoBuffers())
	{
		fBufferIndex = 0U;
	}
}

bool
DescriptorLists::IsMaskPushConstant( int index )
{
	return index >= Uniform::kMaskMatrix0 && index <= Uniform::kMaskMatrix2;
}

bool
DescriptorLists::IsPushConstant( int index )
{
	return Uniform::kTotalTime == index || IsMaskPushConstant( index ); // TODO: others, e.g. SamplerIndex?
}

bool
DescriptorLists::IsUserData( int index )
{
	return index >= Uniform::kUserData0;
}

VulkanRenderer::VulkanRenderer( Rtt_Allocator* allocator, VulkanState * state )
:   Super( allocator ),
	fState( state ),
    fSwapchainTexture( NULL ),
	fPrimaryFBO( NULL ),
	fFirstPipeline( VK_NULL_HANDLE ),
	fUniformsLayout( VK_NULL_HANDLE ),
	fUserDataLayout( VK_NULL_HANDLE ),
	fTextureLayout( VK_NULL_HANDLE ),
	fPipelineLayout( VK_NULL_HANDLE ),
	fPipelineCreateInfo( state )
{
	fFrontCommandBuffer = Rtt_NEW( allocator, VulkanCommandBuffer( allocator, *this ) );
	fBackCommandBuffer = Rtt_NEW( allocator, VulkanCommandBuffer( allocator, *this ) );

	VkPushConstantRange pushConstantRange;

	pushConstantRange.offset = 0U;
	pushConstantRange.size = sizeof( VulkanPushConstants );
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

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

	InitializePipelineState();
}

VulkanRenderer::~VulkanRenderer()
{
	vkQueueWaitIdle( fState->GetGraphicsQueue() );

	TearDownSwapchain();

	Rtt_DELETE( fPrimaryFBO );
	Rtt_DELETE( fSwapchainTexture );
	Rtt_DELETE( fState );
}

void
VulkanRenderer::BeginFrame( Real totalTime, Real deltaTime, Real contentScaleX, Real contentScaleY )
{
	VulkanCommandBuffer * vulkanCommandBuffer = static_cast< VulkanCommandBuffer * >( fBackCommandBuffer );
	VkResult result = vulkanCommandBuffer->GetExecuteResult();
	bool canContinue = VK_SUCCESS == result;
	VkSwapchainKHR swapchain = fState->GetSwapchain();
	
	if (VK_NULL_HANDLE == swapchain)
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

	if (canContinue)
	{
		result = vulkanCommandBuffer->WaitAndAcquire( fState->GetDevice(), swapchain );
	CoronaLog( "ACQUIRE: %i", result );
		canContinue = VK_SUCCESS == result || VK_SUBOPTIMAL_KHR == result;
	}

	if (fPrimaryFBO)
	{
		SetFrameBufferObject( fPrimaryFBO );
	}

	Super::BeginFrame( totalTime, deltaTime, contentScaleX, contentScaleY );

	if (canContinue)
	{
		uint32_t index = vulkanCommandBuffer->GetImageIndex();

		vulkanCommandBuffer->BeginRecording( fCommandBuffers[index], fDescriptorLists.data() + 3U * index );
		CoronaLog("OK");
	}

	else
	{
		if (VK_ERROR_OUT_OF_DATE_KHR == result)
		{
		//	fState->RebuildSwapChain();
			// TODO: should we then try again?
			// or can we just do that straight out?
				// if an acquire fails, then what????
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

	// TODO: this always precedes Swap(), so hook up resources for Create() / Update()
	// yank them in Execute()
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
	
	fCommandBuffers.resize( imageCount );
	fSwapchainImages.resize( imageCount );

	fDescriptorLists.clear();

	VulkanState * state = GetState();

	for (uint32_t i = 0; i < imageCount; ++i)
	{
		static_assert( DescriptorLists::kUniforms < DescriptorLists::kUserData, "UBOs in unexpected order" );
		static_assert( DescriptorLists::kUserData < DescriptorLists::kTexture, "UBO / textures in unexpected order" );

		fDescriptorLists.push_back( DescriptorLists( state, fUniformsLayout, 4096U, sizeof( VulkanUniforms ) ) );
		fDescriptorLists.push_back( DescriptorLists( state, fUserDataLayout, 1024U, sizeof( VulkanUserData ) ) );
		fDescriptorLists.push_back( DescriptorLists( fTextureLayout, true ) );
	}

	VkCommandBufferAllocateInfo allocInfo = {};

    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandBufferCount = imageCount;
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

	VkSwapchainKHR newSwapchain = MakeSwapchain();

	TearDownSwapchain();

	if (newSwapchain != VK_NULL_HANDLE )
	{
		BuildUpSwapchain( newSwapchain );
	}
}

void
VulkanRenderer::TearDownSwapchain()
{
	// TODO: tell frame buffers they're invalid, e.g. iterate list

    const VkAllocationCallbacks * allocator = fState->GetAllocator();
    VkDevice device = fState->GetDevice();

	vkDestroySwapchainKHR( device, fState->GetSwapchain(), allocator );

	// TODO: lots more stuff...

	vkFreeCommandBuffers( device, fState->GetCommandPool(), fCommandBuffers.size(), fCommandBuffers.data() );

    fState->SetSwapchain( VK_NULL_HANDLE );
}

const size_t kFinalBlendFactor = VK_BLEND_OP_MAX;
const size_t kFinalBlendOp = VK_BLEND_OP_MAX;

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

		const VkAllocationCallbacks * allocator = fState->GetAllocator();
// TODO: can we use another queue to actually build this while still recording commands?
        if (VK_SUCCESS == vkCreateGraphicsPipelines( fState->GetDevice(), fState->GetPipelineCache(), 1U, &pipelineCreateInfo, allocator, &pipeline ))
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

//	fWorkingKey = fDefaultKey;

	return pipeline;
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

	packedPipeline.fRasterSamplesFlags = 1U;
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

	fWorkingKey = fDefaultKey;

	RestartWorkingPipeline();
}

void
VulkanRenderer::RestartWorkingPipeline()
{
    new (&fPipelineCreateInfo) PipelineCreateInfo( fState );
}

VulkanRenderer::PipelineCreateInfo::PipelineCreateInfo( VulkanState * state )
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
	multisampling.rasterizationSamples = VkSampleCountFlagBits( state->GetSampleCountFlags() );

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
	return memcmp( fContents.data(), other.fContents.data(), sizeof( PipelineKey ) ) < 0;
}

bool
VulkanRenderer::PipelineKey::operator == ( const PipelineKey & other ) const
{
	return !(*this < other) && !(other < *this);
}

/*
    void createUniformBuffers() {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        uniformBuffers.resize(swapChainImages.size());
        uniformBuffersMemory.resize(swapChainImages.size());

        for (size_t i = 0; i < swapChainImages.size(); i++) {
            createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffers[i], uniformBuffersMemory[i]);
        }
    }

    void updateUniformBuffer(uint32_t currentImage) {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        UniformBufferObject ubo = {};
        ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), swapChainExtent.width / (float) swapChainExtent.height, 0.1f, 10.0f);
        ubo.proj[1][1] *= -1;

        void* data;
        vkMapMemory(device, uniformBuffersMemory[currentImage], 0, sizeof(ubo), 0, &data);
            memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(device, uniformBuffersMemory[currentImage]);
    }

    void drawFrame() {
		// BeginFrame()

        updateUniformBuffer(imageIndex);

		// Rest
    }

    void createColorResources() {
        VkFormat colorFormat = swapChainImageFormat;

        createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage, colorImageMemory);
        colorImageView = createImageView(colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

        transitionImageLayout(colorImage, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1);
    }

    void createDepthResources() {
        VkFormat depthFormat = findDepthFormat();

        createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
        depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

        transitionImageLayout(depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
    }

    VkFormat findDepthFormat() {
        return findSupportedFormat(
            {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }
*/

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
