// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IObjectChooser.h"
#include "Factory/AnimNextFactoryParams.h"
#include "UAF/UAFAssetData.h"
#include "ObjectChooser_UAFGraph.generated.h"

#define UE_API CHOOSER_API

USTRUCT(DisplayName = "UAFGraph", Meta = (ResultType = "Object", Category = "Basic", Tooltip = "A asset that can produce a UAF Graph"))
struct FUAFGraphChooser : public FObjectChooserBase
{
	GENERATED_BODY()

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FUAFGraphChooser() = default;
	~FUAFGraphChooser() = default;
	FUAFGraphChooser(const FUAFGraphChooser&) = default;
	FUAFGraphChooser(FUAFGraphChooser&&) = default;
	FUAFGraphChooser& operator=(const FUAFGraphChooser&) = default;
	FUAFGraphChooser& operator=(FUAFGraphChooser&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

#if WITH_EDITORONLY_DATA
	bool Serialize(FArchive& Ar);
	void PostSerialize(const FArchive& Ar);
#endif
	
	// FObjectChooserBase interface
	virtual UObject* ChooseObject(FChooserEvaluationContext& Context) const final override;
	virtual EIteratorStatus IterateObjects(FChooserEvaluationContext& Context, FObjectChooserIteratorCallback Callback) const final override;

#if WITH_EDITOR
	virtual UObject* GetReferencedObject() const override;
#endif

	UObject* GetObjectFromAssetData() const;
	
	UPROPERTY(EditAnywhere, Category = "Parameters")
	TInstancedStruct<FUAFGraphFactoryAsset> AssetData;

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.8, "Use AssetData instead")
	UPROPERTY()
	TObjectPtr<UObject> Asset_DEPRECATED;
#endif // WITH_EDITORONLY_DATA
	
	// Factory params for procedural graphs
	UE_DEPRECATED(5.8, "Use AssetData instead")
	UPROPERTY()
	FAnimNextFactoryParams Parameters;
};

template<>
struct TStructOpsTypeTraits<FUAFGraphChooser> : public TStructOpsTypeTraitsBase2<FUAFGraphChooser>
{
#if WITH_EDITORONLY_DATA
	enum
	{
		WithSerializer = true,
		WithPostSerialize = true,
	};
#endif
};

#undef UE_API
