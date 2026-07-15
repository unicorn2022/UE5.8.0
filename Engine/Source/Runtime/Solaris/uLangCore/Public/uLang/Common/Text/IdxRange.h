// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"

namespace uLang
{

/**
 * Specifies a range using both beginning and ending indexes.
 * Often used with CUTF8StringView and SAxisRange (row and column)
 * CUTF8StringView can convert to and from SIdxRange and SAxisRange.
 **/
struct SIdxRange
{
public:

    // Public Data Members

    uint32_t _Begin;  ///< Beginning of index range
    uint32_t _End;    ///< End of index range

    // Construction

    SIdxRange() : _Begin(0u), _End(0u)                                 {}
    SIdxRange(ENoInit)                                                 {} // Do nothing - use with care!
    SIdxRange(uint32_t Length) : _Begin(0u), _End(Length)              {}
    SIdxRange(uint32_t Begin, uint32_t End) : _Begin(Begin), _End(End) {}
    static SIdxRange MakeSpan(uint32_t Begin, uint32_t Length)         { return SIdxRange(Begin, Begin + Length); }
    void Reset()                                                       { _Begin = _End = 0u; }

    // Accessors

    uint32_t GetLength() const                { return _End - _Begin; }
    bool IsEmpty() const                      { return _Begin == _End; }
    bool IsOrdered() const                    { return _Begin <= _End; }
    void Set(uint32_t Begin, uint32_t End)    { _Begin = Begin; _End = End; }
    void AdvanceToEnd()                       { _Begin = _End; }

    // Comparisons

    bool operator==(const SIdxRange & Other) const  { return ((_Begin == Other._Begin) && (_End == Other._End)); }
    bool operator!=(const SIdxRange & Other) const  { return ((_Begin != Other._Begin) || (_End != Other._End)); }

};  // SIdxRange

}