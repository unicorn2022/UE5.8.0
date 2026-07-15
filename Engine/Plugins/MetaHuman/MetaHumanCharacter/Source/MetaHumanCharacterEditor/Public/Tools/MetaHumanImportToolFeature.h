// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Features/IModularFeature.h"
#include "Templates/SharedPointer.h"
#include "Templates/SubclassOf.h"
#include "Textures/SlateIcon.h"

class FUICommandInfo;
class UInteractiveToolBuilder;
class SWidget;
class UObject;
class UMetaHumanCharacterExternalImportTool;

/**
 * Modular feature interface implemented by external plugins to contribute an import tool
 * to the MetaHuman Character Editor's Import palette.
 *
 * External plugins should:
 *   1. Subclass IMetaHumanImportToolFeature and implement all pure virtual methods.
 *   2. Declare a static instance of their subclass.
 *   3. In StartupModule(), call:
 *        IModularFeatures::Get().RegisterModularFeature(IMetaHumanImportToolFeature::GetModularFeatureName(), &MyFeature);
 *   4. In ShutdownModule(), call:
 *        IModularFeatures::Get().UnregisterModularFeature(IMetaHumanImportToolFeature::GetModularFeatureName(), &MyFeature);
 */
class METAHUMANCHARACTEREDITOR_API IMetaHumanImportToolFeature : public IModularFeature
{
public:
	/** Feature name key used with IModularFeatures::Get(). */
	static FName GetModularFeatureName()
	{
		static FName ModularFeatureName = FName(TEXT("MetaHumanImportTool"));
		return ModularFeatureName;
	}	

	/** Command for the palette toggle button. Must be registered before the editor mode enters. */
	virtual TSharedPtr<FUICommandInfo> GetCommand() const = 0;

	/**
	 * Creates the tool builder.
	 * Outer is the EdMode (matching internal tool creation via NewObject<Builder>(this)).
	 */
	virtual UInteractiveToolBuilder* CreateBuilder(UObject* Outer) const = 0;

	/**
	 * Creates the scrollable content widget shown in the tool's panel.
	 * Do NOT include an Apply button — it is provided by the host framework.
	 */
	virtual TSharedRef<SWidget> CreateContent(UMetaHumanCharacterExternalImportTool* Tool) const = 0;

	/**
	 * The concrete UClass of the tool.
	 * Used to match the active tool in CreateToolView() via Tool->IsA(GetToolClass()).
	 * Must be a subclass of UMetaHumanCharacterExternalImportTool.
	 */
	virtual TSubclassOf<UMetaHumanCharacterExternalImportTool> GetToolClass() const = 0;

	/**
	 * Optional icon shown on the palette toggle button.
	 * Return a default-constructed FSlateIcon() (IsSet() == false) to show no icon.
	 */
	virtual FSlateIcon GetIcon() const { return FSlateIcon(); }
};
