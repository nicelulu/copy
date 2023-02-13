/**
 * Copyright 2016-2023 ClickHouse, Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <Common/HashTable/FixedHashTable.h>

template <typename Key, typename Allocator = HashTableAllocator>
class FixedHashSet : public FixedHashTable<Key, FixedHashTableCell<Key>, FixedHashTableStoredSize<FixedHashTableCell<Key>>, Allocator>
{
public:
    using Cell = FixedHashTableCell<Key>;
    using Base = FixedHashTable<Key, Cell, FixedHashTableStoredSize<Cell>, Allocator>;
    using Self = FixedHashSet;

    void merge(const Self & rhs)
    {
        for (size_t i = 0; i < Base::BUFFER_SIZE; ++i)
            if (Base::buf[i].isZero(*this) && !rhs.buf[i].isZero(*this))
                new (&Base::buf[i]) Cell(rhs.buf[i]);
    }

    /// NOTE: Currently this method isn't used. When it does, the ReadBuffer should
    ///  contain the Key explicitly.
    // void readAndMerge(RK::ReadBuffer & rb)
    // {

    // }
};
