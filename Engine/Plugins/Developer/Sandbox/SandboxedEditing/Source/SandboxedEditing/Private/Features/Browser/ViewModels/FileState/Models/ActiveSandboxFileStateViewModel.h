// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StaticFileStateViewModel.h"

namespace UE::SandboxedEditing
{
class FSandboxSystemModel;

/** Refreshes the file states of the active sandbox.  */
class FActiveSandboxFileStateViewModel : public FStaticFileStateViewModel
{
public:
	
	explicit FActiveSandboxFileStateViewModel(
		const TSharedRef<FSandboxSystemModel>& InModel,
		const TSharedRef<FFilterFileStateViewModel>& InFilterViewModel,
		TMap<FName, TSharedRef<IFileStateColumnBehavior>> InColumns
		);
	~FActiveSandboxFileStateViewModel();
	
private:
	
	/** Used to get the file states from the active sandbox. */
	TSharedPtr<FSandboxSystemModel> Model;
	
	/** Refreshes the items from the active sandbox. */
	void RefreshItems();
	
	void OnSandboxLoaded();
	void OnSandboxLeft();
};
}

