// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/EnumRange.h"

#include "MetaHumanCharacterMakeup.generated.h"

USTRUCT(BlueprintType)
struct FMetaHumanCharacterFoundationMakeupProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation")
	bool bApplyFoundation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation")
	int32 PresetIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation", meta = (HideAlphaChannel, EditCondition = "bApplyFoundation"))
	FLinearColor Color = FLinearColor{ ForceInit };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "bApplyFoundation"))
	float Intensity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "bApplyFoundation"))
	float Roughness = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "bApplyFoundation"))
	float Concealer = 0.57f;

	bool operator==(const FMetaHumanCharacterFoundationMakeupProperties& InOther) const
	{
		return bApplyFoundation == InOther.bApplyFoundation &&
			Color == InOther.Color &&
			PresetIndex == InOther.PresetIndex &&
			Intensity == InOther.Intensity &&
			Roughness == InOther.Roughness &&
			Concealer == InOther.Concealer;
	}

	bool operator!=(const FMetaHumanCharacterFoundationMakeupProperties& InOther) const
	{
		return !(*this == InOther);
	}
};

UENUM()
enum class EMetaHumanCharacterEyeMakeupType : uint8
{
	None,
	ThinLiner,
	SoftSmokey,
	FullThinLiner,
	CatEye,
	PandaSmudge,
	DramaticSmudge,
	DoubleMod,
	ClassicBar,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterEyeMakeupType, EMetaHumanCharacterEyeMakeupType::Count);

USTRUCT(BlueprintType)
struct FMetaHumanCharacterEyeMakeupProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eyes")
	EMetaHumanCharacterEyeMakeupType Type = EMetaHumanCharacterEyeMakeupType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eyes", meta = (HideAlphaChannel, EditCondition = "Type != EMetaHumanCharacterEyeMakeupType::None"))
	FLinearColor PrimaryColor = FLinearColor{ 0.086f, 0.013f, 0.004f, 1.0f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eyes", meta = (HideAlphaChannel, EditCondition = "Type != EMetaHumanCharacterEyeMakeupType::None"))
	FLinearColor SecondaryColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eyes", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "Type != EMetaHumanCharacterEyeMakeupType::None"))
	float Roughness = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eyes", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "Type != EMetaHumanCharacterEyeMakeupType::None"))
	float Opacity = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eyes", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "Type != EMetaHumanCharacterEyeMakeupType::None"))
	float Metalness = 0.0f;

	bool operator==(const FMetaHumanCharacterEyeMakeupProperties& InOther) const
	{
		return Type == InOther.Type &&
			PrimaryColor == InOther.PrimaryColor &&
			SecondaryColor == InOther.SecondaryColor &&
			Roughness == InOther.Roughness &&
			Opacity == InOther.Opacity && 
			Metalness == InOther.Metalness;
	}

	bool operator!=(const FMetaHumanCharacterEyeMakeupProperties& InOther) const
	{
		return !(*this == InOther);
	}
};

UENUM()
enum class EMetaHumanCharacterBlushMakeupType : uint8
{
	None,
	Angled,
	Apple,
	LowSweep,
	HighCurve,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterBlushMakeupType, EMetaHumanCharacterBlushMakeupType::Count);

USTRUCT(BlueprintType)
struct FMetaHumanCharacterBlushMakeupProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blush")
	EMetaHumanCharacterBlushMakeupType Type = EMetaHumanCharacterBlushMakeupType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blush", meta = (HideAlphaChannel, EditCondition = "Type != EMetaHumanCharacterBlushMakeupType::None"))
	FLinearColor Color = FLinearColor{ 0.224f, 0.011f, 0.02f, 1.0f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blush", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "Type != EMetaHumanCharacterBlushMakeupType::None"))
	float Intensity = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blush", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "Type != EMetaHumanCharacterBlushMakeupType::None"))
	float Roughness = 0.6f;

	bool operator==(const FMetaHumanCharacterBlushMakeupProperties& InOther) const
	{
		return Type == InOther.Type &&
			Color == InOther.Color &&
			Intensity == InOther.Intensity &&
			Roughness == InOther.Roughness;
	}

	bool operator!=(const FMetaHumanCharacterBlushMakeupProperties& InOther) const
	{
		return !(*this == InOther);
	}
};

UENUM()
enum class EMetaHumanCharacterLipsMakeupType : uint8
{
	None,
	Natural,
	Hollywood,
	Cupid,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterLipsMakeupType, EMetaHumanCharacterLipsMakeupType::Count)

USTRUCT(BlueprintType)
struct FMetaHumanCharacterLipsMakeupProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lips")
	EMetaHumanCharacterLipsMakeupType Type = EMetaHumanCharacterLipsMakeupType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lips", meta = (HideAlphaChannel, EditCondition = "Type != EMetaHumanCharacterLipsMakeupType::None"))
	FLinearColor Color = FLinearColor{ 0.22f, 0.01f, 0.008f, 1.0f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lips", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "Type != EMetaHumanCharacterLipsMakeupType::None"))
	float Roughness = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lips", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "Type != EMetaHumanCharacterLipsMakeupType::None"))
	float Opacity = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blush", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1", EditCondition = "Type != EMetaHumanCharacterLipsMakeupType::None"))
	float Metalness = 1.0f;

	bool operator==(const FMetaHumanCharacterLipsMakeupProperties& InOther) const
	{
		return Type == InOther.Type &&
			Color == InOther.Color &&
			Roughness == InOther.Roughness &&
			Opacity == InOther.Opacity &&
			Metalness == InOther.Metalness;
	}

	bool operator!=(const FMetaHumanCharacterLipsMakeupProperties& InOther) const
	{
		return !(*this == InOther);
	}
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterMakeupSettings
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterFoundationMakeupProperties Foundation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eyes", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterEyeMakeupProperties Eyes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blush", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterBlushMakeupProperties Blush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lips", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterLipsMakeupProperties Lips;

	bool operator==(const FMetaHumanCharacterMakeupSettings& InOther) const
	{
		return Foundation == InOther.Foundation &&
			Eyes == InOther.Eyes &&
			Blush == InOther.Blush &&
			Lips == InOther.Lips ;
	}

	bool operator!=(const FMetaHumanCharacterMakeupSettings& InOther) const
	{
		return !(*this == InOther);
	}
};