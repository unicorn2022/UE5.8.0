// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Features/Browser/ViewModels/Columns/ISearchableColumnBehavior.h"
#include "Templates/SharedPointer.h"

namespace UE::SandboxedEditing
{
class FSandboxListItem;

/** Operations for columns that display sandboxes in the sandbox list view. */
class ISandboxColumnBehavior : public ISearchableColumnBehavior<TSharedPtr<FSandboxListItem>>
{};
}
