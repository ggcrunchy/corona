//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_ShaderCode.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

void
ShaderCode::SetSources( const char * sources[], size_t count )
{
	fCode.clear();

	for (int i = 0; i < count; ++i)
	{
		fCode += sources[i];
	}

	GatherIntervals();
}

size_t
ShaderCode::Find( const char * what, size_t offset ) const
{
	while (true)
	{
		size_t pos = fCode.find( what, offset );

		if (std::string::npos == pos)
		{
			return std::string::npos;
		}

		if (OutsideComments( pos ))
		{
			return pos;
		}

		else
		{
			offset = pos + strlen( what );
		}
	}
}

int
ShaderCode::Insert( size_t pos, const std::string & insertion )
{
	fCode.insert( pos, insertion );

	return AdvanceIntervals( pos, 0U, insertion );
}

int
ShaderCode::Replace( size_t pos, size_t count, const std::string & replacement )
{
	fCode.replace( pos, count, replacement );

	return AdvanceIntervals( pos, count, replacement );
}

bool
ShaderCode::OutsideComments( size_t pos ) const
{
	for (auto && interval : fIntervals)
	{
		if (pos > interval.first && pos < interval.second)
		{
			return false;
		}
	}

	return true;
}

int
ShaderCode::AdvanceIntervals( size_t pos, size_t count, const std::string & replacement )
{
	int oldSize = int( count ), newSize = int( replacement.size() ), delta = newSize - oldSize;

	for (auto && iter = fIntervals.rbegin(); iter != fIntervals.rend(); ++iter)
	{
		if (iter->first > pos)
		{
			iter->first += delta;
			iter->second += delta;
		}

		else
		{
			break;
		}
	}

	return delta;
}

void
ShaderCode::GatherIntervals()
{
	fIntervals.clear();

	size_t offset = 0U;

	// Multi-line comments
	while (true)
	{
		size_t beginPos = fCode.find( "/*", offset );

		if (std::string::npos == beginPos)
		{
			break;
		}

		offset = beginPos + strlen( "/*" );

		size_t endPos = fCode.find( "*/", offset );

		if (std::string::npos != endPos) // unclosed comments mean code itself is broken
		{
			offset = endPos + strlen( "*/" );

			fIntervals.push_back( std::make_pair( beginPos, offset - 1U ) );
		}
	}

	// Single-line comments
	offset = 0U;

	while (true)
	{
		size_t beginPos = Find( "//", offset );

		if (std::string::npos == beginPos)
		{
			return;
		}

		else
		{
			size_t endPos = fCode.find( "\n", beginPos + 1U );

			if (std::string::npos == endPos)
			{
				fIntervals.push_back( std::make_pair( beginPos, fCode.size() ) );

				return;
			}

			else
			{
				offset = endPos + strlen( "\n" );

				fIntervals.push_back( std::make_pair( beginPos, offset - 1U ) );
			}
		}
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------