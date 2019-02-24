//////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2018 Corona Labs Inc.
// Contact: support@coronalabs.com
//
// This file is part of the Corona game engine.
//
// Commercial License Usage
// Licensees holding valid commercial Corona licenses may use this file in
// accordance with the commercial license agreement between you and 
// Corona Labs Inc. For licensing terms and conditions please contact
// support@coronalabs.com or visit https://coronalabs.com/com-license
//
// GNU General Public License Usage
// Alternatively, this file may be used under the terms of the GNU General
// Public license version 3. The license is as published by the Free Software
// Foundation and appearing in the file LICENSE.GPL3 included in the packaging
// of this file. Please review the following information to ensure the GNU 
// General Public License requirements will
// be met: https://www.gnu.org/licenses/gpl-3.0.html
//
// For overview and more information on licensing please refer to README.md
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_GLUniformArray_H__
#define _Rtt_GLUniformArray_H__

#include "Renderer/Rtt_GPUResource.h"
#include "Renderer/Rtt_UniformArray.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class GLUniformArray : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef GLUniformArray Self;

	public:
		GLUniformArray();

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();

	public:
		U8 *GetBytes() const { return fBytes; }
		void SetBytes( U8 *offset ) { fBytes = offset; }

		U32 GetSize() const { return fSize; }
		void SetSize( U32 size ) { fSize = size; }

	private:
		U8 *fBytes;
		U32 fSize;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_GLUniformArray_H__
