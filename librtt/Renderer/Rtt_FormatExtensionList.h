//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Format_Extension_List_H__
#define _Rtt_Format_Extension_List_H__

#include "Core/Rtt_Array.h"
#include "Core/Rtt_Types.h"

// ----------------------------------------------------------------------------

struct CoronaVertexExtension;

namespace Rtt
{

class CommandBuffer;
class String;

// ----------------------------------------------------------------------------

class FormatExtensionList {
    public:
		struct Group;
		struct Attribute;
		
        FormatExtensionList( Group* groups = NULL, U16 groupCount = 0, Attribute* attributes = NULL, U16 attributeCount = 0 );
        ~FormatExtensionList();
        
    public:
        struct Attribute {
        //    U32 nameHash;
            U32 type : 4; // adequate in GL 4.6 / ES 3.2; other backends might call for redesign?
            U32 offset : 10; // fully saturated = 32 dvec4 values = 32 * 8 * 4 = 2^10
            U32 name_triples_minus_1 : 4; // 1..16 g
            U32 name_offset : 11; // fully saturated = 32 48-byte names = 2^5 * 2^5 * 1.5 < 2^11, with ~ 512 wiggle room (header <= 32, easy fit)
            U32 comp_minus_1 : 2; // 1..4
            U32 normalized : 1;

            U32 GetSize() const;
            U16 GetComponentCount() const { return comp_minus_1 + 1; }
            bool IsFloat() const;
        };
    
        struct Group {
            U32 divisor;
            U16 count;
            U16 size;
            
            bool IsInstanceRate() const { return 0 != divisor; }
            bool IsWindowed() const { return 0 == size; }
            bool NeedsDivisor() const { return divisor > 1; }
            U32 GetWindowAttributeCount( U32 divisorChunks/*valueCount*/ ) const;
            U32 /*GetValueCount*/InstanceStreamSizeInDivisorChunks( U32 instanceCount ) const;
            /*size_t*/U32 /*GetDataSize*/InstanceStreamSizeInBytes( U32 instanceCount, const Attribute * firstAttribute ) const;
            U32 /*GetVertexCount*/InstanceStreamSizeInVertices( U32 instanceCount, const Attribute * firstAttribute ) const;
        };
   
	public:
        class Iterator {
        public:
            enum GroupFilter { kAllGroups, kVertexRateGroups, kInstancedGroups };
            enum IterationPolicy { kIterateGroups, kIterateAttributes };
        
            Iterator( const FormatExtensionList* list, GroupFilter filter, IterationPolicy policy );
        
        public:
			struct CurrentState {
				const Attribute* attribute;
				const Group* group;
				U32 attributeIndex;
				U32 groupIndex;
			};
			
			Iterator begin();
			Iterator end();
			
			const CurrentState operator*() const;
			
			Iterator& operator++();
			bool operator!=( const Iterator& ) const;

        public:
        /*    void Advance();
            bool IsDone() const;
            U32 GetAttributeIndex() const;
            U32 GetGroupIndex() const;
            const Attribute* GetAttribute() const;
            const Group* GetGroup() const;*/
        
        private:
            void AdvanceGroup();
            void UpdateGroup();

        private:
            GroupFilter fFilter;
            IterationPolicy fPolicy;
            const FormatExtensionList * fList;
            U32 fFirstInGroup;
            U32 fOffsetInGroup;
            U32 fGroupIndex;
        };
    
    public:
        static Iterator AllGroups( const FormatExtensionList* list );
        static Iterator AllAttributes( const FormatExtensionList* list );
        static Iterator InstancedGroups( const FormatExtensionList* list );
    #if 0
        static FormatExtensionList FromArrays( const Group groups[], const Attribute attributes[], U16 groupCount, U16 attributeCount );//const Array<Group>& groups, const Array<Attribute>& attributes );
#endif
	public:
		class NamedAttributeIterator {
		public:
			NamedAttributeIterator();
			NamedAttributeIterator( const U8* lookupData, U8 specificTriples );
			
			struct CurrentState {
				const U8 *nameData;
				U8 attributeIndex;
				U8 triplesCount;
			};
			
			CurrentState operator*() const;
			
		private:
			void PrepareTripleCount();
			
		public:
			NamedAttributeIterator& operator++();
			bool operator!=( const NamedAttributeIterator& ) const;
			
		private:
			const U8 *fNameData;
			U16 fTriplesBits;
			U8 fAttributeIndex;
			U8 fTriples;
		};

		class NamedAttributeRange {
		public:
			NamedAttributeRange( const U8* lookupData, U8 specificTriples )
			:	fLookupData( lookupData ),
				fSpecificTriples( specificTriples )
			{
			}
			
			NamedAttributeIterator begin() const { return NamedAttributeIterator( fLookupData, fSpecificTriples ); }
			NamedAttributeIterator end() const { return NamedAttributeIterator(); }
			
		private:
			const U8 *fLookupData;
			U8 fSpecificTriples;
		};

	public:
		NamedAttributeRange NamedAttributes() const { return NamedAttributeRange( fLookupData, 0 ); }
		NamedAttributeRange NamedAttributesWithTriplesCount( U8 triplesCount ) const { return NamedAttributeRange( fLookupData, triplesCount ); }

		const U8* FindAttributeNameData( const Attribute* attribute ) const { return fLookupData + attribute->name_offset; }
		int FindAttributeWithName( const char* name ) const;
		int FindMatchingAttribute( const U8* data, U8 triplesCount ) const;
		int FindMatchingAttribute( const FormatExtensionList* otherList, const Attribute* otherAttribute ) const;

    public:
        U32 /*ExtraVertexCount*/ExtraVertexRateSizeInVertices() const;
        U32 InstanceGroupCount() const;
	    bool IsInstanced() const { return fInstancedByID || HasInstanceRateData(); }
        bool IsInstancedByID() const { return fInstancedByID; }
        bool HasInstanceRateData() const;
        bool HasVertexRateData() const;
 //       void SortNames() const;
 //       const char* FindNameByAttribute( U32 attributeIndex, S32* index = NULL ) const;
 //       S32 FindHash( size_t hash ) const;
 //       S32 FindName( const char* name ) const;
        U32 FindGroup( U32 attributeIndex ) const;
        S32 FindCorrespondingInstanceGroup( const Group* group, const Attribute* attribute, const U8* nameData, U32 * attributeIndex ) const;
    
    public:
        const Attribute* GetAttributes() const { return fAttributes; }
        const Group* GetGroups() const { return fGroups; }
        U16 GetAttributeCount() const { return fAttributeCount; }
        U16 GetGroupCount() const { return fGroupCount; }

    public:
        static /*size_t*/U32 /*GetExtraVertexSize*/ExtraVertexRateSizeInBytes( const FormatExtensionList * list );
        static /*size_t*/U32 /*GetVertexSize*/FullVertexRateSizeInBytes( const FormatExtensionList * list );
        static bool Compatible( const FormatExtensionList * shaderList, const FormatExtensionList * geometryList );
        static bool Match( const FormatExtensionList * list1, const FormatExtensionList * list2 );
        static void ReconcileFormats( Rtt_Allocator* allocator, CommandBuffer * buffer, const FormatExtensionList * shaderList, const FormatExtensionList * geometryList, U32 offset );
    
    public:
		struct NamedAttributeInfo/*Pair*/ {
        //    String* str;
			const char *name;
			Attribute *attribute;
            //S32 index;
            //U8 index;
        //    U16 offset;
            U16 length;
            //U8 suffix; // TODO: prefer array...
        
            bool operator<( const NamedAttributeInfo/*Pair*/ & other ) const;// const { return length < other.length; }// return index < other.index; }
        };
        
        void Build( Rtt_Allocator* allocator, const CoronaVertexExtension * extension );//, const NamedAttributeInfo info[] );
    
    public:
		enum {
			kFinal = 0x80,
			kMaxAttribs = 32 // could be up to 128 (just has to not fight kFinal), but hardware is not usually
							 // so generous; also, this is the sweet spot for the Attribute bit-packing
		};
    
    private:
        Attribute * fAttributes;
        Group * fGroups;
        U8 * fLookupData; // 1 + N U16 headers; U8* name content follows
//        mutable NamePair* fNames; // allow reordering
        U16 fAttributeCount;
        U16 fGroupCount;
	    bool fInstancedByID;
        bool fOwnsData;
//        mutable bool fSorted;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Format_Extension_List_H__
