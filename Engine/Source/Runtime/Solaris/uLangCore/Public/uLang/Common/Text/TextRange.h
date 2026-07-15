// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Misc/MathUtils.h"

namespace uLang
{

/**
 * Position in a text document/string expressed as zero-based row/line and zero-based
 * column/character offset. A position is between two characters like an ‘insert’ cursor
 * in an editor.  The column is a byte offset into the UTF-8 encoded line.
 *
 * Similar to a LSP Position - https://microsoft.github.io/language-server-protocol/specifications/specification-3-14/#position
 * Also similar to UE `FIntPoint`
 **/
struct STextPosition
{
public:
   // Public Data Members

    uint32_t _Row    = uint32_invalid;
    uint32_t _Column = uint32_invalid;

    // Accessors

    void Invalidate()               { _Row = _Column = uint32_invalid; }
    void Reset()                    { _Row = _Column = 0u; }
    bool IsValid() const            { return _Row != uint32_invalid; }
    bool IsInvalid() const          { return _Row == uint32_invalid; }

    bool operator==(const STextPosition& Other) const { return _Row == Other._Row && _Column == Other._Column; }
    bool operator!=(const STextPosition& Other) const { return _Row != Other._Row || _Column != Other._Column; }
    bool operator<=(const STextPosition& Other) const { return (_Row < Other._Row) || ((_Row == Other._Row) && (_Column <= Other._Column)); }
    bool operator< (const STextPosition& Other) const { return (_Row < Other._Row) || ((_Row == Other._Row) && (_Column <  Other._Column)); }
    bool operator>=(const STextPosition& Other) const { return (_Row > Other._Row) || ((_Row == Other._Row) && (_Column >= Other._Column)); }
    bool operator> (const STextPosition& Other) const { return (_Row > Other._Row) || ((_Row == Other._Row) && (_Column > Other._Column)); }
};

/**
 * A range in a text document/string expressed as (zero-based) begin and end row/column.
 * A range is comparable to a selection in an editor. Therefore the end position is
 * exclusive. Columns are byte offsets into the UTF-8 encoded line.
 *
 * To specify a range that contains a line including the line ending character(s), use an
 * end position denoting the start of the next line (or) same row and the column just
 * past the last character on the row.
 *
 * Often used with CUTF8StringView and SIdxRange.
 * CUTF8StringView should do the converting to and from SIdxRange and STextRange.
 *
 * Similar to a LSP Range - https://microsoft.github.io/language-server-protocol/specifications/specification-3-14/#range
 * Also similar to UE `FIntRect`
 **/
struct STextRange
{
public:

    // TODO-uLang: Revisit - Could use two STextPosition structures instead of four integers.

    uint32_t BeginRow()    const  { return _Begin._Row; }
    uint32_t BeginColumn() const  { return _Begin._Column; }
    uint32_t EndRow()      const  { return _End._Row; }
    uint32_t EndColumn()   const  { return _End._Column; }

    void SetBeginRow(uint32_t Row)    { _Begin._Row = Row; }
    void SetBeginColumn(uint32_t Col) { _Begin._Column = Col; }
    void SetEndRow(uint32_t Row)      { _End._Row = Row; }
    void SetEndColumn(uint32_t Col)   { _End._Column = Col; }

    // Construction

    STextRange() : _Begin{ uint32_invalid, uint32_invalid}, _End{ uint32_invalid, uint32_invalid}  {}
    STextRange(ENoInit)  {} // Do nothing - use with care!
    STextRange(uint32_t BeginRow, uint32_t BeginColumn, uint32_t EndRow, uint32_t EndColumn) : _Begin{BeginRow, BeginColumn}, _End{EndRow, EndColumn}  {}
    STextRange(const STextPosition& Begin, const STextPosition& End) : _Begin(Begin), _End(End)  {}
    STextRange(const STextPosition& BeginAndEnd) : _Begin(BeginAndEnd), _End(BeginAndEnd) {}

    // Accessors

    STextPosition GetBegin() const                      { return _Begin; }
    STextPosition GetEnd() const                        { return _End; }
    void          SetBegin(const STextPosition& Begin)  { _Begin = Begin; }
    void          SetEnd(const STextPosition& End)      { _End = End; }

    void Invalidate()           { _Begin.Invalidate(); _End.Invalidate(); }
    void InvalidateEnd()        { _End.Invalidate(); }
    void Reset()                { _Begin.Reset(); _End.Reset(); }
    bool IsEmpty() const        { return (_Begin == _End); }
    bool IsOrdered() const      { return (_Begin <= _End); }
    bool IsRowSingle() const    { return _Begin._Row == _End._Row; }
    bool IsRowMulti() const     { return _Begin._Row != _End._Row; }
    bool IsInvalid() const      { return _Begin._Row == uint32_invalid; }
    bool IsValid() const        { return _Begin._Row != uint32_invalid; }
    bool IsInvalidEnd() const   { return _End._Row == uint32_invalid; }
    bool IsValidEnd() const     { return _End._Row != uint32_invalid; }

    // Comparisons

    bool operator==(const STextRange & Other) const  { return (_Begin == Other._Begin && _End == Other._End); }
    bool operator!=(const STextRange & Other) const  { return (_Begin != Other._Begin || _End != Other._End); }

    bool Overlaps(const STextRange& Other) const { return CMath::Max(_Begin, Other._Begin) < CMath::Min(_End, Other._End); } // = "there is at least one byte of overlap"
    bool IsContainedIn(const STextRange& ContainingRange) const { return _Begin >= ContainingRange._Begin && _Begin <= ContainingRange._End && _End >= ContainingRange._Begin && _End <= ContainingRange._End; } // = "no character of this is outside of ContainingRange"

    bool IsInRange(const STextPosition& Position) const { return _Begin <= Position && Position < _End; }
    bool IsInRangeInclusive(const STextPosition& Position) const { return _Begin <= Position && Position <= _End; }

    // Operations

    // Compute the union of two text ranges
    STextRange operator|(const STextRange& Other) const
    {
        return STextRange(
            _Begin < Other._Begin ? _Begin : Other._Begin,
            _End > Other._End ? _End : Other._End);
    }

    // Make this text range the union of this and another
    STextRange& operator|=(const STextRange& Other)
    {
        if (Other._Begin < _Begin)
        {
            _Begin = Other._Begin;
        }

        if (Other._End > _End)
        {
            _End = Other._End;
        }

        return *this;
    }

private:

    // Beginning position (zero-based, inclusive)
    STextPosition _Begin;

    // End position (zero-based, exclusive)
    STextPosition _End;

};  // STextRange

}