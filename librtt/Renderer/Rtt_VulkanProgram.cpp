//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_VulkanState.h"
#include "Renderer/Rtt_VulkanProgram.h"

#include "Renderer/Rtt_CommandBuffer.h"
#include "Renderer/Rtt_Geometry_Renderer.h"
#include "Renderer/Rtt_Texture.h"
#ifdef Rtt_USE_PRECOMPILED_SHADERS
	#include "Renderer/Rtt_ShaderBinary.h"
	#include "Renderer/Rtt_ShaderBinaryVersions.h"
#endif
#include "Core/Rtt_Assert.h"
#include "CoronaLog.h"

#include <shaderc/shaderc.h>
#include <string>
#include <stdlib.h>
/*
#include <cstdio>
#include <string.h> // memset.
#ifdef Rtt_WIN_PHONE_ENV
	#include <GLES2/gl2ext.h>
#endif
*/	

// To reduce memory consumption and startup cost, defer the
// creation of Vulkan shaders and programs until they're needed.
// Depending on usage, this could result in framerate dips.
#define DEFER_VK_CREATION 1

// ----------------------------------------------------------------------------

namespace /*anonymous*/
{
	using namespace Rtt;
	/*
	// Check that the given shader compiled and log any errors
	void CheckShaderCompilationStatus( GLuint name, bool isVerbose, const char *label, int startLine )
	{
		if ( isVerbose )
		{
			if ( label )
			{
				Rtt_LogException( "ERROR: An error occurred in the %s kernel.\n", label );
			}
			Rtt_LogException( "%s", infoLog );
			Rtt_LogException( "\tNOTE: Kernel starts at line number (%d), so subtract that from the line numbers above.\n", startLine );
		}
	}

	// Check that the given program linked and log any errors
	void CheckProgramLinkStatus( GLuint name, bool isVerbose )
	{
		if ( isVerbose )
		{
			Rtt_LogException( "%s", infoLog );
		}
		else
		{
			Rtt_LogException(
				"ERROR: A shader failed to compile. To see errors, add the following to the top of your main.lua:\n"
				"\tdisplay.setDefault( 'isShaderCompilerVerbose', true )\n" );
		}
	}
	*/
	const char* kWireframeSource =
		"void main()" \
		"{" \
			"gl_FragColor = vec4(1.0);" \
		"}";
}

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

VulkanProgram::VulkanProgram( VulkanState * state )
:	fState( state )
{
	for( U32 i = 0; i < Program::kNumVersions; ++i )
	{
		Reset( fData[i] );
	}
}

void 
VulkanProgram::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kProgram == resource->GetType() );
	fResource = resource;
	
	#if !DEFER_VK_CREATION
		for( U32 i = 0; i < kMaximumMaskCount + 1; ++i )
		{
			Create( fData[i], i );
		}
	#endif
}

void
VulkanProgram::Update( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kProgram == resource->GetType() );
	if( fData[Program::kMaskCount0].IsValid() ) Update( Program::kMaskCount0, fData[Program::kMaskCount0] );
	if( fData[Program::kMaskCount1].IsValid() ) Update( Program::kMaskCount1, fData[Program::kMaskCount1] );
	if( fData[Program::kMaskCount2].IsValid() ) Update( Program::kMaskCount2, fData[Program::kMaskCount2] );
	if( fData[Program::kMaskCount3].IsValid() ) Update( Program::kMaskCount3, fData[Program::kMaskCount3] );
	if( fData[Program::kWireframe].IsValid() ) Update( Program::kWireframe, fData[Program::kWireframe]);
}

void 
VulkanProgram::Destroy()
{
	const VkAllocationCallbacks * allocator = fState->GetAllocationCallbacks();
	VkDevice device = fState->GetDevice();

	for( U32 i = 0; i < Program::kNumVersions; ++i )
	{
		VersionData& data = fData[i];
		if( data.IsValid() )
		{
#ifndef Rtt_USE_PRECOMPILED_SHADERS
			vkDestroyShaderModule( device, data.fVertexShader, allocator );
			vkDestroyShaderModule( device, data.fFragmentShader, allocator );
#endif
			Reset( data );
		}
	}
}

VulkanProgram::PipelineStages
VulkanProgram::Bind( Program::Version version )
{
	VersionData& data = fData[version];
	
	#if DEFER_VK_CREATION
		if( !data.IsValid() )
		{
			Create( version, data );
		}
	#endif

	PipelineStages stageCreateInfo( data.fShadersID );

	VkPipelineShaderStageCreateInfo shaderStageInfo = {};

	shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfo.pName = "main";

	shaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStageInfo.module = data.fVertexShader;

	stageCreateInfo.fStages.push_back( shaderStageInfo );

	shaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStageInfo.module = data.fFragmentShader;

	stageCreateInfo.fStages.push_back( shaderStageInfo );

	return stageCreateInfo;
}

U32 VulkanProgram::sID;

void
VulkanProgram::Create( Program::Version version, VersionData& data )
{
	Update( version, data );

	if (data.IsValid())
	{
		data.fShadersID = sID++;
	}
}

static int
CountLines( const char **segments, int numSegments )
{
	int result = 0;

	for ( int i = 0; i < numSegments; i++ )
	{
		result += Program::CountLines( segments[i] );
	}

	return result;
}

void
VulkanProgram::Compile( int ikind, const char * sources[], int sourceCount, Maps & maps, VkShaderModule & module )
{
	std::string code;

	for (int i = 0; i < sourceCount; ++i)
	{
		code += sources[i];
	}
	
	shaderc_shader_kind kind = shaderc_shader_kind( ikind );
	const char * what = shaderc_vertex_shader == kind ? "vertex shader" : "fragment shader";
	shaderc_compilation_result_t result = shaderc_compile_into_spv( fState->GetCompiler(), code.data(), code.size(), shaderc_vertex_shader, what, "main", fState->GetCompileOptions() );
	shaderc_compilation_status status = shaderc_result_get_compilation_status( result );

	if (shaderc_compilation_status_success == status)
	{
		const uint32_t * ir = reinterpret_cast< const uint32_t * >( shaderc_result_get_bytes( result ) );
		VkShaderModuleCreateInfo createShaderModuleInfo = {};

		createShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createShaderModuleInfo.codeSize = shaderc_result_get_length( result );
		createShaderModuleInfo.pCode = ir;

		spirv_cross::CompilerGLSL comp( ir, createShaderModuleInfo.codeSize / 4U );
		spirv_cross::ShaderResources resources = comp.get_shader_resources();

		for (auto & uniform : resources.uniform_buffers)
		{
			for (auto & range : comp.get_active_buffer_ranges( uniform.id ))
			{
				std::string name = comp.get_member_name( uniform.base_type_id, range.index );

				maps.uniforms[name] = std::make_pair( range.offset, range.range );
				// TODO: compare, if found in both?
			}
		}

		for (auto & sampler : resources.sampled_images)
		{
			const spirv_cross::SPIRType & type = comp.get_type_from_variable( sampler.id );

			maps.samplers[sampler.name] = type.image;
			// TODO: ditto (although far less likely in vertex shader)
		}

		VkShaderModule shaderModule;

		if (VK_SUCCESS == vkCreateShaderModule( fState->GetDevice(), &createShaderModuleInfo, nullptr, &shaderModule ))
		{
			module = shaderModule;
		}

		else
		{
			CoronaLog( "Failed to create shader module!" );
		}
	}

	else
	{
		CoronaLog( "Failed to compile %s:\n\n%s", what, shaderc_result_get_error_message( result ) );
	}

	shaderc_result_release( result );
}

VulkanProgram::Maps
VulkanProgram::UpdateShaderSource( Program* program, Program::Version version, VersionData& data )
{
	Maps maps;

#ifndef Rtt_USE_PRECOMPILED_SHADERS
	char maskBuffer[] = "#define MASK_COUNT 0\n";
	switch( version )
	{
		case Program::kMaskCount1:	maskBuffer[sizeof( maskBuffer ) - 3] = '1'; break;
		case Program::kMaskCount2:	maskBuffer[sizeof( maskBuffer ) - 3] = '2'; break;
		case Program::kMaskCount3:	maskBuffer[sizeof( maskBuffer ) - 3] = '3'; break;
		default: break;
	}

	const char *program_header_source = program->GetHeaderSource();
	const char *header = ( program_header_source ? program_header_source : "" );

	const char* shader_source[5];
	memset( shader_source, 0, sizeof( shader_source ) );
	shader_source[0] = header;
	shader_source[1] = "#define FRAGMENT_SHADER_SUPPORTS_HIGHP 1\n"; // TODO? this seems a safe assumption on Vulkan...
	shader_source[2] = maskBuffer;

	if ( program->IsCompilerVerbose() )
	{
		// All the segments except the last one
		int numSegments = sizeof( shader_source ) / sizeof( shader_source[0] ) - 1;
		data.fHeaderNumLines = CountLines( shader_source, numSegments );
	}

	// Vertex shader.
	{
		shader_source[3] = program->GetVertexShaderSource();

		Compile( shaderc_vertex_shader, shader_source, sizeof( shader_source ) / sizeof( shader_source[0] ), maps, data.fVertexShader );
	}

	// Fragment shader.
	{
		shader_source[3] = ( version == Program::kWireframe ) ? kWireframeSource : program->GetFragmentShaderSource();

		Compile( shaderc_fragment_shader, shader_source, sizeof( shader_source ) / sizeof( shader_source[0] ), maps, data.fFragmentShader );
	}
#else
	// no need to compile, but reflection here...
#endif
	return maps;
}

U32
VulkanProgram::Maps::CheckForSampler( const std::string & key /* TODO: info... */ )
{
	auto iter = samplers.find( key );

	if (iter != samplers.end())
	{
		auto imageType = iter->second;

		return 0U;	// TODO: maybe just want binding decorator, from above?
					// imageType would be handy for later expansion...
	}

	else
	{
		return ~0U;
	}
}

U32
VulkanProgram::Maps::CheckForUniform( const std::string & key )
{
	auto iter = uniforms.find( key );

	if (iter != uniforms.end())
	{
		auto & pair = iter->second;

		return pair.first; // TODO: not sure we need second, cf. note in VersionData
	}

	else
	{
		return ~0U;
	}
}

void
VulkanProgram::Update( Program::Version version, VersionData& data )
{
	Program* program = static_cast<Program*>( fResource );

	Maps maps = UpdateShaderSource( program,
									version,
									data );

#ifdef Rtt_USE_PRECOMPILED_SHADERS // TODO! (can probably just load spv?)
	ShaderBinary *shaderBinary = program->GetCompiledShaders()->Get(version);
	glProgramBinaryOES(data.fProgram, GL_PROGRAM_BINARY_ANGLE, shaderBinary->GetBytes(), shaderBinary->GetByteCount());
	GL_CHECK_ERROR();
	GLint linkResult = 0;
	glGetProgramiv(data.fProgram, GL_LINK_STATUS, &linkResult);
	if (!linkResult)
	{
		const int MAX_MESSAGE_LENGTH = 1024;
		char message[MAX_MESSAGE_LENGTH];
		GLint resultLength = 0;
		glGetProgramInfoLog(data.fProgram, MAX_MESSAGE_LENGTH, &resultLength, message);
		Rtt_LogException(message);
	}
	int locationIndex;
	locationIndex = glGetAttribLocation(data.fProgram, "a_Position");
	locationIndex = glGetAttribLocation(data.fProgram, "a_TexCoord");
	locationIndex = glGetAttribLocation(data.fProgram, "a_ColorScale");
	locationIndex = glGetAttribLocation(data.fProgram, "a_UserData");
#else
	bool isVerbose = program->IsCompilerVerbose();
	int kernelStartLine = 0;

	// TODO!

	if ( isVerbose )
	{
		kernelStartLine = data.fHeaderNumLines + program->GetVertexShellNumLines();
	}

	if ( isVerbose )
	{
		kernelStartLine = data.fHeaderNumLines + program->GetFragmentShellNumLines();
	}
#endif

	data.fUniformLocations[Uniform::kViewProjectionMatrix] = maps.CheckForUniform( "ViewProjectionMatrix" );
	data.fUniformLocations[Uniform::kMaskMatrix0] = maps.CheckForUniform( "MaskMatrix0" );
	data.fUniformLocations[Uniform::kMaskMatrix1] = maps.CheckForUniform( "MaskMatrix1" );
	data.fUniformLocations[Uniform::kMaskMatrix2] = maps.CheckForUniform( "MaskMatrix2" );
	data.fUniformLocations[Uniform::kTotalTime] = maps.CheckForUniform( "TotalTime" );
	data.fUniformLocations[Uniform::kDeltaTime] = maps.CheckForUniform( "DeltaTime" );
	data.fUniformLocations[Uniform::kTexelSize] = maps.CheckForUniform( "TexelSize" );
	data.fUniformLocations[Uniform::kContentScale] = maps.CheckForUniform( "ContentScale" );
	data.fUniformLocations[Uniform::kUserData0] = maps.CheckForUniform( "UserData0" );
	data.fUniformLocations[Uniform::kUserData1] = maps.CheckForUniform( "UserData1" );
	data.fUniformLocations[Uniform::kUserData2] = maps.CheckForUniform( "UserData2" );
	data.fUniformLocations[Uniform::kUserData3] = maps.CheckForUniform( "UserData3" );

/*
	glUniform1i( glGetUniformLocation( data.fProgram, "u_FillSampler0" ), Texture::kFill0 );
	glUniform1i( glGetUniformLocation( data.fProgram, "u_FillSampler1" ), Texture::kFill1 );
	glUniform1i( glGetUniformLocation( data.fProgram, "u_MaskSampler0" ), Texture::kMask0 );
	glUniform1i( glGetUniformLocation( data.fProgram, "u_MaskSampler1" ), Texture::kMask1 );
	glUniform1i( glGetUniformLocation( data.fProgram, "u_MaskSampler2" ), Texture::kMask2 );
*/
}

void
VulkanProgram::Reset( VersionData& data )
{
	data.fVertexShader = VK_NULL_HANDLE;
	data.fFragmentShader = VK_NULL_HANDLE;

	for( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i )
	{
		const uint32_t kInactiveLocation = ~0U;
		data.fUniformLocations[ i ] = kInactiveLocation;

		// CommandBuffer also initializes timestamp to zero
		const U32 kTimestamp = 0;
		data.fTimestamps[ i ] = kTimestamp;
	}
	
	data.fHeaderNumLines = 0;
}

void
VulkanProgram::InitializeCompiler( shaderc_compiler_t * compiler, shaderc_compile_options_t * options )
{
	if (compiler && options)
	{
		*compiler = shaderc_compiler_initialize();
		*options = shaderc_compile_options_initialize();

// void shaderc_compile_options_set_generate_debug_info(shaderc_compile_options_t options)
// void shaderc_compile_options_set_optimization_level(shaderc_compile_options_t options, shaderc_optimization_level level)
// void shaderc_compile_options_set_invert_y(shaderc_compile_options_t options, bool enable)
	}
}

void
VulkanProgram::CleanUpCompiler( shaderc_compiler_t compiler, shaderc_compile_options_t options )
{
	shaderc_compiler_release( compiler );
	shaderc_compile_options_release( options );
}

std::vector< VkVertexInputAttributeDescription >
VulkanProgram::AttributeDescriptions()
{
	std::vector< VkVertexInputAttributeDescription > attributeDescriptions;

	VkVertexInputAttributeDescription description = {};

	description.location = Geometry::kVertexPositionAttribute;
	description.format = VK_FORMAT_R32G32B32_SFLOAT;
	description.offset = offsetof( Geometry::Vertex, x );

	attributeDescriptions.push_back( description );

	description.location = Geometry::kVertexTexCoordAttribute;
	description.format = VK_FORMAT_R32G32B32_SFLOAT;
	description.offset = offsetof( Geometry::Vertex, u );

	attributeDescriptions.push_back( description );

	description.location = Geometry::kVertexColorScaleAttribute;
	description.format = VK_FORMAT_R8G8B8A8_UNORM;
	description.offset = offsetof( Geometry::Vertex, rs );

	attributeDescriptions.push_back( description );

	description.location = Geometry::kVertexUserDataAttribute;
	description.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	description.offset = offsetof( Geometry::Vertex, ux );

	attributeDescriptions.push_back( description );

	return attributeDescriptions;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

/*
    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.pImmutableSamplers = nullptr;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};
        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout!");
        }
    }

    void createDescriptorPool() {
        std::array<VkDescriptorPoolSize, 2> poolSizes = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = static_cast<uint32_t>(swapChainImages.size());
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = static_cast<uint32_t>(swapChainImages.size());

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = static_cast<uint32_t>(swapChainImages.size());

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    void createDescriptorSets() {
        std::vector<VkDescriptorSetLayout> layouts(swapChainImages.size(), descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(swapChainImages.size());
        allocInfo.pSetLayouts = layouts.data();

        descriptorSets.resize(swapChainImages.size());
        if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate descriptor sets!");
        }

        for (size_t i = 0; i < swapChainImages.size(); i++) {
            VkDescriptorBufferInfo bufferInfo = {};
            bufferInfo.buffer = uniformBuffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

            VkDescriptorImageInfo imageInfo = {};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = textureImageView;
            imageInfo.sampler = textureSampler;

            std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};

            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = descriptorSets[i];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &bufferInfo;

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = descriptorSets[i];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        }
    }
*/