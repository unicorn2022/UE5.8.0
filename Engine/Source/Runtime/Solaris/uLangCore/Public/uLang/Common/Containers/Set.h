// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/HashTable.h"
#include "uLang/Common/Memory/Allocator.h"

namespace uLang
{
template<class ElementType, class KeyType, class HashTraits, class AllocatorType, typename... AllocatorArgsType>
class TSetG
{
public:
    explicit TSetG(AllocatorArgsType&&... AllocatorArgs) : _HashTable(Move(AllocatorArgs)...) {}

    uint32_t Num() const { return _HashTable.Num(); }
    bool Contains(const ElementType& Element) const { return _HashTable.Contains(Element); }
    ElementType* Find(const KeyType& Key) { return _HashTable.Find(Key); }
    const ElementType* Find(const KeyType& Key) const { return _HashTable.Find(Key); }

    template <typename Predicate>
    const ElementType* FindByPredicate(Predicate Pred) const
    {
        return _HashTable.FindByPredicate(Pred);
    }

    template <typename Predicate>
    ElementType* FindByPredicate(Predicate Pred)
    {
        return _HashTable.FindByPredicate(Pred);
    }

    template <typename ArgType>
    ElementType& Insert(ArgType&& Arg)
    {
        return _HashTable.Insert(ForwardArg<ArgType>(Arg));
    }

    ElementType& FindOrInsert(ElementType&& Element) { return _HashTable.FindOrInsert(ForwardArg<ElementType>(Element)); }
    bool Remove(const ElementType& Element) { return _HashTable.Remove(Element); }

    bool IsEmpty() const { return _HashTable.IsEmpty(); }

    void Empty() { _HashTable.Empty(); }

    using HashTableType = THashTable<ElementType, ElementType, HashTraits, AllocatorType, AllocatorArgsType...>;
    using ConstIterator = typename HashTableType::template Iterator<true>;
    using Iterator = typename HashTableType::template Iterator<false>;

    Iterator begin()
    {
        return _HashTable.begin();
    }

    Iterator end()
    {
        return _HashTable.end();
    }

    ConstIterator begin() const
    {
        return _HashTable.cbegin();
    }

    ConstIterator end() const
    {
        return _HashTable.cend();
    }

    ConstIterator cbegin() const
    {
        return _HashTable.cbegin();
    }

    ConstIterator cend() const
    {
        return _HashTable.cend();
    }

protected:
    HashTableType _HashTable;
};

// A set that assumes that the KeyType has a method `GetTypeHash()`, and that allocates memory from the heap
template<class ElementType, class KeyType = ElementType>
using TSet = TSetG<ElementType, KeyType, TDefaultHashTraits<ElementType>, CHeapRawAllocator>;

} // namespace uLang
