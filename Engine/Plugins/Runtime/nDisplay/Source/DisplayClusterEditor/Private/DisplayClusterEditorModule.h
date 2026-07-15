// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDisplayClusterEditor.h"


/**
 * Display Cluster editor module
 */
class FDisplayClusterEditorModule :
	public IDisplayClusterEditor
{
public:
	FDisplayClusterEditorModule();

public:
	//~ IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

protected:
	void RegisterSettings();
	void UnregisterSettings();

private:
	const FName Container;
	const FName Category;
	const FName Section;
};
