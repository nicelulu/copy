#pragma once

#include <DB/Common/HashTable/Hash.h>
#include <DB/Common/HashTable/HashTable.h>
#include <DB/Common/HashTable/HashTableAllocator.h>

#include <DB/IO/WriteBuffer.h>
#include <DB/IO/WriteHelpers.h>
#include <DB/IO/ReadBuffer.h>
#include <DB/IO/ReadHelpers.h>
#include <DB/IO/VarInt.h>


template
<
	typename Key,
	typename Hash = DefaultHash<Key>,
	typename Grower = HashTableGrower,
	typename Allocator = HashTableAllocator
>
class HashSet : public HashTable<Key, HashTableCell<Key, Hash>, Hash, Grower, Allocator>
{
public:
	typedef HashSet<Key, Hash, Grower, Allocator> Self;
	typedef HashTableCell<Key, Hash> Cell;

	void merge(const Self & rhs)
	{
		if (!this->has_zero && rhs.has_zero)
		{
			this->has_zero = true;
			++this->m_size;
		}

		for (size_t i = 0; i < rhs.grower.bufSize(); ++i)
			if (!rhs.buf[i].isZero())
				this->insert(rhs.buf[i]);
	}


	void readAndMerge(DB::ReadBuffer & rb)
	{
		size_t new_size = 0;
		DB::readVarUInt(new_size, rb);

		this->resize(new_size);

		for (size_t i = 0; i < new_size; ++i)
		{
			Cell x;
			x.read(rb);
			this->insert(x);
		}
	}
};
