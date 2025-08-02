//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Texture_H__
#define _Rtt_Texture_H__

#include "Renderer/Rtt_CPUResource.h"
#include "Core/Rtt_Types.h"
#include <stdlib.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class Texture : public CPUResource
{
	public:
		typedef CPUResource Super;
		typedef Texture Self;

		typedef enum _FormatValue
		{
			kAlpha,
			kLuminance,
			kRGB,
			kRGBA,
			kBGRA,
			kABGR,
			kARGB,
			kLuminanceAlpha,
			kNonCore,
			kNumFormats
		}
		FormatValue;

		class Format {
		public:
			Format( FormatValue value = kNumFormats );
			
			bool operator == ( FormatValue value ) const;
			
			static Format NonCore( U16 index, U16 layoutDetails );
			
			FormatValue GetValue( U16* index = NULL, U16* layoutDetails = NULL ) const;
			
		private:
			U16 fValue;
			U16 fIndex;
		};

		typedef enum _Filter
		{
			kNearest,
			kLinear,
			kNumFilters
		}
		Filter;

		typedef enum _Wrap
		{
			kClampToEdge,
			kRepeat,
			kMirroredRepeat,

			kNumWraps
		}
		Wrap;

		typedef enum _Unit
		{
			kFill0,
			kFill1,
			kMask0,
			kMask1,
			kMask2,
			kNumUnits
		}
		Unit;

	public:

		Texture( Rtt_Allocator* allocator );
		virtual ~Texture();

		virtual ResourceType GetType() const;
		virtual void Allocate();
		virtual void Deallocate();

		virtual U32 GetWidth() const = 0;
		virtual U32 GetHeight() const = 0;
		virtual Format GetFormat() const = 0;
		virtual Filter GetFilter() const = 0;
		virtual Wrap GetWrapX() const;
		virtual Wrap GetWrapY() const;
		virtual size_t GetSizeInBytes() const;
		virtual U8 GetByteAlignment() const;

		virtual const U8* GetData() const;
		virtual void ReleaseData();

		virtual void SetFilter( Filter newValue );
		virtual void SetWrapX( Wrap newValue );
		virtual void SetWrapY( Wrap newValue );
	
	public:
		void SetRetina( bool newValue ){ fIsRetina = newValue; }
		bool IsRetina() const { return fIsRetina; }
		void SetTarget( bool newValue ){ fIsTarget = newValue; }
		bool IsTarget() const { return fIsTarget; }

	private:
		bool fIsRetina;
		bool fIsTarget;
};

inline bool operator == ( Texture::FormatValue value, const Texture::Format& format )
{
	return format.GetValue() == value;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Texture_H__
