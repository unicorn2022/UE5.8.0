// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SingleSelectionTool.h"
#include "MetaHumanCharacterExternalImportTool.generated.h"

class UMetaHumanCharacter;

/**
 * Abstract base class for externally-contributed MetaHuman import tools.
 *
 * External plugins should:
 *   1. Derive their tool UClass from UMetaHumanCharacterExternalImportTool.
 *   2. Override CanApply() and Apply().
 *   3. Optionally override GetWarningText().
 *   4. Implement IMetaHumanImportToolFeature and register via IModularFeatures.
 *
 * The host framework calls these methods to drive the Apply button state and action.
 *
 * Use GetTargetMetaHumanCharacter() inside Apply() to obtain the MetaHuman Character being edited.
 */
UCLASS(Abstract, Transient)
class METAHUMANCHARACTEREDITOR_API UMetaHumanCharacterExternalImportTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:
	/** Returns true when the Apply button should be enabled. */
	virtual bool CanApply() const
		PURE_VIRTUAL(UMetaHumanCharacterExternalImportTool::CanApply, return false;);

	/** Called when the Apply button is clicked. */
	virtual void Apply()
		PURE_VIRTUAL(UMetaHumanCharacterExternalImportTool::Apply,);

	/** Warning text shown above the content area. Return empty to hide. Defaults to empty. */
	virtual FText GetWarningText() const;

protected:
	/** Returns the MetaHuman Character asset that is currently being edited. */
	UMetaHumanCharacter* GetTargetMetaHumanCharacter() const;
};
