// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "Templates/SharedPointerFwd.h"

namespace UE::FileSandboxCore { struct FGatheredFileChanges; }

namespace UE::SandboxedEditing
{
struct FFileStateItem;

/** @return An item array transformed from FGatheredFileChanges  */
TArray<TSharedPtr<FFileStateItem>> MakeItemsFromFileChanges(const FileSandboxCore::FGatheredFileChanges& InChanges);
}

