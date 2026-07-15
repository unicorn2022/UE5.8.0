// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectBlueprintFunctionLibrary.h"
#include "Engine/Engine.h"
#include "SmartObjectSubsystem.h"
#include "BlackboardKeyType_SOClaimHandle.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BTFunctionLibrary.h"
#include "Types/TargetingSystemTypes.h"
#include "Annotations/SmartObjectSlotEntranceAnnotation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SmartObjectBlueprintFunctionLibrary)

//----------------------------------------------------------------------//
// USmartObjectBlueprintFunctionLibrary 
//----------------------------------------------------------------------//
FSmartObjectClaimHandle USmartObjectBlueprintFunctionLibrary::GetValueAsSOClaimHandle(UBlackboardComponent* BlackboardComponent, const FName& KeyName)
{
	if (BlackboardComponent == nullptr)
	{
		return {};
	}
	return BlackboardComponent->GetValue<UBlackboardKeyType_SOClaimHandle>(KeyName);
}

void USmartObjectBlueprintFunctionLibrary::SetValueAsSOClaimHandle(UBlackboardComponent* BlackboardComponent, const FName& KeyName, const FSmartObjectClaimHandle Value)
{
	if (BlackboardComponent == nullptr)
	{
		return;
	}
	const FBlackboard::FKey KeyID = BlackboardComponent->GetKeyID(KeyName);
	BlackboardComponent->SetValue<UBlackboardKeyType_SOClaimHandle>(KeyID, Value);
}

FSmartObjectClaimHandle USmartObjectBlueprintFunctionLibrary::SmartObjectClaimHandle_Invalid()
{
	return FSmartObjectClaimHandle::InvalidHandle;
}

bool USmartObjectBlueprintFunctionLibrary::AddOrRemoveSmartObject(AActor* SmartObjectActor, const bool bAdd)
{
	return AddOrRemoveMultipleSmartObjects({SmartObjectActor}, bAdd);
}

bool USmartObjectBlueprintFunctionLibrary::AddSmartObject(AActor* SmartObjectActor)
{
	return AddOrRemoveMultipleSmartObjects({SmartObjectActor}, /*bAdd*/true);
}

bool USmartObjectBlueprintFunctionLibrary::AddMultipleSmartObjects(const TArray<AActor*>& SmartObjectActors)
{
	return AddOrRemoveMultipleSmartObjects(SmartObjectActors, /*bAdd*/true);
}

bool USmartObjectBlueprintFunctionLibrary::RemoveSmartObject(AActor* SmartObjectActor)
{
	return AddOrRemoveMultipleSmartObjects({SmartObjectActor}, /*bAdd*/false);
}

bool USmartObjectBlueprintFunctionLibrary::RemoveMultipleSmartObjects(const TArray<AActor*>& SmartObjectActors)
{
	return AddOrRemoveMultipleSmartObjects(SmartObjectActors, /*bAdd*/false);
}

bool USmartObjectBlueprintFunctionLibrary::AddOrRemoveMultipleSmartObjects(const TArray<AActor*>& SmartObjectActors, const bool bAdd)
{
	bool bSuccess = true;
	if (SmartObjectActors.IsEmpty())
	{
		return bSuccess;
	}

	USmartObjectSubsystem* Subsystem = nullptr;
	for (const AActor* SmartObjectActor : SmartObjectActors)
	{
		if (SmartObjectActor == nullptr)
		{
			UE_LOGF(LogSmartObject, Warning, "Null actor found and skipped")
			bSuccess = false;
			continue;
		}

		if (Subsystem == nullptr)
		{
			Subsystem = USmartObjectSubsystem::GetCurrent(SmartObjectActor->GetWorld());
			if (Subsystem == nullptr)
			{
				UE_LOGF(LogSmartObject, Warning, "Unable to find SmartObjectSubsystem for the provided actors.")
				return false;
			}
		}

		bSuccess = bAdd ? Subsystem->RegisterSmartObjectActor(*SmartObjectActor) : Subsystem->RemoveSmartObjectActor(*SmartObjectActor) && bSuccess;
	}

	return bSuccess;
}

bool USmartObjectBlueprintFunctionLibrary::SetSmartObjectEnabled(AActor* SmartObjectActor, const bool bEnabled)
{
	return SetMultipleSmartObjectsEnabled({SmartObjectActor}, bEnabled);
}

FSmartObjectClaimHandle USmartObjectBlueprintFunctionLibrary::MarkSmartObjectSlotAsClaimed(
	UObject* WorldContextObject,
	const FSmartObjectSlotHandle SlotHandle,
	const AActor* UserActor,
	ESmartObjectClaimPriority ClaimPriority)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(World))
	{
		return Subsystem->MarkSlotAsClaimed(SlotHandle, ClaimPriority, FConstStructView::Make(FSmartObjectActorUserData(UserActor)));
	}

	return FSmartObjectClaimHandle::InvalidHandle;
}

const USmartObjectBehaviorDefinition* USmartObjectBlueprintFunctionLibrary::MarkSmartObjectSlotAsOccupied(
	UObject* WorldContextObject,
	const FSmartObjectClaimHandle ClaimHandle,
	const TSubclassOf<USmartObjectBehaviorDefinition> DefinitionClass)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(World))
	{
		return Subsystem->MarkSlotAsOccupied(ClaimHandle, DefinitionClass);
	}

	return nullptr;
}

bool USmartObjectBlueprintFunctionLibrary::MarkSmartObjectSlotAsFree(
	UObject* WorldContextObject,
	const FSmartObjectClaimHandle ClaimHandle)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(World))
	{
		return Subsystem->MarkSlotAsFree(ClaimHandle);	
	}

	return false;
}

bool USmartObjectBlueprintFunctionLibrary::FindSmartObjectsInComponent(const FSmartObjectRequestFilter& Filter, USmartObjectComponent* SmartObjectComponent, TArray<FSmartObjectRequestResult>& OutResults, const AActor* UserActor)
{
	if (SmartObjectComponent)
	{
		if (const USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(SmartObjectComponent->GetWorld()))
		{
			return Subsystem->FindSmartObjectsInList(Filter, { SmartObjectComponent->GetOwner() }, OutResults, FConstStructView::Make(FSmartObjectActorUserData(UserActor)));	
		}
	}

	return false;
}

bool USmartObjectBlueprintFunctionLibrary::FindSmartObjectsInActor(const FSmartObjectRequestFilter& Filter, AActor* SearchActor, TArray<FSmartObjectRequestResult>& OutResults, const AActor* UserActor)
{
	if (SearchActor)
	{
		if (const USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(SearchActor->GetWorld()))
    	{
    		return Subsystem->FindSmartObjectsInList(Filter, { SearchActor }, OutResults, FConstStructView::Make(FSmartObjectActorUserData(UserActor)));	
    	}
	}
	return false;
}

bool USmartObjectBlueprintFunctionLibrary::FindSmartObjectsInTargetingRequest(UObject* WorldContextObject, const FSmartObjectRequestFilter& Filter, const FTargetingRequestHandle TargetingHandle, TArray<FSmartObjectRequestResult>& OutResults, const AActor* UserActor /*= nullptr*/)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (const USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(World))
	{
		return Subsystem->FindSmartObjectsInTargetingRequest(Filter, TargetingHandle, OutResults, FConstStructView::Make(FSmartObjectActorUserData(UserActor)));
	}
	return false;
}

bool USmartObjectBlueprintFunctionLibrary::FindSmartObjectsInList(UObject* WorldContextObject, const FSmartObjectRequestFilter& Filter, const TArray<AActor*>& ActorList, TArray<FSmartObjectRequestResult>& OutResults, const AActor* UserActor /*= nullptr*/)
{
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (const USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(World))
	{
		return Subsystem->FindSmartObjectsInList(Filter, ActorList, OutResults, FConstStructView::Make(FSmartObjectActorUserData(UserActor)));
	}
	return false;
}

FString USmartObjectBlueprintFunctionLibrary::Conv_SmartObjectClaimHandleToString(const FSmartObjectClaimHandle& Result)
{
	return LexToString(Result);
}

FString USmartObjectBlueprintFunctionLibrary::Conv_SmartObjectRequestResultToString(const FSmartObjectRequestResult& Result)
{
	return LexToString(Result);
}

FString USmartObjectBlueprintFunctionLibrary::Conv_SmartObjectDefinitionToString(const USmartObjectDefinition* Definition)
{
	if (Definition)
	{
		return LexToString(*Definition);	
	}

	UE_LOGF(LogSmartObject, Error, "Attempted to convert null SmartObjectDefinition to string!");
	
	static const FString InvalidDefinitionString = TEXT("INVALID");
	return InvalidDefinitionString;
}

FString USmartObjectBlueprintFunctionLibrary::Conv_SmartObjectHandleToString(const FSmartObjectHandle& Handle)
{
	return LexToString(Handle);
}

bool USmartObjectBlueprintFunctionLibrary::NotEqual_SmartObjectHandleSmartObjectHandle(const FSmartObjectHandle& A, const FSmartObjectHandle& B)
{
	return A != B;
}

bool USmartObjectBlueprintFunctionLibrary::Equal_SmartObjectHandleSmartObjectHandle(const FSmartObjectHandle& A, const FSmartObjectHandle& B)
{
	return A == B;
}

bool USmartObjectBlueprintFunctionLibrary::IsValidSmartObjectHandle(const FSmartObjectHandle& Handle)
{
	return Handle.IsValid();
}

FString USmartObjectBlueprintFunctionLibrary::Conv_SmartObjectSlotHandleToString(const FSmartObjectSlotHandle& Handle)
{
	return LexToString(Handle);
}

bool USmartObjectBlueprintFunctionLibrary::Equal_SmartObjectSlotHandleSmartObjectSlotHandle(const FSmartObjectSlotHandle& A, const FSmartObjectSlotHandle& B)
{
	return A == B;
}

bool USmartObjectBlueprintFunctionLibrary::NotEqual_SmartObjectSlotHandleSmartObjectSlotHandle(const FSmartObjectSlotHandle& A, const FSmartObjectSlotHandle& B)
{
	return A != B;
}

bool USmartObjectBlueprintFunctionLibrary::IsValidSmartObjectSlotHandle(const FSmartObjectSlotHandle& Handle)
{
	return Handle.IsValid();
}

bool USmartObjectBlueprintFunctionLibrary::SetMultipleSmartObjectsEnabled(const TArray<AActor*>& SmartObjectActors, const bool bEnabled)
{
	bool bSuccess = true;
	if (SmartObjectActors.IsEmpty())
	{
		return bSuccess;
	}

	USmartObjectSubsystem* Subsystem = nullptr;
	for (const AActor* SmartObjectActor : SmartObjectActors)
	{
		if (SmartObjectActor == nullptr)
		{
			UE_LOGF(LogSmartObject, Warning, "Null actor found and skipped")
			bSuccess = false;
			continue;
		}

		if (Subsystem == nullptr)
		{
			Subsystem = USmartObjectSubsystem::GetCurrent(SmartObjectActor->GetWorld());
			if (Subsystem == nullptr)
			{
				UE_LOGF(LogSmartObject, Warning, "Unable to find SmartObjectSubsystem for the provided actors.")
				return false;
			}
		}

		bSuccess = Subsystem->SetSmartObjectActorEnabled(*SmartObjectActor, bEnabled) && bSuccess;
	}

	return bSuccess;
}

void USmartObjectBlueprintFunctionLibrary::SetBlackboardValueAsSOClaimHandle(UBTNode* NodeOwner, const FBlackboardKeySelector& Key, const FSmartObjectClaimHandle& Value)
{
	if (UBlackboardComponent* BlackboardComp = UBTFunctionLibrary::GetOwnersBlackboard(NodeOwner))
	{
		BlackboardComp->SetValue<UBlackboardKeyType_SOClaimHandle>(Key.SelectedKeyName, Value);
	}
}

FSmartObjectClaimHandle USmartObjectBlueprintFunctionLibrary::GetBlackboardValueAsSOClaimHandle(UBTNode* NodeOwner, const FBlackboardKeySelector& Key)
{
	UBlackboardComponent* BlackboardComp = UBTFunctionLibrary::GetOwnersBlackboard(NodeOwner);
	return BlackboardComp ? BlackboardComp->GetValue<UBlackboardKeyType_SOClaimHandle>(Key.SelectedKeyName) : FSmartObjectClaimHandle::InvalidHandle;
}

int32 USmartObjectBlueprintFunctionLibrary::GetNumSlotEntrances(const USmartObjectDefinition* Definition, const int32 SlotIndex)
{
	if (Definition == nullptr)
	{
		// Null definition - get out.
		UE_LOGF(LogSmartObject, Warning, "GetNumSlotEntrances: Definition is null.");
		return 0;
	}

	if (!Definition->IsValidSlotIndex(SlotIndex))
	{
		// Invalid slot index - get out.
		UE_LOGF(LogSmartObject, Warning, "GetNumSlotEntrances: SlotIndex %d is invalid for definition '%ls'.", SlotIndex, *Definition->GetName());
		return 0;
	}

	// If we made it here, we have a valid definition and slot index.
	const FSmartObjectSlotDefinition& SlotDefinition = Definition->GetSlot(SlotIndex);
	const UScriptStruct* EntranceStaticStruct = FSmartObjectSlotEntranceAnnotation::StaticStruct();
	int32 Count = 0;

	for (const FSmartObjectDefinitionDataProxy& DataProxy : SlotDefinition.DefinitionData)
	{
		const UScriptStruct* DataStruct = DataProxy.Data.GetScriptStruct();
		if (DataStruct != nullptr && DataStruct->IsChildOf(EntranceStaticStruct))
		{
			++Count;
		}
	}

	return Count;
}

bool USmartObjectBlueprintFunctionLibrary::GetSlotEntranceOffsetAndRotation(const USmartObjectDefinition* Definition, const int32 SlotIndex, const int32 EntranceIndex, FVector& OutOffset, FRotator& OutRotation)
{
	OutOffset = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;

	if (Definition == nullptr)
	{
		// Null definition - get out.
		UE_LOGF(LogSmartObject, Warning, "GetSlotEntranceOffsetAndRotation: Definition is null.");
		return false;
	}

	if (!Definition->IsValidSlotIndex(SlotIndex))
	{
		// Invalid slot index - get out.
		UE_LOGF(LogSmartObject, Warning, "GetSlotEntranceOffsetAndRotation: SlotIndex %d is invalid for definition '%ls'.", SlotIndex, *Definition->GetName());
		return false;
	}

	// If we made it here, we have a valid definition and slot index.
	const FSmartObjectSlotDefinition& SlotDefinition = Definition->GetSlot(SlotIndex);
	const UScriptStruct* EntranceStaticStruct = FSmartObjectSlotEntranceAnnotation::StaticStruct();
	int32 CurrentEntranceIndex = 0;

	for (const FSmartObjectDefinitionDataProxy& DataProxy : SlotDefinition.DefinitionData)
	{
		const UScriptStruct* DataStruct = DataProxy.Data.GetScriptStruct();
		if (DataStruct == nullptr || !DataStruct->IsChildOf(EntranceStaticStruct))
		{
			// Not an entrance annotation - skip.
			continue;
		}

		if (CurrentEntranceIndex == EntranceIndex)
		{
			const FSmartObjectSlotEntranceAnnotation& EntranceAnnotation = DataProxy.Data.Get<FSmartObjectSlotEntranceAnnotation>();
			OutOffset = FVector(EntranceAnnotation.Offset);
			OutRotation = FRotator(EntranceAnnotation.Rotation);
			return true;
		}

		++CurrentEntranceIndex;
	}

	// Entrance not found at the requested index - get out.
	UE_LOGF(LogSmartObject, Warning, "GetSlotEntranceOffsetAndRotation: EntranceIndex %d not found for slot %d in definition '%ls'. Found %d entrance(s).", EntranceIndex, SlotIndex, *Definition->GetName(), CurrentEntranceIndex);
	return false;
}

bool USmartObjectBlueprintFunctionLibrary::GetSlotEntranceTransform(const USmartObjectDefinition* Definition, const int32 SlotIndex, const int32 EntranceIndex, FTransform& OutTransform)
{
	FVector Offset;
	FRotator Rotation;
	if (!GetSlotEntranceOffsetAndRotation(Definition, SlotIndex, EntranceIndex, Offset, Rotation))
	{
		// Entrance not found - get out. GetSlotEntranceOffsetAndRotation already logged the reason.
		OutTransform = FTransform::Identity;
		return false;
	}

	// If we made it here, we have valid offset and rotation.
	OutTransform = FTransform(Rotation, Offset);
	return true;
}

bool USmartObjectBlueprintFunctionLibrary::GetSlotDefinitionDataByType(const USmartObjectDefinition* Definition, int32 SlotIndex, TArray<int32>& OutDefinitionData)
{
	// Stub - the real work happens in the CustomThunk below.
	checkNoEntry();
	return false;
}

DEFINE_FUNCTION(USmartObjectBlueprintFunctionLibrary::execGetSlotDefinitionDataByType)
{
	// Step 1: Read the Definition parameter.
	P_GET_OBJECT(USmartObjectDefinition, Definition);

	// Step 2: Read the SlotIndex parameter.
	P_GET_PROPERTY(FIntProperty, SlotIndex);

	// Step 3: Read the wildcard output array parameter.
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FArrayProperty>(nullptr);
	void* ArrayAddr = Stack.MostRecentPropertyAddress;
	FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Stack.MostRecentProperty);
	if (!ArrayProperty)
	{
		// Failed to resolve the output array property - get out.
		Stack.bArrayContextFailed = true;
		return;
	}

	P_FINISH;

	if (!ArrayAddr)
	{
		// Null array address - get out.
		UE_LOGF(LogSmartObject, Warning, "GetSlotDefinitionDataByType: Output array address is null.");
		*(bool*)RESULT_PARAM = false;
		return;
	}

	// Construct the array helper early so all error paths can empty the output array.
	FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayAddr);
	ArrayHelper.EmptyValues();

	// Step 4: Determine the struct type from the array's inner property.
	const FStructProperty* InnerStructProperty = CastField<FStructProperty>(ArrayProperty->Inner);
	if (InnerStructProperty == nullptr)
	{
		// Output array is not a struct array - get out.
		UE_LOGF(LogSmartObject, Warning, "GetSlotDefinitionDataByType: Output array must be a struct type that inherits from FSmartObjectDefinitionData.");
		*(bool*)RESULT_PARAM = false;
		return;
	}

	const UScriptStruct* RequestedStruct = InnerStructProperty->Struct;
	if (!RequestedStruct->IsChildOf(FSmartObjectDefinitionData::StaticStruct()))
	{
		// Requested type does not inherit from FSmartObjectDefinitionData - get out.
		UE_LOGF(LogSmartObject, Warning, "GetSlotDefinitionDataByType: Struct type '%ls' does not inherit from FSmartObjectDefinitionData.", *RequestedStruct->GetName());
		*(bool*)RESULT_PARAM = false;
		return;
	}

	if (RequestedStruct == FSmartObjectDefinitionData::StaticStruct())
	{
		// Base type has no UPROPERTY fields — matching all entries would yield empty shells via struct slicing. Get out.
		UE_LOGF(LogSmartObject, Warning, "GetSlotDefinitionDataByType: Output type is the base FSmartObjectDefinitionData which has no Blueprint-visible fields. Use a derived type instead.");
		*(bool*)RESULT_PARAM = false;
		return;
	}

	P_NATIVE_BEGIN;

	if (Definition == nullptr)
	{
		// Null definition - get out.
		UE_LOGF(LogSmartObject, Warning, "GetSlotDefinitionDataByType: Definition is null.");
		*(bool*)RESULT_PARAM = false;
	}
	else if (!Definition->IsValidSlotIndex(SlotIndex))
	{
		// Invalid slot index - get out.
		UE_LOGF(LogSmartObject, Warning, "GetSlotDefinitionDataByType: SlotIndex %d is invalid for definition '%ls'.", SlotIndex, *Definition->GetName());
		*(bool*)RESULT_PARAM = false;
	}
	else
	{
		// If we made it here, we have a valid definition, slot, and struct type.
		const FSmartObjectSlotDefinition& SlotDefinition = Definition->GetSlot(SlotIndex);

		for (const FSmartObjectDefinitionDataProxy& DataProxy : SlotDefinition.DefinitionData)
		{
			const UScriptStruct* DataStruct = DataProxy.Data.GetScriptStruct();
			if (DataStruct == nullptr || !DataStruct->IsChildOf(RequestedStruct))
			{
				// Not a matching type - skip.
				continue;
			}

			// AddValue() initializes the element, then we copy the source data over it.
			const int32 NewIndex = ArrayHelper.AddValue();
			RequestedStruct->CopyScriptStruct(ArrayHelper.GetRawPtr(NewIndex), DataProxy.Data.GetMemory());
		}

		*(bool*)RESULT_PARAM = ArrayHelper.Num() > 0;
	}

	P_NATIVE_END;
}
