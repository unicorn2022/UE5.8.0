// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFVariableMenus.h"
#include "CoreMinimal.h"
#include "ChooserPlayerTraitData.h"
#include "DetailCategoryBuilder.h"
#include "IChooserParameterFloat.h"
#include "IObjectChooser.h"
#include "IPropertyAccessEditor.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Text/STextBlock.h"
#include "ObjectChooserWidgetFactories.h"

#include "ChooserParameters.h"
#include "Features/IModularFeatures.h"

#define LOCTEXT_NAMESPACE "UAFVariableMenus"

namespace UE::UAF::ChooserEditor
{

	void CollectSharedVariablesRecursive(TObjectPtr<const UUAFRigVMAsset> SharedVariables, TArray<TObjectPtr<const UUAFRigVMAsset>>& SharedVariablesList)
	{
		if (SharedVariables)
		{
			if (!SharedVariablesList.Contains(SharedVariables))
			{
				SharedVariablesList.Add(SharedVariables);

				for(auto& SharedVariablesRef : SharedVariables->GetReferencedVariableAssets())
				{
					CollectSharedVariablesRecursive(SharedVariablesRef, SharedVariablesList);
				}
			}
		}
	}
	
	template <typename PropertyType>
	void CreateUAFVariableMenus(const FString& TypeFilter, UObject* TransactionObject, const IHasContextClass* ContextClassOwner, FInstancedStruct* Parameter, FMenuBuilder& MenuBuilder)
	{
		if (ContextClassOwner)
		{
			FString AlternateTypeFilter;
			if (TypeFilter.Len() > 0)
			{
				if (TypeFilter[TypeFilter.Len() - 1] == '*')
				{
					FString Trimmed = TypeFilter.TrimChar('*');
					AlternateTypeFilter = "TObjectPtr<" + Trimmed + ">";
				}
			}
				
			TConstArrayView<FInstancedStruct> ContextData;
			if (ContextClassOwner)
			{
				ContextData = ContextClassOwner->GetContextData();
			}


			TArray<TObjectPtr<const UUAFRigVMAsset>> SharedVariablesList;
			
			for(int ContextIndex = 0; ContextIndex < ContextData.Num(); ContextIndex++)
			{
				const FInstancedStruct &ContextEntry = ContextData[ContextIndex];
				if (const FUAFSharedVariablesContext* UAFSharedVariablesContext = ContextEntry.GetPtr<FUAFSharedVariablesContext>())
				{
					for (const TObjectPtr<UUAFSharedVariables>& SharedVariables : UAFSharedVariablesContext->SharedVariablesAssets)
					{
						CollectSharedVariablesRecursive(SharedVariables, SharedVariablesList);
					}
				}
			}
			
			
			for (const TObjectPtr<const UUAFRigVMAsset>& SharedVariables : SharedVariablesList)
			{
				if (SharedVariables)
				{
					const UStruct* PropertyBagStruct = SharedVariables->GetVariableDefaults().GetPropertyBagStruct();
					UStruct* Struct = const_cast<UStruct*>(PropertyBagStruct);

					// Show struct properties.
					MenuBuilder.AddSubMenu(
						SNew(STextBlock).Text(FText::FromName(SharedVariables->GetFName())),
						FNewMenuDelegate::CreateLambda([TypeFilter, AlternateTypeFilter, TransactionObject, Struct, Parameter, ContextClassOwner, SharedVariables](FMenuBuilder& MenuBuilder)
							{
								// Make first chain element representing the index in the context array.
								TArray<TSharedPtr<FBindingChainElement>> BindingChain;
								BindingChain.Emplace(MakeShared<FBindingChainElement>(nullptr, 0));

								FPropertyBindingWidgetArgs Args;
								Args.bAllowPropertyBindings = true;


								Args.bAllowUObjectFunctions = true;
								Args.bAllowOnlyThreadSafeFunctions = true;

								auto CanBindProperty = [TypeFilter, AlternateTypeFilter, TransactionObject](FProperty* Property, TConstArrayView<FBindingChainElement> InBindingChain)
									{
										if (InBindingChain.Num() > 2)
										{
											// for now, we only support binding directly to UAF variables (eg not members of structs)
											// so filter out anything with a deeper binding chain
											return false;
										}

										if (TypeFilter == "" || Property == nullptr)
										{
											return true;
										}
										if (TypeFilter == "struct")
										{
											// special case for struct of any type
											return CastField<FStructProperty>(Property) != nullptr;
										}
										if (TypeFilter == "object")
										{
											// special case for objects references of any type
											return CastField<FObjectPropertyBase>(Property) != nullptr;
										}
										if (TypeFilter == "double")
										{
											// special case for doubles to bind to either floats or doubles
											return Property->GetCPPType() == "float" || Property->GetCPPType() == "double" || Property->GetCPPType() == "int32";
										}
										else if (TypeFilter == "enum")
										{
											// special case for enums, to find properties of type EnumProperty or ByteProperty which have an Enum

											if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property))
											{
												return true;
											}
											else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
											{
												return ByteProperty->Enum != nullptr;
											}
											return false;
										}
										else if (TypeFilter == "bool")
										{
											// special case for bools, because CPPType == "bool" doesn't catch: uint8 bBool : 1

											if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
											{
												return true;
											}
											return false;
										}

										const FString CPPType = Property->GetCPPType();

										return CPPType == TypeFilter || CPPType == AlternateTypeFilter;
									};

								// allow struct bindings to bind context structs directly
								Args.OnCanBindToContextStructWithIndex = FOnCanBindToContextStructWithIndex::CreateLambda([TypeFilter](UStruct* StructType, int32 StructIndex)
									{
										if (StructType)
										{
											if (TypeFilter == "struct" && !StructType->IsChildOf(UObject::StaticClass()))
											{
												// struct bindings can bind any type of struct
												return true;
											}
											else
											{
												const FString CPPName = StructType->GetPrefixCPP() + StructType->GetName();
												return CPPName == TypeFilter;
											}
										}
										return false;
									});

								Args.OnCanBindPropertyWithBindingChain = FOnCanBindPropertyWithBindingChain::CreateLambda(CanBindProperty);

								Args.OnCanBindFunction = FOnCanBindFunction::CreateLambda([CanBindProperty](UFunction* Function)
									{
										if (Function->NumParms != 1)
										{
											// only allow binding object member functions which have no parameters
											return false;
										}

										if (FProperty* ReturnProperty = Function->GetReturnProperty())
										{
											return CanBindProperty(ReturnProperty, {});
										}

										return false;
									});

								Args.OnCanBindToClass = FOnCanBindToClass::CreateLambda([](UClass* InClass)
									{
										return true;
									});

								Args.OnCanBindToSubObjectClass = FOnCanBindToSubObjectClass::CreateLambda([](UClass* InClass)
									{
										// CanBindToSubObjectClass does the opposite of what it's name says.  True means don't allow bindings
										// don't allow binding to any object propertoes (forcing use of thread safe functions to access objects)
										return true;
									});

								Args.OnCanAcceptPropertyOrChildrenWithBindingChain = FOnCanAcceptPropertyOrChildrenWithBindingChain::CreateLambda([](FProperty* InProperty, TConstArrayView<FBindingChainElement> BindingChain)
									{
										// Make only blueprint visible properties visible for binding.
										return true;// InProperty->HasAnyPropertyFlags(CPF_BlueprintVisible);
									});


								Args.OnAddBinding = FOnAddBinding::CreateLambda(
									[Parameter, TransactionObject, ContextClassOwner, SharedVariables](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
									{
										if (Parameter)
										{
											const FScopedTransaction Transaction(NSLOCTEXT("ContextPropertyWidget", "Change Property Binding", "Change Property Binding"));
											TransactionObject->Modify(true);

											if (!Parameter->GetPtr<PropertyType>())
											{
												Parameter->InitializeAs(PropertyType::StaticStruct());
											}

											PropertyType& Property = Parameter->GetMutable<PropertyType>();

											Property.Variable = FAnimNextVariableReference::FromName(InBindingChain.Last().Field.GetFName(), SharedVariables);
											TransactionObject->PostEditChange();
										}
									});

								const IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
								PropertyAccessEditor.FillPropertyMenu(MenuBuilder, Args, NAME_None, Struct, BindingChain);
							}
						));
				}
			}
		}
	}
	
	void CreateUAFFloatPropertyMenus(UObject* TransactionObject, const IHasContextClass* ContextClassOwner, FInstancedStruct* Parameter, FMenuBuilder& MenuBuilder, TFunction<void()> BindingChanged)
	{
		CreateUAFVariableMenus<FFloatAnimProperty>("double", TransactionObject, ContextClassOwner, Parameter, MenuBuilder);
	}
		
	void CreateUAFNamePropertyMenus(UObject* TransactionObject, const IHasContextClass* ContextClassOwner, FInstancedStruct* Parameter, FMenuBuilder& MenuBuilder, TFunction<void()> BindingChanged)
	{
		CreateUAFVariableMenus<FNameAnimProperty>("FName", TransactionObject, ContextClassOwner, Parameter, MenuBuilder);
	}
	
	void CreateUAFBoolPropertyMenus(UObject* TransactionObject, const IHasContextClass* ContextClassOwner, FInstancedStruct* Parameter, FMenuBuilder& MenuBuilder, TFunction<void()> BindingChanged)
	{
		CreateUAFVariableMenus<FBoolAnimProperty>("bool", TransactionObject, ContextClassOwner, Parameter, MenuBuilder);
	}
    	
	void CreateUAFEnumPropertyMenus(UObject* TransactionObject, const IHasContextClass* ContextClassOwner, FInstancedStruct* Parameter, FMenuBuilder& MenuBuilder, TFunction<void()> BindingChanged)
	{
		CreateUAFVariableMenus<FEnumAnimProperty>("enum", TransactionObject, ContextClassOwner, Parameter, MenuBuilder);
	}
	
	void RegisterVariableMenus()
	{
		UE::ChooserEditor::FObjectChooserWidgetFactories::RegisterParameterMenuCreator(FChooserParameterFloatBase::StaticStruct(), CreateUAFFloatPropertyMenus);
		UE::ChooserEditor::FObjectChooserWidgetFactories::RegisterParameterMenuCreator(FChooserParameterNameBase::StaticStruct(), CreateUAFNamePropertyMenus);
		UE::ChooserEditor::FObjectChooserWidgetFactories::RegisterParameterMenuCreator(FChooserParameterBoolBase::StaticStruct(), CreateUAFBoolPropertyMenus);
		UE::ChooserEditor::FObjectChooserWidgetFactories::RegisterParameterMenuCreator(FChooserParameterEnumBase::StaticStruct(), CreateUAFEnumPropertyMenus);
	}
}

#undef LOCTEXT_NAMESPACE
