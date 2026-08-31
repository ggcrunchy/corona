//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Core/Rtt_String.h"
#include "Core/Rtt_Assert.h"
#include "Core/Rtt_Array.h"

#include "Renderer/Rtt_CommandBuffer.h"
#include "Renderer/Rtt_FormatExtensionList.h"

#include "Corona/CoronaGraphics.h"
#include <algorithm>

#include <stdio.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

U32
FormatExtensionList::Attribute::GetSize() const
{
    U32 count = GetComponentCount();
    
    if (kAttributeType_Byte != type)
    {
        count *= 4; // TODO: double, etc.
    }
        
    return count;
}

bool
FormatExtensionList::Attribute::IsFloat() const
{
    return !!normalized || (CoronaVertexExtensionAttributeType)kAttributeType_Float == type;
}

U32
FormatExtensionList::Group::GetWindowAttributeCount( U32 divisorChunks/*valueCount*/ ) const
{
    return /*valueCount*/divisorChunks + count - 1;
}

U32
FormatExtensionList::Group::/*GetValueCount*/InstanceStreamSizeInDivisorChunks( U32 instanceCount ) const
{
    Rtt_ASSERT( 0 != divisor );
    
    return (instanceCount + divisor - 1) / divisor;
}

/*size_t*/U32
FormatExtensionList::Group::InstanceStreamSizeInBytes/*GetDataSize*/( U32 instanceCount, const Attribute * firstAttribute ) const
{
    U32 /*valueCount*/divisorChunkCount = /*GetValueCount*/InstanceStreamSizeInDivisorChunks( instanceCount );
    
    if (IsWindowed())
    {
        Rtt_ASSERT( firstAttribute );
        
        return GetWindowAttributeCount( /*valueCount*/divisorChunkCount ) * firstAttribute->GetSize();
    }
    
    else
    {
        return size * divisorChunkCount;//valueCount;
    }
}

U32
FormatExtensionList::Group::InstanceStreamSizeInVertices/*GetVertexCount*/( U32 instanceCount, const Attribute * firstAttribute ) const
{
    return Geometry::Vertex::SizeInVertices( /*GetDataSize*/InstanceStreamSizeInBytes( instanceCount, firstAttribute ) );
}

FormatExtensionList::FormatExtensionList(  Group* groups, U16 groupCount, Attribute* attributes, U16 attributeCount )
:   fAttributes( attributes /*NULL*/ ),
    fGroups( groups/*NULL*/ ),
    fLookupData( NULL ),
//    fNames( NULL ),
    fAttributeCount( attributeCount/*0*/ ),
    fGroupCount( groupCount/*0*/ ),
    fOwnsData( false )/*,
    fSorted( false )*/
{
}

FormatExtensionList::~FormatExtensionList()
{
    if (fOwnsData)
    {
        Rtt_DELETE( fAttributes );
        Rtt_DELETE( fGroups );
        /*
        if ( fNames )
        {
            for (U32 i = 0; i < fAttributeCount; ++i)
            {
                Rtt_DELETE( fNames[i].str );
            }
            
            Rtt_DELETE( fNames );
        }*/
        Rtt_FREE( fLookupData );
    }
}

FormatExtensionList::Iterator
FormatExtensionList::AllGroups( const FormatExtensionList* list )
{
    return Iterator( list, Iterator::kAllGroups, Iterator::kIterateGroups );
}

FormatExtensionList::Iterator
FormatExtensionList::AllAttributes( const FormatExtensionList* list )
{
    return Iterator( list, Iterator::kAllGroups, Iterator::kIterateAttributes );
}

FormatExtensionList::Iterator
FormatExtensionList::InstancedGroups( const FormatExtensionList* list )
{
    return Iterator( list, Iterator::kInstancedGroups, Iterator::kIterateGroups );
}
#if 0
FormatExtensionList FormatExtensionList::FromArrays( const Group groups[], const Attribute attributes[], U16 groupCount, U16 attributeCount ) //const Array<Group>& groups, const Array<Attribute>& attributes )
{
    FormatExtensionList list;

    list.fGroups = groups;//const_cast<Group*>( groups.ReadAccess() );
    list.fGroupCount = groupCount;// (U16)groups.Length();
    list.fAttributes = attributes;//const_cast<Attribute*>( attributes.ReadAccess() );
    list.fAttributeCount = attributeCount;//(U16)attributes.Length();

    return list;
}
#endif
static bool
HasVertexRateGroup( const FormatExtensionList::Group * groups, U32 groupCount )
{
    return ( groupCount > 0 ) && !groups[0].IsInstanceRate();
}

U32
FormatExtensionList::ExtraVertexRateSizeInVertices/*ExtraVertexCount*/() const
{
//    U32 extra = 0;
    
    if ( HasVertexRateGroup( fGroups, fGroupCount ) )
    {
        /*extra =*/return Geometry::Vertex::SizeInVertices( fGroups[0].size );
    }
    else
    {
		return 0;
    }
//    return extra;
}

U32
FormatExtensionList::InstanceGroupCount() const
{
/*    U32 count = fGroupCount;
    
    if ( HasVertexRateData() )
    {
        --count;
    }
    
    return count;*/
    return fGroupCount - HasVertexRateData();
}

bool
FormatExtensionList::HasInstanceRateData() const
{/*
    if (1 == fGroupCount)
    {
        return fGroups[0].IsInstanceRate();
    }
    
    else
    {
        return fGroupCount > 1;
    }*/
    return InstanceGroupCount() > 0;
}

bool
FormatExtensionList::HasVertexRateData() const
{
    return HasVertexRateGroup( fGroups, fGroupCount );
}
#if 0
void
FormatExtensionList::SortNames() const
{
    if (fNames && !fSorted)
    {
        std::sort( fNames, fNames + fAttributeCount );
        
        fSorted = true;
    }
}

const char*
FormatExtensionList::FindNameByAttribute( U32 attributeIndex, S32* index ) const
{
    if ( fNames && attributeIndex < fAttributeCount )
    {
        NamePair& pair = fNames[attributeIndex];

        if ( index )
        {
            *index = pair.index;
        }

        return pair.str->GetString();
    }

    else
    {
        return NULL;
    }
}

S32
FormatExtensionList::FindHash( size_t hash ) const
{
    for (U32 i = 0; i < fAttributeCount; ++i)
    {
        if (hash == fAttributes[i].nameHash)
        {
            return i;
        }
    }
    
    return -1;
}

S32
FormatExtensionList::FindName( const char* name ) const
{
    if (fNames)
    {
        for (U32 i = 0; i < fAttributeCount; ++i)
        {
            if (0 == Rtt_StringCompare( name, fNames[i].str->GetString() ))
            {
                FormatExtensionList::NamePair temp = fNames[i]; // move to front
                
                fNames[i] = fNames[0];
                fNames[0] = temp;
                fSorted = false;
                
                return temp.index;
            }
        }
    }
    
    return -1;
}
#endif
U32
FormatExtensionList::FindGroup( U32 attributeIndex ) const
{
    Rtt_ASSERT( attributeIndex < fAttributeCount );

    S32 groupIndex = -1;
    
//    for (auto iter = FormatExtensionList::AllGroups( this ); !iter.IsDone(); iter.Advance())
	for ( auto&& iter : FormatExtensionList::AllGroups( this ) )
    {
        if (iter.attributeIndex/* GetAttributeIndex()*/ > attributeIndex)
        {
            break;
        }
        
        ++groupIndex;
    }
    
    Rtt_ASSERT( groupIndex >= 0 );
    
    return groupIndex;
}


S32
FormatExtensionList::FindCorrespondingInstanceGroup( const Group* group, const Attribute* attribute, const U8* nameData, U32 * attributeIndex ) const
{
//    for (auto iter = FormatExtensionList::InstancedGroups( this ); !iter.IsDone(); iter.Advance())
	for ( auto&& iter : FormatExtensionList::InstancedGroups( this ) )
    {
        const Group* curGroup = iter.group;//GetGroup();
        bool windowingMatches = curGroup->IsWindowed() == group->IsWindowed();
        bool divisorsMatch = curGroup->divisor == group->divisor;
        bool namesMatch = !group->IsWindowed() || FindMatchingAttribute( nameData, attribute->name_triples_minus_1 + 1 ); /*attribute->nameHash == iter.GetAttribute()->nameHash*/;
    
        // Matching groups must have the same divisor.
        // In the case of windowed groups, they must also both be such, and
        // also agree in names: however, if any name matches, they all do.
        if (windowingMatches && divisorsMatch && namesMatch)
        {
            if (attributeIndex)
            {
                *attributeIndex = iter.attributeIndex;// GetAttributeIndex();
            }
        
            return (S32)iter.groupIndex;//GetGroupIndex();
        }
    }
            
    return -1;
}

/*size_t*/U32
FormatExtensionList::/*GetExtraVertexSize*/ExtraVertexRateSizeInBytes( const FormatExtensionList * list )
{
    if (list)
    {
        return list->ExtraVertexRateSizeInVertices/*ExtraVertexCount*/() * sizeof(Geometry::Vertex);
    }
    
    else
    {
        return 0;
    }
}

/*size_t*/U32
FormatExtensionList::FullVertexRateSizeInBytes/*GetVertexSize*/( const FormatExtensionList* list )
{
    return sizeof(Geometry::Vertex) + ExtraVertexRateSizeInBytes/*GetExtraVertexSize*/( list );
}

static bool
LogNameError( const char * formatStr, const FormatExtensionList * list, const FormatExtensionList::Attribute* attribute )// FormatExtensionList::Iterator::CurrentState& state/* & iter*/ )
{
//    list->SortNames();

	char name[64 + 1];
	String::DecodeIdentifier( name, list->FindAttributeNameData( attribute ), attribute->name_triples_minus_1 + 1 );
//    const char* name = list->FindNameByAttribute( state.attributeIndex );// iter.GetAttributeIndex() );
 /*
    if ( NULL == name )
    {
        name = "??? (name set missing)";
    }*/
    
    Rtt_TRACE_SIM(( formatStr, *name ? name : "??? (name set missing)" ));

    return false;
}

bool
FormatExtensionList::Compatible( const FormatExtensionList * shaderList, const FormatExtensionList * geometryList )
{
    if (NULL == shaderList) // implicitly the reduced case, below...
    {
        return true;
    }
    
    else if (NULL == geometryList) // ...whereas this is the opposite
    {
        Rtt_TRACE_SIM(( "WARNING: no geometry list, so shader list implicitly not a subset." ));
        
        return false;
    }
    
    else if ((shaderList->fGroupCount > geometryList->fGroupCount) || (shaderList->fAttributeCount > geometryList->fAttributeCount))
    {
        Rtt_TRACE_SIM(( "WARNING: shader list has more attributes or groups than geometry list." ));
        
        return false;
    }
	
    else
    {
		bool geometryIsInstanced = geometryList->fInstancedByID;
		
//        for (auto iter = FormatExtensionList::AllAttributes( shaderList ); !iter.IsDone(); iter.Advance())
		for ( auto&& iter : FormatExtensionList::AllAttributes( shaderList ) )
        {
            Attribute shaderAttribute = *iter.attribute;//*iter.GetAttribute();
        //    S32 geometryAttributeIndex = geometryList->FindHash( shaderAttribute.nameHash );
			S32 geometryAttributeIndex = geometryList->FindMatchingAttribute( shaderList, iter.attribute );
            if (-1 == geometryAttributeIndex)
            {
                return LogNameError( "WARNING: no attribute named `%s` in geometry list.", shaderList, iter.attribute );
            }
            
            Attribute geometryAttribute = geometryList->fAttributes[geometryAttributeIndex];
            
            if (shaderAttribute.type != geometryAttribute.type)
            {
                if (shaderAttribute.IsFloat() != geometryAttribute.IsFloat())
                {
                    return LogNameError( "WARNING: type clash with attribute named `%s`: float vs. non-float", shaderList, iter.attribute );
                }
                
                else
                {
                    return LogNameError( "WARNING: incompatible types with attribute named `%s`", shaderList, iter.attribute );
                    
                    // TODO: too strict? not enough? byte / int compatiblity, etc. signedness etc.
                }
            }
            
            U32 geometryGroupIndex = geometryList->FindGroup( geometryAttributeIndex );
            Group geometryGroup = geometryList->fGroups[geometryGroupIndex], shaderGroup = *iter.group;//GetGroup();
        
			geometryIsInstanced |= geometryGroup.IsInstanceRate();
			
            if (shaderGroup.divisor != geometryGroup.divisor)
            {
                return LogNameError( "WARNING: instancing count clash with attribute named `%s`", shaderList, iter.attribute );
            }
            
            if (shaderGroup.IsWindowed() != geometryGroup.IsWindowed())
            {
                return LogNameError( "WARNING: structuring clash with attribute named `%s`: windowed vs. not windowed", shaderList, iter.attribute );
            }
        
            if (shaderGroup.IsWindowed() && shaderGroup.count != geometryGroup.count)
            {
                return LogNameError( "WARNING: structuring clash with attribute named `%s`: windows have different sizes", shaderList, iter.attribute );
            }
        }
		
		if (geometryIsInstanced && !shaderList->IsInstanced())
		{
			Rtt_Log( "WARNING: geometry list instantiated, but not shader list." );
			
			return false;
		}

        return true;
    }
}

bool
FormatExtensionList::Match( const FormatExtensionList * list1, const FormatExtensionList * list2 )
{
    if (NULL == list1 || NULL == list2)
    {
        return list1 == list2;
    }
    
    else if ((list1->fGroupCount != list2->fGroupCount) || (list1->fAttributeCount != list2->fAttributeCount))
    {
        return false;
    }
    
    else
    {
        return 0 == memcmp( list1->fGroups, list2->fGroups, sizeof(Group) * list1->fGroupCount )
        && 0 == memcmp( list1->fAttributes, list2->fAttributes, sizeof(Attribute) * list1->fAttributeCount );
        // TODO? strictly speaking, the components need not agree
    }
}

bool
FormatExtensionList::NamedAttributeInfo::operator<( const NamedAttributeInfo & other ) const
{
#if 0
	if ( length == other.length )
	{
		int comp = /*( 0 ==*/ strncmp( name, other.name, length );// );
	/*	if ( 0 == comp ) // might be a window attribute
		{
			return suffix < other.suffix;
		}
		else*/
		// n.b. trying to move windows to use array rather that multiple named attributes
		{
			return comp < 0;
		}
	}
	else
#endif
	{
		return length < other.length;
	}
}

struct GroupInfo {
	U32 divisor;
	U16 index;
	U16 isWindow;
	
	bool operator<( const GroupInfo& other ) const
	{
		if ( divisor == other.divisor )
		{
			return isWindow < other.isWindow;
		}
		else
		{
			return divisor < other.divisor;
		}
	}
};

static U16
Max( U16 a, U16 b )
{
	return a > b ? a : b;
}

void
FormatExtensionList::Build( Rtt_Allocator* allocator, const CoronaVertexExtension * extension )//, const NamedAttributeInfo info[] )
{
	GroupInfo groupInfo[kMaxAttribs];
/*
    Array< Attribute > attributes( allocator );
    Array< Group > groups( allocator );*/
//	const char *haystack[kMaxAttribs];
    
//    LightPtrArray< String > names( allocator );
    
    for ( int i = 0; i < extension->count; i++ )
    {
		groupInfo[i].index = i;
		groupInfo[i].isWindow = extension->attributes[i].windowSize > 1;
		groupInfo[i].divisor = extension->attributes[i].instancesToReplicate;
    }

    std::sort( groupInfo, groupInfo + extension->count );

	NamedAttributeInfo info[kMaxAttribs];
    Attribute attributes[kMaxAttribs];
    Group groups[kMaxAttribs], *curGroup;

	for ( U32 i = 0; i < extension->count; i++ )
	{
		bool isWindow = false;
		const CoronaVertexExtensionAttribute &attribData = extension->attributes[ groupInfo[i].index ];
	
		if ( ( 0 == i ) || ( groupInfo[i - 1] < groupInfo[i] ) )
		{
			curGroup = &groups[fGroupCount++];
			curGroup->size = 0;
		
			isWindow = groupInfo[i].isWindow;
			if ( isWindow )
			{
				curGroup->count = attribData.windowSize;
				curGroup->divisor = Max( groupInfo[i].divisor, 1 );
			}
			else
			{
				curGroup->count = 0;
				curGroup->divisor = groupInfo[i].divisor;
			}
		}
		
		Attribute &attrib = attributes[fAttributeCount++];
		
		attrib.type = attribData.type;
        attrib.comp_minus_1 = attribData.components - 1;
        attrib.normalized = attribData.normalized;
        
        if ( !isWindow )
        {
			attrib.offset = curGroup->size;
			
			curGroup->count++;
			curGroup->size += attrib.GetSize();
        }
        
		info[i].name = attribData.name;
		info[i].attribute = &attrib;
        info[i].length = strlen( attribData.name ); // TODO: comes from Lua, known
	}

#if 0
    for ( int i = 0; i < extension->count; i++ )
    {
        const CoronaVertexExtensionAttribute & attributeData = extension->attributes[i];

        Attribute attribute;// = {};
        
        attribute.type = attributeData.type;
        attribute.comp_minus_1 = attributeData.components - 1;
        attribute.normalized = attributeData.normalized;
        
        // Any window gets its own group.
        if (attributeData.windowSize > 1)
        {
            Group group;// = {};
            
            group.size = 0;
            group.count = attributeData.windowSize;
            group.divisor = attributeData.instancesToReplicate;
            
            if (0 == group.divisor)
            {
                ++group.divisor;
            }
            
            /*
            const unsigned int kWindowSizeLimit = 100; // should be more than enough, i.e. max attribs far less
            
            Rtt_ASSERT( attributeData.windowSize < kWindowSizeLimit );
            */
            // ^^^ TODO: should be caught by pre-build step
            for (int j = 0; j < attributeData.windowSize; ++j)
            {/*
                String* str = Rtt_NEW( allocator, String( allocator, attributeData.name ) );

                names.Append( str );
               
                char buf[3] = {}; // two digits, cf. kWindowSizeLimit
                
                snprintf( buf, sizeof(buf), "%i", j + 1 );
                
                str->Append( buf );

                attribute.nameHash = str->GetHash32();
           */     
				// ^^^ TODO: don't do this (or the loop), but figure out what needs tracking
				// make it a one-attribute group, if not already
				int attribIndex = fAttributeCount++;
haystack[attribIndex] = attributeData.name;
                attributes[attribIndex] = attribute;//.Append( attribute );
                // ^^^ or point to it...
                attribute.offset += attribute.GetSize();
            }
            
            int groupIndex = fGroupCount++;
            groups[groupIndex] = group;//.Append( group );
        }
        
        // Otherwise, merge desciptors with common divisors.
        else
        {
            S32 attributeIndex = 0, groupIndex = -1;
            
            if (!attributeData.instancesToReplicate) // not instanced?
            {
                if (!HasVertexRateGroup( groups/*.ReadAccess()*/, fGroupCount/*groups.Length()*/ )) // assumed to be first
                {
					memmove( groups + 1, groups, fGroupCount * sizeof(Group) );
					memset( groups, 0, sizeof(Group) );
                //    groups.Insert( 0, Group{} );
                
					++fGroupCount;
                }
                // ^^^ TODO: we could detect this beforehand...
                groupIndex = 0;
                attributeIndex = groups[0].count;
            }
            
            else
            {
                for (S32 j = 0/*, length = groups.Length()*/; j < fGroupCount/*length*/; ++j) // group exists?
                {
                    attributeIndex += groups[j].count; // skip over group, if windowed, or to end otherwise
                    
                    if (!groups[j].IsWindowed() && groups[j].divisor == attributeData.instancesToReplicate)
                    {
                        groupIndex = j;
                        
                        break;
                    }
                }
                
                if (-1 == groupIndex)
                {
                    groupIndex = fGroupCount++;//groups.Length();
                    
               //     Group group = {};
                    
                    groups[groupIndex].divisor = attributeData.instancesToReplicate;

//                    groups.Append( group );
                }
            }
            
            Group & group = groups[groupIndex];
            
            attribute.offset = group.size;

            group.size += attribute.GetSize();
            
            ++group.count;
     /*
            String* str = Rtt_NEW( allocator, String( allocator, attributeData.name ) );
            
            attribute.nameHash = str->GetHash32();*/
            
			memmove( attributes + attributeIndex + 1, attributes + attributeIndex, ( fAttributeCount - attributeIndex ) * sizeof(Attribute) );
memmove( haystack + attributeIndex + 1, haystack + attributeIndex, ( fAttributeCount - attributeIndex ) * sizeof(const char *) );
            attributes[attributeIndex] = attribute;
haystack[attributeIndex] = attributeData.name;
       //     attributes.Insert( attributeIndex, attribute );
       //     names.Insert( attributeIndex, str );
			fAttributeCount++;
        }
        
        info[i].name = extension->attributes[i].name;
        info[i].length = strlen( extension->attributes[i].name ); // TODO: comes from Lua, known
    //    info[i].offset = attribute.offset;
    }
    #endif
    if ( extension->count > 1 )
    {
		std::sort( info, info + extension->count );
    }
    
	U16 triplesBits = 0, namesSize = 0, offsets[15];
	for ( int i = 0, j = 0, prevTriples = -1; i < extension->count; i++ )
	{
        U16 numTriples = String::IdentifierLengthToTriples( info[i].length );
        
        triplesBits |= 1U << ( numTriples - 1 );

		if ( ( i > 0 ) && ( numTriples != prevTriples ) )
		{
			offsets[j++] = namesSize;
		}
		
        namesSize += numTriples * 3 + 1; // include attribute index / final flag
		
		prevTriples = numTriples;
	}
    
	// Tally, in layout order:
	// * 1 16-bit bitset
	// * #triples - 1 16-bit offsets (first one has impicit offset 0)
	// * namesSize bytes for attribute index + name pairs
	
	int triplesCount = __builtin_popcount( triplesBits ); // TODO: Windows!
	
	fLookupData = (U8*)Rtt_MALLOC( L, triplesCount * sizeof(U16) + namesSize );

	memcpy( fLookupData, &triplesBits, sizeof(U16) );
	memcpy( fLookupData + sizeof(U16), offsets, ( triplesCount - 1 ) * sizeof(U16) );
    
    for ( int i = 0, wpos = triplesCount * sizeof(U16), prevTriples = -1; i < extension->count; i++ )
    {
    #if 0
		int pos = 0;
		for ( /*U16 */const char *needle = info[i].name/*offset*/; ( pos < fAttributeCount ) && ( haystack[pos]/*offset*/ != needle ); pos++ ) /* empty */ ;
		
		Rtt_ASSERT( pos < fAttributeCount );
	#endif
	/*
		U8 packed[48];
		
		String::EncodeIdentifier( packed, info[i].name, 16 );
		String::IdentifierLengthToTriples( info[i].length );
		attributes[pos].name_offset;
		attributes[pos].name_triples_minus_1;*/
		
		U16 numTriples = String::IdentifierLengthToTriples( info[i].length );
		if (numTriples == prevTriples)
		{
			fLookupData[wpos - 1] &= ~kFinal; // bad guess: previous identifier was not final
		}
	
		prevTriples = numTriples;

		/*attributes[pos]*/info[i].attribute->name_offset = wpos;
		/*attributes[pos]*/info[i].attribute->name_triples_minus_1 = numTriples - 1;
		
		String::EncodeIdentifier( fLookupData + wpos, info[i].name, 16 );

		wpos += numTriples * 3;

		fLookupData[wpos++] = /* pos */(U8)( info[i].attribute - attributes ) | kFinal; // provisionally make this identifier final one of size
    }
    
//    fAttributeCount = (U16)attributes.Length();
    fAttributes = Rtt_NEW( NULL, Attribute[fAttributeCount] );

    memcpy( fAttributes, attributes/*.ReadAccess()*/, fAttributeCount * sizeof(Attribute) );
    
//    fGroupCount = (U16)groups.Length();
    fGroups = Rtt_NEW( NULL, Group[fGroupCount] );
    
    memcpy( fGroups, groups/*.ReadAccess()*/, fGroupCount * sizeof(Group) );
/*    
    fNames = Rtt_NEW( NULL, FormatExtensionList::NamePair[names.Length()] );

    for (U32 i = 0; i < fAttributeCount; ++i)
    {
        fNames[i].str = names[i];
        fNames[i].index = (S32)i; // preserve attribute index against reordering
    }*/
    // ^^^ TODO: add in recent code
    
	fInstancedByID = !HasInstanceRateData() && !!extension->instanceByID;
    fOwnsData = true;
    // fSorted = true;
}

void
FormatExtensionList::ReconcileFormats( Rtt_Allocator* allocator, CommandBuffer * buffer, const FormatExtensionList * shaderList, const FormatExtensionList * geometryList, U32 offset )
{/*
    Array<Attribute> attributes( allocator );
    Array<Group> groups( allocator );
    Array<U32> groupIndices( allocator );*/
    Attribute attributes[kMaxAttribs];
    Group groups[kMaxAttribs];
    U32 groupIndices[kMaxAttribs];
	U16 attributeCount = 0, groupCount = 0;

    Rtt_ASSERT( geometryList || !shaderList );

//    for (auto iter = FormatExtensionList::AllAttributes( shaderList ); !iter.IsDone(); iter.Advance())
	for ( auto&& iter : FormatExtensionList::AllAttributes( shaderList ) )
    {
    //    S32 geometryAttributeIndex = -1;//geometryList->FindHash( iter.GetAttribute()->nameHash );
		S32 geometryAttributeIndex = geometryList->FindMatchingAttribute( shaderList, iter.attribute );
        
        Rtt_ASSERT( -1 != geometryAttributeIndex );
        
        const Attribute& geometryAttribute = geometryList->fAttributes[geometryAttributeIndex];
        
        attributes[attributeCount++] = geometryAttribute;//.Append( geometryAttribute );
        
        U32 groupIndex = geometryList->FindGroup( geometryAttributeIndex );
        
        bool indexFound = false;

        for (S32 i = 0, length = groupCount/*groupIndices.Length()*/; i < length && !indexFound; i++)
        {
            indexFound = groupIndex == groupIndices[i];
        }

        if ( !indexFound )
        {
            groupIndices[groupCount] = groupIndex;//.Append( groupIndex );
         
            Group group = geometryList->fGroups[groupIndex];
            
            group.count = 0;
            groupIndex = groupCount;// groups.Length();
            
            groups[groupCount] = group;//.Append( group );
            
            ++groupCount;
        }
        
        ++groups[groupIndex].count;
    }
    
    FormatExtensionList reconciledList( groups, groupCount, attributes, attributeCount );// = FormatExtensionList::FromArrays( groups, attributes, groupCount, attributeCount );
    U32 geometryAttributeCount = geometryList ? geometryList->fAttributeCount : 0;
    
    buffer->BindVertexFormat( &reconciledList, geometryAttributeCount, FormatExtensionList::FullVertexRateSizeInBytes/*GetVertexSize*/( geometryList ), offset );
}

FormatExtensionList::Iterator::Iterator( const FormatExtensionList* list, GroupFilter filter, IterationPolicy policy )
:   fList( NULL ),
    fFilter( filter ),
    fPolicy( policy ),
    fFirstInGroup( 0 ),
    fOffsetInGroup( 0 ),
    fGroupIndex( 0 )
{
    if (list && list->fGroupCount > 0)
    {
        fList = list;
        
        if (kInstancedGroups == fFilter && list->HasVertexRateData())
        {
            AdvanceGroup();
        }
        
        UpdateGroup();
    }
}

FormatExtensionList::Iterator
FormatExtensionList::Iterator::begin()
{
	return *this;
	/*
	for ( auto&& iter : *this )
	{
	}*/
}

FormatExtensionList::Iterator
FormatExtensionList::Iterator::end()
{
	return *this; // unused, cf. operator!=()
}

const FormatExtensionList::Iterator::CurrentState
FormatExtensionList::Iterator::operator*() const
{
	CurrentState cur;

	cur.groupIndex = fGroupIndex;
	cur.group = fList->fGroups + cur.groupIndex;
	cur.attributeIndex = fFirstInGroup + fOffsetInGroup;
	cur.attribute = fList->fAttributes + cur.attributeIndex;
	
	return cur;
}

FormatExtensionList::Iterator&
FormatExtensionList::Iterator::operator++()
{
//	Advance();
	bool advanceGroup = kIterateGroups == fPolicy;
	
	if (!advanceGroup)
	{
		Rtt_ASSERT( kIterateAttributes == fPolicy );

		++fOffsetInGroup;
		
		advanceGroup = fOffsetInGroup == fList->fGroups[fGroupIndex].count;
	}
	
	if (advanceGroup)
	{
		if (kVertexRateGroups == fFilter)
		{
			fList = NULL;
		}
		
		else
		{
			AdvanceGroup();
			UpdateGroup();
		}
	}

	return *this;
}

bool
FormatExtensionList::Iterator::operator!=( const Iterator& ) const
{
	return ( NULL != fList );
}
/*
void
FormatExtensionList::Iterator::Advance()
{
    if (!IsDone())
    {
        bool advanceGroup = kIterateGroups == fPolicy;
        
        if (!advanceGroup)
        {
            Rtt_ASSERT( kIterateAttributes == fPolicy );

            ++fOffsetInGroup;
            
            advanceGroup = fOffsetInGroup == fList->fGroups[fGroupIndex].count;
        }
        
        if (advanceGroup)
        {
            if (kVertexRateGroups == fFilter)
            {
                fList = NULL;
            }
            
            else
            {
                AdvanceGroup();
                UpdateGroup();
            }
        }
    }
}

bool
FormatExtensionList::Iterator::IsDone() const
{
    return NULL == fList;
}

U32
FormatExtensionList::Iterator::GetAttributeIndex() const
{
    return fFirstInGroup + fOffsetInGroup;
}

U32
FormatExtensionList::Iterator::GetGroupIndex() const
{
    return fGroupIndex;
}

const FormatExtensionList::Attribute*
FormatExtensionList::Iterator::GetAttribute() const
{
    return fList ? &fList->fAttributes[GetAttributeIndex()] : NULL;
}

const FormatExtensionList::Group*
FormatExtensionList::Iterator::GetGroup() const
{
    return fList ? &fList->fGroups[fGroupIndex] : NULL;
}
*/
void
FormatExtensionList::Iterator::AdvanceGroup()
{
    fFirstInGroup += fList->fGroups[fGroupIndex].count;
    
    ++fGroupIndex;
}

void
FormatExtensionList::Iterator::UpdateGroup()
{
    fOffsetInGroup = 0;

    if (fGroupIndex == fList->fGroupCount)
    {
        fList = NULL;
    }
}

// ----------------------------------------------------------------------------

FormatExtensionList::NamedAttributeIterator::NamedAttributeIterator()
:	fNameData( NULL)
{
}

FormatExtensionList::NamedAttributeIterator::NamedAttributeIterator( const U8* lookupData, U8 specificTriples )
{
	memcpy( &fTriplesBits, lookupData, sizeof(U16) );

	fNameData = lookupData + __builtin_popcount( fTriplesBits ) * sizeof(U16);
	
	if ( 0 != specificTriples )
	{
		U16 mask = 1U << ( specificTriples - 1 );
		if ( fTriplesBits & mask )
		{
			U16 offsetIndex = __builtin_popcount( fTriplesBits & ( mask - 1 ) );
			
			if ( offsetIndex > 0 )
			{
				U16 offset;
				
				// implicitly (offsetIndex - 1), but add 1 to skip `fTriplesBits`
				memcpy( &offset, lookupData + offsetIndex * sizeof(U16), sizeof(U16) );
				
				fNameData += offset;
			}
			
			fTriplesBits = mask;
		}
		else
		{
			fTriplesBits = 0;
		}
	}

	PrepareTripleCount();
}

void
FormatExtensionList::NamedAttributeIterator::PrepareTripleCount()
{
	U16 lsb = fTriplesBits & -fTriplesBits;
	
	fTriples = __builtin_popcount( lsb - 1 ) + 1;
	fAttributeIndex = fTriplesBits ? fNameData[fTriples * 3] : 0;
}

FormatExtensionList::NamedAttributeIterator::CurrentState 
FormatExtensionList::NamedAttributeIterator::operator*() const
{
	CurrentState cur;
	cur.nameData = fNameData;
	cur.attributeIndex = fAttributeIndex & ~kFinal;
	cur.triplesCount = fTriples;

	return cur;
}

FormatExtensionList::NamedAttributeIterator& 
FormatExtensionList::NamedAttributeIterator::operator++()
{
	fNameData += fTriples * 3 + 1;
	
	if ( fAttributeIndex & kFinal )
	{
		fTriplesBits &= fTriplesBits - 1;
		
		PrepareTripleCount();
	}
	else
	{
		fAttributeIndex = fNameData[fTriples * 3];
	}
	
	return *this;
}

bool
FormatExtensionList::NamedAttributeIterator::operator!=( const NamedAttributeIterator& ) const
{
	return ( 0 != fTriplesBits );
}

int
FormatExtensionList::FindAttributeWithName( const char* name ) const
{
	if ( strlen( name ) <= 64 )
	{
		U8 packed[48];
		int n = String::EncodeIdentifier( packed, name, 16 );
		if ( n > 0 )
		{
			return FindMatchingAttribute( packed, n / 3 );
		}
	}
	
	return -1;
}

int
FormatExtensionList::FindMatchingAttribute( const U8* data, U8 triplesCount ) const
{
	int size = triplesCount * 3;
	for ( auto&& iter : NamedAttributesWithTriplesCount( triplesCount ) )
	{
		if ( 0 == memcmp( iter.nameData, data, size ) )
		{
			return iter.attributeIndex;
		}
	}
	
	return -1;
}

int
FormatExtensionList::FindMatchingAttribute( const FormatExtensionList* otherList, const Attribute* otherAttribute ) const
{
	Rtt_ASSERT( otherList );
	Rtt_ASSERT( otherAttribute );

	return FindMatchingAttribute( otherList->fLookupData + otherAttribute->name_offset, otherAttribute->name_triples_minus_1 + 1 );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
