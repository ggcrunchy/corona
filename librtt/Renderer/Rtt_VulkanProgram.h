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
#include "Renderer/Rtt_VulkanIncludes.h"
#include "Core/Rtt_Assert.h"
#include "Core/Rtt_Macros.h"

#include <utility>
#include <vector>

// ----------------------------------------------------------------------------

struct shaderc_compiler;
struct shaderc_compile_options;

namespace Rtt
{

struct VulkanCompilerMaps;
class VulkanRenderer;
class VulkanContext;
class ShaderCode;

// ----------------------------------------------------------------------------

class VulkanProgram : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef VulkanProgram Self;

	public:
		VulkanProgram( VulkanContext * context );

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();
		
		void Bind( VulkanRenderer & renderer, Program::Version version );

		Rtt_CLASSCONSTANT( VulkanProgram, kInvalidID, (uint16_t)~0U );

		struct Location {
			Location( size_t offset = 0U, size_t range = 0U, bool isUniform = false )
			:	fOffset( offset ),
				fRange( range ),
				fIsUniform( isUniform )
			{
			}

			bool IsValid() const { return !!fRange; }

			size_t fOffset;
			size_t fRange;
			bool fIsUniform;
		};

		// TODO: cleanup these functions
		inline Location GetUniformLocation( U32 unit, Program::Version version )
		{
			Rtt_ASSERT( version <= Program::kNumVersions );
			return fData[version].fUniformLocations[ unit ];
		}

		inline Location GetTranslationLocation( U32 unit, Program::Version version )
		{
			Rtt_ASSERT( unit >= Uniform::kMaskMatrix0 && unit <= Uniform::kMaskMatrix2 );
			Rtt_ASSERT( version <= Program::kNumVersions );
			return fData[version].fMaskTranslationLocations[ unit - Uniform::kMaskMatrix0 ];
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

		inline bool IsValid( Program::Version version )
		{
			Rtt_ASSERT( version <= Program::kNumVersions );
			return fData[version].IsValid();
		}

		U32 GetPushConstantStages() const { return fPushConstantStages; }
		bool HavePushConstantUniforms() const { return fPushConstantUniforms; }

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
			Location fUniformLocations[Uniform::kNumBuiltInVariables];
			Location fMaskTranslationLocations[3]; // these should only ever be present alongside the corresponding MaskMatrix
			// TODO? also supply ranges (else assume always in proper form)
			// TODO? divvy these up between uniforms and push constants
			U32 fTimestamps[Uniform::kNumBuiltInVariables];
			U32 fShadersID;
			
			// Metadata
			int fHeaderNumLines;
		};

		void Create( Program::Version version, VersionData& data );
		void Update( Program::Version version, VersionData& data );
		void Reset( VersionData& data );

		struct UserdataValue {
			UserdataValue()
			:	fComponentCount( 0U ),
				fStages( 0U )
			{
			}

			bool operator < (const UserdataValue & other) const { return fComponentCount > other.fComponentCount; } // process largest-sized elements first
			bool IsValid() const { return !!fComponentCount; }

			int fIndex;
			U32 fComponentCount;
			U32 fStages;
		};

		struct UserdataDeclaration {
			UserdataValue * fValue;
			size_t fPosition;
			size_t fLength;
		};

		struct UserdataPosition {
			UserdataPosition()
			:	fValue( NULL ),
				fOffset( 0U ),
				fRow( ~0U )
			{
			}

			bool operator < (const UserdataPosition & other) const { return fRow != other.fRow ? fRow < other.fRow : fOffset < other.fOffset; }
			bool IsValid() const { return fRow != ~0U; }

			UserdataValue * fValue;
			U32 fOffset;
			U32 fRow;
		};

		size_t GatherUniformUserdata( bool isVertexSource, ShaderCode & code, UserdataValue values[], std::vector< UserdataDeclaration > & declarations );
		bool ReplaceFragCoords( ShaderCode & code, size_t offset );
		void ReplaceVertexSamplers( ShaderCode & code );
		void ReplaceVaryings( bool isVertexSource, ShaderCode & code, VulkanCompilerMaps & maps );
		void Compile( int kind, ShaderCode & code, VulkanCompilerMaps & maps, VkShaderModule & module );
		std::pair< bool, int > SearchForFreeRows( const UserdataValue values[], UserdataPosition positions[], size_t vectorCount );
		U32 AddToString( std::string & str, const UserdataValue & value );
		void UpdateShaderSource( VulkanCompilerMaps & maps, Program* program, Program::Version version, VersionData& data );

		static void InitializeCompiler( shaderc_compiler ** compiler, shaderc_compile_options ** options );
		static void CleanUpCompiler( shaderc_compiler * compiler, shaderc_compile_options * options );

		VulkanContext * fContext;
		VersionData fData[Program::kNumVersions];
		CPUResource* fResource;
		U32 fPushConstantStages;
		bool fPushConstantUniforms;

		static U32 sID;

		friend class VulkanContext;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanProgram_H__
