// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFAnimationChooserInitializer.h"
#include "UAFAnimChooser.h"
#include "ChooserPlayerTraitData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UAFAnimationChooserInitializer)

#define LOCTEXT_NAMESPACE "UAFAnimationChooserInitializer"

void FUAFAnimationChooserInitializer::InitializeSignature(UChooserSignature* Chooser) const
{
	Chooser->ContextData.SetNum(2);
	Chooser->ContextData[0].InitializeAs(FUAFSharedVariablesContext::StaticStruct());
	FUAFSharedVariablesContext& UAFVariablesData = Chooser->ContextData[0].GetMutable<FUAFSharedVariablesContext>();
	UAFVariablesData.SharedVariablesAssets = SharedVariablesAssets;
	UAFVariablesData.Direction = EContextObjectDirection::ReadWrite;
	
	Chooser->ContextData[1].InitializeAs(FContextObjectTypeStruct::StaticStruct());
	FContextObjectTypeStruct& StructData = Chooser->ContextData[1].GetMutable<FContextObjectTypeStruct>();
	StructData.Struct = FUAFChooserPlayerSettings::StaticStruct();
	StructData.Direction = EContextObjectDirection::Write;
}

UClass* FUAFAnimationChooserInitializer::OverrideClass(UClass* Class) const
{
	if (Class == UChooserTable::StaticClass())
	{
		return UUAFAnimChooserTable::StaticClass();
	}
	return Class;
}

#undef LOCTEXT_NAMESPACE
