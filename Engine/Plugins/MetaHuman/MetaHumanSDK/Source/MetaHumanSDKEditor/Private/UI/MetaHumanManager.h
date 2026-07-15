// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/UniquePtr.h"

struct FAssetData;

namespace UE::MetaHuman
{
class FMetaHumanManagerImpl;

/**
 * Class that handles the display of the MetaHuman Manager UI for packaging MetaHuman Assets
 */
class FMetaHumanManager
{
public:
	/**
	 * Initializes the manager and registers the UI with the editor
	 */
	static void Initialize();
	static void Shutdown();
	static void CreateWindow();

	/**
	 * Opens the MetaHuman Manager window and attempts to select the supplied asset in the
	 * navigation pane. If the asset cannot be matched to an item in the navigation (e.g. the
	 * asset isn't packageable, or the navigation hasn't surfaced it for any reason) the window
	 * still opens — selection just defaults to the first item as usual.
	 */
	static void OpenAndSelectAsset(const FAssetData& Asset);

private:
	FMetaHumanManager() = default;
	static TUniquePtr<FMetaHumanManagerImpl> Instance;
};
} // namespace UE::MetaHuman
