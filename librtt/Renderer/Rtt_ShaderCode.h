//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_ShaderCode_H__
#define _Rtt_ShaderCode_H__

#include <string>
#include <utility>
#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class ShaderCode
{
	public:
		typedef ShaderCode Self;

		typedef std::pair< size_t, size_t > Interval;

	public:
		void SetSources( const char * sources[], size_t count );

	public:
		size_t Find( const char * what, size_t offset ) const;

		int Insert( size_t pos, const std::string & insertion );
		int Replace( size_t pos, size_t count, const std::string & replacement );

	public:
		const std::string & GetString() const { return fCode; }
		const char * GetCStr() const { return fCode.c_str(); }

	private:
		bool OutsideComments( size_t pos ) const;
		int AdvanceIntervals( size_t pos, size_t count, const std::string & replacement );
		void GatherIntervals();

	private:
		std::string fCode;
		std::vector< Interval > fIntervals;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_ShaderCode_H__