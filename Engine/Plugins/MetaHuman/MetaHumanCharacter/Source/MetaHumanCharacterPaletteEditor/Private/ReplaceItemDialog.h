// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"

class UMetaHumanInstance;

namespace UE::MetaHuman
{

/**
 * Opens a modal dialog that allows the user to redirect one or more item
 * selections in a group of MetaHuman Instances to another item,
 * or to remove them entirely.
 *
 * This is useful when an item is about to be removed from a Collection and
 * any Instances that reference it need to be updated to point at a
 * replacement item first.
 */
void OpenReplaceItemDialog(const TArray<UMetaHumanInstance*>& InInstances);

} // namespace UE::MetaHuman
