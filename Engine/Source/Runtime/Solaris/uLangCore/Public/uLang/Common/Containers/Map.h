// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/HashTable.h"
#include "uLang/Common/Memory/Allocator.h"

namespace uLang
{
template<class KeyType, class ValueType, class HashTraits, class AllocatorType, typename... AllocatorArgsType>
class TMapG
{
    using PairType = TKeyValuePair<KeyType, ValueType>;

public:
    TMapG(AllocatorArgsType&&... AllocatorArgs) : _HashTable(ForwardArg<AllocatorArgsType>(AllocatorArgs)...) {}

    uint32_t Num() const { return _HashTable.Num(); }
    bool Contains(const KeyType& Key) const { return _HashTable.Contains(Key); }
    ValueType* Find(const KeyType& Key) { PairType* Pair = _HashTable.Find(Key); return Pair ? &Pair->_Value : nullptr; }
    const ValueType* Find(const KeyType& Key) const { const PairType* Pair = _HashTable.Find(Key); return Pair ? &Pair->_Value : nullptr; }

    template <typename Predicate>
    const PairType* FindByPredicate(Predicate Pred) const
    {
        return _HashTable.FindByPredicate(Pred);
    }

    template <typename Predicate>
    PairType* FindByPredicate(Predicate Pred)
    {
        return _HashTable.FindByPredicate(Pred);
    }

    PairType& Insert(const KeyType& Key, ValueType&& Value) { return _HashTable.Insert({ Key, Move(Value) }); }
    PairType& Insert(KeyType&& Key, const ValueType& Value) { return _HashTable.Insert({ Move(Key), Value }); }
    PairType& Insert(const KeyType& Key, const ValueType& Value) { return _HashTable.Insert({ Key, ValueType(Value) }); }
    PairType& Insert(KeyType&& Key, ValueType&& Value) { return _HashTable.Insert({ Move(Key), Move(Value) }); }
    
    template <typename OtherKeyType, typename OtherValueType>
    PairType& FindOrInsert(OtherKeyType&& Key, OtherValueType&& Value)
    {
        return _HashTable.FindOrInsert({ForwardArg<OtherKeyType>(Key), ForwardArg<OtherValueType>(Value)});
    }
    
    template <typename OtherKeyType>
    PairType& FindOrInsert(OtherKeyType&& Key)
    {
        return FindOrInsert(ForwardArg<OtherKeyType>(Key), ValueType{});
    }

    bool Remove(const KeyType& Key) { return _HashTable.Remove(Key); }
    void Empty() { return _HashTable.Empty(); }

    ValueType& operator[](const KeyType& Key)
    {
        PairType* Pair = _HashTable.Find(Key);
        if (Pair)
        {
            return Pair->_Value;
        }
        else
        {
            ValueType DefaultValue{};
            KeyType NewKey(Key);
            Insert(Move(NewKey), Move(DefaultValue));
            return _HashTable.Find(Key)->_Value;
        }
    }

    using HashTableType = THashTable<KeyType, PairType, HashTraits, AllocatorType, AllocatorArgsType...>;
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

// A map that assumes that the KeyType has a method `GetTypeHash`(), and that allocates memory from the heap
template<class KeyType, class ValueType>
using TMap = TMapG<KeyType, ValueType, TDefaultHashTraits<KeyType>, CHeapRawAllocator>;

} // namespace uLang
