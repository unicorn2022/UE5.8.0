// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UAssetEditor.h"

#include "MetaHumanCharacterPaletteAssetEditor.generated.h"

class UMetaHumanInstance;
class UMetaHumanCollection;

/**
 * An asset editor capable of editing MetaHuman Collection and Instance assets
 */
UCLASS()
class UMetaHumanCharacterPaletteAssetEditor : public UAssetEditor
{
	GENERATED_BODY()

public:

	// Begin UAssetEditor Interface
	virtual void GetObjectsToEdit(TArray<UObject*>& OutObjectsToEdit) override;
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;
	// End UAssetEditor Interface

	UMetaHumanCollection* GetMetaHumanCollection() const
	{
		return Collection;
	}

	UMetaHumanInstance* GetMetaHumanInstance() const
	{
		return Instance;
	}

	bool IsCollectionEditable() const
	{
		return bIsCollectionEditable;
	}

	/** The Collection is the object being edited. Instance will be the Collection's default instance. */
	void SetObjectToEdit(UMetaHumanCollection* InObject);
	/** The Instance is the object being edited. Its Collection will be accessible but not editable. */
	void SetObjectToEdit(UMetaHumanInstance* InObject);

private:
	UPROPERTY()
	TObjectPtr<UMetaHumanCollection> Collection;

	UPROPERTY()
	TObjectPtr<UMetaHumanInstance> Instance;

	bool bIsCollectionEditable;
};
