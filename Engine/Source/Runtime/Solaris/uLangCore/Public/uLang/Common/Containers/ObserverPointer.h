// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/UniquePointer.h"

#ifndef ULANG_CHECK_OBSERVER_POINTERS
    #define ULANG_CHECK_OBSERVER_POINTERS ULANG_DO_CHECK
#endif

namespace uLang
{

#if ULANG_CHECK_OBSERVER_POINTERS

template<class ObjectType> class TOPtr;

/**
 * A convenience mixin class, storing an observer id
 * Derive from this if you want an object to be observed by a TObserverPointer
 */
class CObservedMix
{
public:

    CObservedMix() : _ObserverId(ObserverId_Null) {}
    ~CObservedMix() { _ObserverId = ObserverId_Null; }

private:

    template<class ObjectType> friend class TOPtr;

    mutable EObserverId _ObserverId;

};

/**
 * Observer pointer using a unique id to tell if an object has gone stale
*/
template<class ObjectType>
class TOPtr
{
public:

    // Construction

    TOPtr() : _Object(nullptr), _ObserverId(ObserverId_Null) {}
    TOPtr(const TOPtr & Other) : _Object(Other._Object), _ObserverId(Other._ObserverId) {}
    template<class OtherObjectType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*>)
    TOPtr(const TOPtr<OtherObjectType> & Other) : _Object(Other._Object), _ObserverId(Other._ObserverId) {}
    template<class OtherObjectType, bool OtherAllowNull, class AllocatorType, typename... AllocatorArgsType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*>)
    TOPtr(const TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & SharedPtr) : _Object(SharedPtr.Get()), _ObserverId(GetId(SharedPtr.Get(), SharedPtr.GetAllocator())) {}
    template<class OtherObjectType, bool OtherAllowNull, class AllocatorType, typename... AllocatorArgsType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*>)
    TOPtr(const TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & UniquePtr) : _Object(UniquePtr.Get()), _ObserverId(GetId(UniquePtr.Get(), UniquePtr.GetAllocator())) {}

    // Assignment

    TOPtr & operator=(const TOPtr & Other) { _Object = Other._Object; _ObserverId = Other._ObserverId; return *this; }
    template<class OtherObjectType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*>)
    TOPtr & operator=(const TOPtr<OtherObjectType> & Other) { _Object = Other._Object; _ObserverId = Other._ObserverId; return *this; }
    template<class OtherObjectType, bool OtherAllowNull, class AllocatorType, typename... AllocatorArgsType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*>)
    TOPtr & operator=(const TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & SharedPtr) { _Object = SharedPtr.Get(); _ObserverId = GetId(SharedPtr.Get(), SharedPtr.GetAllocator()); return *this; }
    template<class OtherObjectType, bool OtherAllowNull, class AllocatorType, typename... AllocatorArgsType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*>)
    TOPtr & operator=(const TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & UniquePtr) { _Object = UniquePtr.Get(); _ObserverId = GetId(UniquePtr.Get(), UniquePtr.GetAllocator()); return *this; }

    // Conversion methods

    operator ObjectType*() const { return Get(); }
    ObjectType &       operator*() const { return *Get(); }
    ObjectType *       operator->() const { return Get(); }
    ObjectType *       Get() const { ULANG_ASSERTF(!_Object || _ObserverId == _Object->_ObserverId, "Observed object has been deleted!"); return _Object; }
    void               Reset() { _Object = nullptr; _ObserverId = ObserverId_Null; }

    // Comparison operators

    bool operator==(const TOPtr & Other) const { return (_Object == Other._Object); }
    bool operator!=(const TOPtr & Other) const { return (_Object != Other._Object); }
    template<class OtherObjectType>
    bool operator==(const TOPtr<OtherObjectType> & Other) const { return (_Object == Other._Object); }
    template<class OtherObjectType>
    bool operator!=(const TOPtr<OtherObjectType> & Other) const { return (_Object != Other._Object); }
    template<class OtherObjectType>
    bool operator==(OtherObjectType * Object) const { return (_Object == Object); }
    template<class OtherObjectType>
    bool operator!=(OtherObjectType * Object) const { return (_Object != Object); }

    // Validation methods

    bool IsValid() const { return (_Object && (_ObserverId == _Object->_ObserverId)); }
    bool IsStale() const { return (_Object && (_ObserverId != _Object->_ObserverId)); }
    bool IsNull() const { return !_Object; }
    bool IsSet() const { return (_Object != nullptr); }
    operator bool() const { return IsValid(); }
    bool operator !() const { return IsNull(); }

    // Id Accessor

    EObserverId GetObserverId() const { return _ObserverId; }

protected:
    template<class OtherObjectType> friend class TOPtr;

    template<class AllocatorType>
    static EObserverId GetId(const ObjectType * Object, const AllocatorType & Allocator)
    {
        EObserverId ObserverId = Object->_ObserverId;
        if (ObserverId == ObserverId_Null)
        {
            Object->_ObserverId = ObserverId = Allocator.GenerateObserverId();
        }
        return ObserverId;
    }

    /// Direct pointer to actual object - might be stale
    /// It is only valid if its _ObserverId is the same as _ObserverId in this smart pointer
    ObjectType * _Object;

    /// Unique id shared between the object and its id smart pointers
    EObserverId _ObserverId;

};

#else // !ULANG_CHECK_OBSERVER_POINTERS

/**
 * Inert version of CObservedMix
 */
class CObservedMix
{
};

/**
 * Non-checked version of TOPtr
 */
template<class ObjectType>
class TOPtr
{
public:

    // Construction

    TOPtr() : _Object(nullptr) {}
    TOPtr(const TOPtr & Other) : _Object(Other._Object) {}
    template<class OtherObjectType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*>)
    TOPtr(const TOPtr<OtherObjectType> & Other) : _Object(Other._Object) {}
    template<class OtherObjectType, bool OtherAllowNull, class AllocatorType, typename... AllocatorArgsType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*>)
    TOPtr(const TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & SharedPtr) : _Object(SharedPtr.Get()) {}
    template<class OtherObjectType, bool OtherAllowNull, class AllocatorType, typename... AllocatorArgsType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*>)
    TOPtr(const TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & UniquePtr) : _Object(UniquePtr.Get()) {}

    // Assignment

    TOPtr & operator=(const TOPtr & Other) { _Object = Other._Object; return *this; }
    template<class OtherObjectType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*>)
    TOPtr & operator=(const TOPtr<OtherObjectType> & Other) { _Object = Other._Object; return *this; }
    template<class OtherObjectType, bool OtherAllowNull, class AllocatorType, typename... AllocatorArgsType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*>)
    TOPtr & operator=(const TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & SharedPtr) { _Object = SharedPtr.Get(); return *this; }
    template<class OtherObjectType, bool OtherAllowNull, class AllocatorType, typename... AllocatorArgsType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*>)
    TOPtr & operator=(const TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & UniquePtr) { _Object = UniquePtr.Get(); return *this; }

    // Conversion methods

    operator ObjectType*() const { return _Object; }
    ObjectType &       operator*() const { return *_Object; }
    ObjectType *       operator->() const { return _Object; }
    ObjectType *       Get() const { return _Object; }
    void               Reset() { _Object = nullptr; }

    // Comparison operators

    bool operator==(const TOPtr & Other) const { return (_Object == Other._Object); }
    bool operator!=(const TOPtr & Other) const { return (_Object != Other._Object); }
    template<class OtherObjectType>
    bool operator==(const TOPtr<OtherObjectType> & Other) const { return (_Object == Other._Object); }
    template<class OtherObjectType>
    bool operator!=(const TOPtr<OtherObjectType> & Other) const { return (_Object != Other._Object); }
    bool operator==(const ObjectType * Object) const { return (_Object == Object); }
    bool operator!=(const ObjectType * Object) const { return (_Object != Object); }

    // Validation methods

    bool IsValid() const { return _Object != nullptr; }
    bool IsStale() const { return false; }
    bool IsNull() const { return _Object == nullptr; }
    bool IsSet() const { return _Object != nullptr; }
    operator bool() const { return IsValid(); }
    bool operator !() const { return IsNull(); }

protected:
    template<class OtherObjectType> friend class TOPtr;

    /// Direct pointer to actual object
    ObjectType * _Object;

};


#endif

}