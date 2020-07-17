//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_VulkanProgram_H__
#define _Rtt_VulkanProgram_H__

#include "Renderer/Rtt_GPUResource.h"
#include "Renderer/Rtt_Program.h"
#include "Renderer/Rtt_Uniform.h"
#include "Core/Rtt_Assert.h"
#include <vulkan/vulkan.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

class VulkanState;

// ----------------------------------------------------------------------------

class VulkanProgram : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef VulkanProgram Self;

	public:
		VulkanProgram( VulkanState * state );

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();
		virtual void Bind( Program::Version version );

		// TODO: cleanup these functions
		inline uint32_t GetUniformLocation( U32 unit, Program::Version version )
		{
			Rtt_ASSERT( version <= Program::kNumVersions );
			return fData[version].fUniformLocations[ unit ];
		}

		inline U32 GetUniformTimestamp( U32 unit, Program::Version version )
		{
			Rtt_ASSERT( version <= Program::kNumVersions );
			return fData[version].fTimestamps[ unit ];
		}

		inline void SetUniformTimestamp( U32 unit, Program::Version version, U32 timestamp)
		{
			Rtt_ASSERT( version <= Program::kNumVersions );
			fData[version].fTimestamps[ unit ] = timestamp;
		}

	private:
		// To make custom shader code work seamlessly with masking, multiple
		// versions of each Program are automatically compiled and linked, 
		// with each version supporting a different number of active masks.
		struct VersionData
		{
			bool IsValid() const { return fVertexShader != VK_NULL_HANDLE && fFragmentShader != VK_NULL_HANDLE; }

			VkShaderModule fVertexShader;
			// TODO? mask counts as specialization info
			VkShaderModule fFragmentShader;
			uint32_t fUniformLocations[Uniform::kNumBuiltInVariables];
			// TODO? divvy these up between uniforms and push constants
			U32 fTimestamps[Uniform::kNumBuiltInVariables];
			U32 fShadersID;
			
			// Metadata
			int fHeaderNumLines;
		};

		void Create( Program::Version version, VersionData& data );
		void Update( Program::Version version, VersionData& data );
		void UpdateShaderSource( Program* program, Program::Version version, VersionData& data );
		void Reset( VersionData& data );

		VulkanState * fState;
		VersionData fData[Program::kNumVersions];
		CPUResource* fResource;

		static U32 sID;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanProgram_H__
