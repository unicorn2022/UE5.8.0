// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataStorage/Scope/EditorDataScopeTypes.h"

namespace UE::Editor::DataStorage::Scope
{

FScopeDataVersion::FScopeDataVersion() = default;

bool FScopeDataVersion::operator==(const FScopeDataVersion& Other) const
{
	return SourceRow == Other.SourceRow && Version == Other.Version;
}

bool FScopeDataVersion::operator!=(const FScopeDataVersion& Other) const
{
	return !(*this == Other);
}

bool FScopeDataVersion::IsValid() const
{
	return SourceRow != InvalidRowHandle;
}

FScopeDataVersion FScopeDataVersion::Make(RowHandle InSourceRow, uint64 InVersion)
{
	FScopeDataVersion Result;
	Result.SourceRow = InSourceRow;
	Result.Version = InVersion;
	return Result;
}

} // namespace UE::Editor::DataStorage::Scope
