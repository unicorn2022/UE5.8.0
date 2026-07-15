// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Memory/Allocator.h"
#include "uLang/Common/Templates/References.h"

namespace uLang
{

template<class ObjectType, bool AllowNull, class AllocatorType, typename... AllocatorArgsType> class TUPtrArrayG;

template <typename ObjectType, bool AllowNull, class AllocatorType, typename... AllocatorArgsType>
class TUPtrG
{
public:

    // Construction

    TUPtrG(NullPtrType NullPtr = nullptr) : _Object(nullptr), _Allocator(DefaultInit) { static_assert(AllowNull, "Cannot default construct a unique reference, as it is not allowed to be null."); }
    TUPtrG(TUPtrG && Other) : _Object(Other._Object), _Allocator(Other._Allocator) { Other._Object = nullptr; } // Note: We null here even when AllowNull==false since 99% likely will be destructed immediately after anyway
    template<class OtherObjectType, bool OtherAllowNull>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*> && (AllowNull || !OtherAllowNull))
    TUPtrG(TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> && Other) : _Object(Other._Object), _Allocator(Other._Allocator) { Other._Object = nullptr; } // Note: We null here even when AllowNull==false since 99% likely will be destructed immediately after anyway
    ~TUPtrG() { if (_Object) Release(); }

    template<typename... CtorArgsType>
    TUPtrG& SetNew(CtorArgsType&&... CtorArgs) { if (_Object) { Release(); } _Object = new(_Allocator) ObjectType(uLang::ForwardArg<CtorArgsType>(CtorArgs)...); return *this; }
    template<typename... CtorArgsType>
    static TUPtrG New(AllocatorArgsType&&... AllocatorArgs, CtorArgsType&&... CtorArgs) { AllocatorType Allocator(AllocatorArgs...); ObjectType * Object = new(Allocator) ObjectType(uLang::ForwardArg<CtorArgsType>(CtorArgs)...); return TUPtrG(Object, uLang::Move(Allocator)); }

    // Non-copyable

    TUPtrG(const TUPtrG &) = delete;
    template<class OtherObjectType, bool OtherAllowNull> TUPtrG(const TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other) = delete;
    TUPtrG& operator=(const TUPtrG &) = delete;
    template<class OtherObjectType, bool OtherAllowNull> TUPtrG & operator=(const TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other) = delete;

    // Assignment

    TUPtrG & operator=(NullPtrType) { Reset(); return *this; }
    TUPtrG & operator=(TUPtrG && Other) { return AssignMove(ForwardArg<TUPtrG>(Other)); }
    template<class OtherObjectType, bool OtherAllowNull>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*> && (AllowNull || !OtherAllowNull))
    TUPtrG & operator=(TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> && Other) { return AssignMove(ForwardArg<TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...>>(Other)); }

    // Conversion methods

    operator ObjectType*() const { return _Object; }
    ObjectType &          operator*() const { return *_Object; }
    ObjectType *          operator->() const { return _Object; }
    ObjectType *          Get() const { return _Object; }
    const AllocatorType & GetAllocator() const { return _Allocator; }
    void                  Reset() { if (_Object) { Release(); _Object = nullptr; } }

    TUPtrG<ObjectType, false, AllocatorType, AllocatorArgsType...>& AsRef() &
    {
        static_assert(AllowNull, "Unnecessary conversion!");
        ULANG_ASSERTF(_Object, "Converting null pointer to reference!");
        return reinterpret_cast<TUPtrG<ObjectType, false, AllocatorType, AllocatorArgsType...>&>(*this);
    }
    const TUPtrG<ObjectType, false, AllocatorType, AllocatorArgsType...>&& AsRef() &&
    {
        static_assert(AllowNull, "Unnecessary conversion!");
        ULANG_ASSERTF(_Object, "Converting null pointer to reference!");
        return reinterpret_cast<const TUPtrG<ObjectType, false, AllocatorType, AllocatorArgsType...>&&>(*this);
    }
    const TUPtrG<ObjectType, false, AllocatorType, AllocatorArgsType...>& AsRef() const&
    {
        static_assert(AllowNull, "Unnecessary conversion!");
        ULANG_ASSERTF(_Object, "Converting null pointer to reference!");
        return reinterpret_cast<const TUPtrG<ObjectType, false, AllocatorType, AllocatorArgsType...>&>(*this);
    }
    template<class OtherObjectType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*> || std::is_convertible_v<ObjectType*, OtherObjectType*>)
    TUPtrG<OtherObjectType, AllowNull, AllocatorType, AllocatorArgsType...>& As() { return *reinterpret_cast<TUPtrG<OtherObjectType, AllowNull, AllocatorType, AllocatorArgsType...> *>(this); }
    template<class OtherObjectType>
    requires (std::is_convertible_v<OtherObjectType*, ObjectType*> || std::is_convertible_v<ObjectType*, OtherObjectType*>)
    const TUPtrG<OtherObjectType, AllowNull, AllocatorType, AllocatorArgsType...>& As() const { return *reinterpret_cast<const TUPtrG<OtherObjectType, AllowNull, AllocatorType, AllocatorArgsType...> *>(this); }

    // Comparison operators

    bool operator==(NullPtrType)  const { return !_Object; }
    bool operator==(const TUPtrG & Other)  const { return _Object == Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    bool operator==(const TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other)  const { return _Object == Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    bool operator==(OtherObjectType * Object) const { return _Object == Object; }
    bool operator!=(const TUPtrG & Other)  const { return _Object != Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    bool operator!=(const TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other)  const { return _Object != Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    bool operator!=(OtherObjectType * Object) const { return _Object != Object; }
    bool operator< (const TUPtrG & Other)  const { return _Object < Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    bool operator< (const TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other)  const { return _Object < Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    bool operator< (OtherObjectType * Object) const { return _Object < Object; }
    bool operator> (const TUPtrG & Other)  const { return _Object > Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    bool operator> (const TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other)  const { return _Object > Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    bool operator> (OtherObjectType * Object) const { return _Object > Object; }

    // Validation methods

    operator bool() { return (_Object != nullptr); }
    operator bool() const { return (_Object != nullptr); }
    bool operator!() const { return (_Object == nullptr); }
    bool IsValid() const { return (_Object != nullptr); }

private:
    template<class OtherObjectType, bool OtherAllowNull, class OtherAllocatorType, typename... OtherAllocatorArgsType> friend class TUPtrG;
    template<class OtherObjectType, bool OtherAllowNull, class OtherAllocatorType, typename... OtherAllocatorArgsType> friend class TUPtrArrayG;

    TUPtrG(ObjectType * Object, const AllocatorType & Allocator)
        : _Object(Object)
        , _Allocator(Allocator)
    {
    }

    template<class OtherObjectType, bool OtherAllowNull>
    TUPtrG & AssignMove(TUPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> && Other)
    {
        if (_Object)
        {
            Release();
        }

        _Object = Other._Object;
        _Allocator = Other._Allocator;
        Other._Object = nullptr;

        return *this;
    }

    /// Let go of our object
    void Release()
    {
        _Object->~ObjectType();
        _Allocator.Deallocate(_Object);
    }

    /// Pointer to original object
    ObjectType* _Object;

    /// How to deallocate the object
    /// This allocator can be 0 in size
    AllocatorType _Allocator;
};

/// Unique pointer that allocates object on the heap
template<class ObjectType>
using TUPtr = TUPtrG<ObjectType, true, CHeapRawAllocator>;

/// Unique reference that allocates object on the heap
template<class ObjectType>
using TURef = TUPtrG<ObjectType, false, CHeapRawAllocator>;

/// Unique pointer that allocates object using a given allocator instance
template<class ObjectType>
using TUPtrA = TUPtrG<ObjectType, true, CInstancedRawAllocator, CAllocatorInstance *>;

/// Unique reference that allocates object using a given allocator instance
template<class ObjectType>
using TURefA = TUPtrG<ObjectType, false, CInstancedRawAllocator, CAllocatorInstance *>;


}