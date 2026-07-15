// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Text/Symbol.h"

namespace uLang
{

/**
 * Common parent class for objects with a name.
 * Used for identifying, sorting, etc.
 **/
class CNamed
{
public:

    // Common Methods

    CNamed()                                             {}
    explicit CNamed(const CSymbol& Name) : _Name(Name)   {}
    CNamed(const CNamed& Source) : _Name(Source._Name)   {}
    CNamed & operator=(const CNamed & Source)            { _Name = Source._Name; return *this; }

    // Converter Methods

    operator const CSymbol& () const                     { return _Name; }

    // Comparison Methods - used for sorting etc.

    EEquate Compare(const CSymbol& Name) const           { return _Name.Compare(Name); }
    bool operator==(const CSymbol& Name) const           { return _Name == Name; }
    bool operator!=(const CSymbol& Name) const           { return _Name != Name; }
    bool operator<=(const CSymbol& Name) const           { return _Name <= Name; }
    bool operator>=(const CSymbol& Name) const           { return _Name >= Name; }
    bool operator<(const CSymbol& Name) const            { return _Name < Name; }
    bool operator>(const CSymbol& Name) const            { return _Name > Name; }

    // Accessor Methods

    const CSymbol& GetName() const                       { return _Name; }
    SymbolId       GetNameId() const                     { return _Name.GetId(); }

    CUTF8StringView AsNameStringView() const             { return _Name.AsStringView(); }
    const char*     AsNameCString() const                { return _Name.AsCString(); }
    UTF8Char        AsNameFirstByte() const              { return _Name.FirstByte(); }

protected:

    // Data Members

    CSymbol _Name;

};  // CNamed

}