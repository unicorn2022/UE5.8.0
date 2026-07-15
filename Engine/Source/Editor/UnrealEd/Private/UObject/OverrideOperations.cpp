// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/OverrideOperations.h"

#include "HAL/IConsoleManager.h"
#include "Logging/LogCategory.h"
#include "ScopedTransaction.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogOverrideOps, Display, All);

#define LOCTEXT_NAMESPACE "OverrideOperations"

namespace UE::OverrideOperations
{
	
static FAutoConsoleCommand CommandResetObject(
	TEXT("Obj.Reset"),
	TEXT("Resets the object and subobjects to its defaults. Args: Path"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.IsEmpty())
			{
				UE_LOGF(LogOverrideOps, Display, "Missing object path argument.");
				return;
			}
			
			const FString& ObjectPath = Args[0];
			UObject* Object = FindFirstObject<UObject>(ObjectPath);
			if (!IsValid(Object))
			{
				UE_LOGF(LogOverrideOps, Display, "Unable to find valid object with path '%ls'.", *ObjectPath);
				return;
			}

			FString Name = Object->GetName();
			
			FScopedTransaction ScopedTransaction(LOCTEXT("ResetObject", "Reset Object"));
			if (!ResetObjectTree(Object))
			{
				UE_LOGF(LogOverrideOps, Warning, "Failed resetting object %ls.", *Name);
				return;
			}
			UE_LOGF(LogOverrideOps, Display, "Resetted object %ls.", *Name);
		}),
	ECVF_Default
	);
	
namespace Private
{
	
/** Helper that retrieves all subobjects of Object that are referenced from within its object tree. */
template<typename PredicateType>
static void GetReferencedSubObjects(TSet<TNotNull<UObject*>>& OutObjects, TNotNull<UObject*> Object, bool bIncludeNested, PredicateType&& Predicate)
{
	OutObjects.Reset();
	
	UObject* Root = Object;
	do
	{
		UObject* Archetype = Root->GetArchetype();
		if (Archetype->HasAnyFlags(RF_ClassDefaultObject))
		{
			break;
		}
		
		Root = Root->GetOuter();
	} while (Root);
	
	if (!Root)
	{
		ensureMsgf(Root, TEXT("Expected object to have a root."));
		return;
	}
	
	Root->GetClass()->Visit(Root, [&OutObjects, Object, bIncludeNested, &Predicate](const FPropertyVisitorContext& Context)
		{
			check(Context.Path.Num() > 0);
		
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Context.Path.Top().Property))
			{
				check(Context.Data.PropertyData);
			
				UObject* SubObject = ObjectProperty->GetObjectPropertyValue(Context.Data.PropertyData);
				if (!SubObject
					|| !(bIncludeNested ? SubObject->IsIn(Object) : SubObject->GetOuter() == Object)
					|| OutObjects.Contains(SubObject)
					|| !Predicate(SubObject))
				{
					return EPropertyVisitorControlFlow::StepOver;
				}
				
				OutObjects.Add(SubObject);
			}
			return EPropertyVisitorControlFlow::StepInto;
		});
}

/** Retrieves the object and all its descendants that are relevant and referenced from within the object's tree. */
static bool GetObjectTree(TSet<TNotNull<UObject*>>& OutObjects, TNotNull<UObject*> Object)
{
	auto IsRelevantObject = [](TNotNull<const UObject*> SubObject)
		{
			return IsValidChecked(SubObject) && !SubObject->HasAnyFlags(RF_MirroredGarbage | RF_Transient);
		};
		
	if (!IsRelevantObject(Object))
	{
		return false;
	}
	
	constexpr bool bIncludeNested = true;
	GetReferencedSubObjects(OutObjects, Object, bIncludeNested, IsRelevantObject);
	OutObjects.Add(Object);
	return true;
}

static void PrepopulateInstancingGraphRecursive(FObjectInstancingGraph& InstancingGraph, const TSet<TNotNull<UObject*>>& ObjectTree,
	TNotNull<UObject*> Object)
{
	ForEachObjectWithOuter(Object, [&InstancingGraph, &ObjectTree](UObject* SubObject)
		{
			if (ObjectTree.Contains(SubObject))
			{
				// If the subobject is not inherited, it might be removed by the reset, so we skip it.
				TNotNull<UObject*> SubObjectArchetype = SubObject->GetArchetype();
				if (!SubObjectArchetype->HasAnyFlags(RF_ClassDefaultObject))
				{
					InstancingGraph.PrepopulateWithInstance(SubObject, SubObjectArchetype);
					PrepopulateInstancingGraphRecursive(InstancingGraph, ObjectTree, SubObject);
				}
			}
		}, EGetObjectsFlags::None, RF_MirroredGarbage | RF_Transient);
}
	
/** Helper function that fills the instancing graph from the root and object tree with relevant objects. */
static void PrepopulateInstancingGraph(FObjectInstancingGraph& InstancingGraph, TNotNull<UObject*> Root, const TSet<TNotNull<UObject*>>& ObjectTree)
{
	PrepopulateInstancingGraphRecursive(InstancingGraph, ObjectTree, Root);
	InstancingGraph.SetDestinationRoot(Root);
}	
	
static void ResetObjectInternal(TNotNull<UObject*> Object, FObjectInstancingGraph& InstancingGraph,
	const TFunction<bool(TNotNull<const FProperty*>)>& PropertyPredicate)
{
	TNotNull<UClass*> ObjectClass = Object->GetClass();
	
	TArray<TNotNull<FProperty*>> PropertiesToReset;
	for (TFieldIterator<FProperty> PropIt(ObjectClass); PropIt; ++PropIt)
	{
		TNotNull<FProperty*> Property = *PropIt;
		if (!PropertyPredicate || PropertyPredicate(Property))
		{
			PropertiesToReset.Add(Property);
		}
	}
	
	if (PropertiesToReset.IsEmpty())
	{
		return;
	}
	
	for (TNotNull<FProperty*> Property : PropertiesToReset)
	{
		FEditPropertyChain Chain;
		Chain.AddTail(Property);
		Object->PreEditChange(Chain);
	}
	
	TNotNull<UObject*> Archetype = Object->GetArchetype();
	TNotNull<UClass*> ArchetypeClass = Archetype->GetClass();
	const bool bUsesDynamicInstancing = ObjectClass->ShouldUseDynamicSubobjectInstancing();
	for (TNotNull<FProperty*> Property : PropertiesToReset)
	{
		// Reset the value to its default.
		Property->CopyCompleteValue_InContainer(Object, Archetype);

		// Assign or instance subobject references.
		if (bUsesDynamicInstancing || Property->ContainsInstancedObjectProperty() || Property->HasAnyPropertyFlags(CPF_AllowSelfReference))
		{
			void* Data = Property->ContainerPtrToValuePtr<uint8>(Object);
			const void* DefaultData = Property->ContainerPtrToValuePtrForDefaults<uint8>(ArchetypeClass, Archetype);
			Property->InstanceSubobjects(Data, DefaultData, Object, &InstancingGraph);
		}
	}
	
	for (TNotNull<FProperty*> Property : PropertiesToReset)
	{
		FEditPropertyChain Chain;
		Chain.AddTail(Property);
		FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ResetToDefault);
		FPropertyChangedChainEvent PropertyChangedChainEvent(Chain, PropertyChangedEvent);
		Object->PostEditChangeChainProperty(PropertyChangedChainEvent);
	}
}
	
static void ResetObjectTreeRecursive(TNotNull<UObject*> Object, FObjectInstancingGraph& InstancingGraph, const TSet<TNotNull<UObject*>>& ObjectTree,
	const TFunction<bool(TNotNull<const UObject*>)>& ObjectPredicate,
	const TFunction<bool(TNotNull<const UObject*>, TNotNull<const FProperty*>)>& PropertyPredicate)
{
	ResetObjectInternal(Object, InstancingGraph, [Object, &PropertyPredicate](TNotNull<const FProperty*> Property)
		{
			return PropertyPredicate(Object, Property);
		});
	
	// Only continue resetting subobjects that already existed before this reset AND are inherited.
	TArray<TNotNull<UObject*>> SubObjects;
	ForEachObjectWithOuter(Object, [&ObjectTree, &ObjectPredicate, &SubObjects](UObject* SubObject)
		{
			if (ObjectTree.Contains(SubObject) && !SubObject->GetArchetype()->HasAnyFlags(RF_ClassDefaultObject) &&
				(!ObjectPredicate || ObjectPredicate(SubObject)))
			{
				SubObjects.Add(SubObject);
			}
		}, EGetObjectsFlags::None);
	
	for (TNotNull<UObject*> SubObject : SubObjects)
	{
		ResetObjectTreeRecursive(SubObject, InstancingGraph, ObjectTree, ObjectPredicate, PropertyPredicate);
	}
}
	
}

bool ResetObject(TNotNull<UObject*> Object, const TFunction<bool(TNotNull<const FProperty*>)>& PropertyPredicate)
{
	// Gather the object and all its descendants that are relevant and referenced from within the object's tree.
	TSet<TNotNull<UObject*>> ObjectTree;
	Private::GetObjectTree(ObjectTree, Object);
	if (ObjectTree.IsEmpty())
	{
		return false;
	}
	
	// Modify all objects, in case they are removed.
	for (TNotNull<UObject*> ObjectToModify : ObjectTree)
	{
		ObjectToModify->Modify();
	}
	
	FObjectInstancingGraph InstancingGraph;
	Private::PrepopulateInstancingGraph(InstancingGraph, Object, ObjectTree);
	
	Private::ResetObjectInternal(Object, InstancingGraph, PropertyPredicate);
	return true;
}

bool ResetObjectTree(TNotNull<UObject*> Object, const TFunction<bool(TNotNull<const UObject*>)>& ObjectPredicate,
	const TFunction<bool(TNotNull<const UObject*>, TNotNull<const FProperty*>)>& PropertyPredicate)
{
	// Gather the object and all its descendants that are relevant and referenced from within the object's tree.
	TSet<TNotNull<UObject*>> ObjectTree;
	Private::GetObjectTree(ObjectTree, Object);
	if (ObjectTree.IsEmpty())
	{
		return false;
	}
	
	// Modify all objects, in case they are removed.
	for (TNotNull<UObject*> ObjectToModify : ObjectTree)
	{
		ObjectToModify->Modify();
	}
	
	FObjectInstancingGraph InstancingGraph;
	Private::PrepopulateInstancingGraph(InstancingGraph, Object, ObjectTree);
	
	Private::ResetObjectTreeRecursive(Object, InstancingGraph, ObjectTree, ObjectPredicate, PropertyPredicate);
	return true;
}
	
}

#undef LOCTEXT_NAMESPACE
