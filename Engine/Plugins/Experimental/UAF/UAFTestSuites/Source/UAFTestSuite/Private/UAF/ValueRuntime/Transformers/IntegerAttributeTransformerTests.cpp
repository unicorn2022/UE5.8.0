// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimNextTest.h"
#include "Animation/BuiltInAttributeTypes.h"
#include "UAF/AbstractSkeleton/AbstractSkeletonSetBinding.h"
#include "UAF/Attributes/AttributeBindingDataCache.h"
#include "UAF/ValueRuntime/ValueRuntimeRegistry.h"
#include "UAF/ValueRuntime/Transformers/Accumulate.h"
#include "UAF/ValueRuntime/Transformers/AdditiveSpace.h"
#include "UAF/ValueRuntime/Transformers/Interpolate.h"
#include "UAF/ValueRuntime/Transformers/Overwrite.h"
#include "UAF/ValueRuntime/Transformers/TransformerTestUtil.h"
#include "UObject/Package.h"

namespace UE::UAF::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransformerTests_IntegerAttribute, "Animation.UAF.ValueRuntime.Transformers.IntegerAttribute", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FTransformerTests_IntegerAttribute::RunTest(const FString& InParameters)
	{
		ON_SCOPE_EXIT{ FUtils::CleanupAfterTests(); };

		TValueOrError<TNonNullPtr<UAbstractSkeletonSetBinding>, FString> SetBindingResult = BuildSetBinding({});

		if (SetBindingResult.HasError())
		{
			AddError(SetBindingResult.GetError());
			return false;
		}
		
		TNonNullPtr<UAbstractSkeletonSetBinding> SetBinding = SetBindingResult.GetValue();

		SetBinding->AddAttributeToSet(FAnimationAttributeIdentifier(TEXT("AttrA"), INDEX_NONE, NAME_None, FIntegerAnimationAttribute::StaticStruct()), "SetA");
		SetBinding->AddAttributeToSet(FAnimationAttributeIdentifier(TEXT("AttrB"), INDEX_NONE, NAME_None, FIntegerAnimationAttribute::StaticStruct()), "SetB");
		SetBinding->AddAttributeToSet(FAnimationAttributeIdentifier(TEXT("AttrC"), INDEX_NONE, NAME_None, FIntegerAnimationAttribute::StaticStruct()), "SetC");
		
		const FAttributeBindingDataPtr& BindingData = GAttributeBindingDataCache.GetOrAdd(SetBinding, nullptr);
		FAttributeNamedSetPtr SetA = BindingData->FindNamedSet("SetA");
		FAttributeNamedSetPtr SetB = BindingData->FindNamedSet("SetB");
		FAttributeNamedSetPtr SetC = BindingData->FindNamedSet("SetC");

		// Overwrite
		AddInfo("Overwrite");
		{
			// 0.25 weight
			{
				FTransformerUnaryTest<FIntegerAnimationAttribute, false> IntegerTest;
				IntegerTest.AttributeName = "AttrA";
				IntegerTest.Input.Value = 5;
				IntegerTest.ApplyTransformer = [](const FValueBundleHeap& InputValues, FValueBundleHeap& OutputValues)
					{
						const auto& TransformerMap = FValueRuntimeRegistry::Get().GetTransformerMap();
						Transformers::FOverwrite::Apply(TransformerMap, InputValues, 0.25f, OutputValues);
					};
				IntegerTest.IsExpectedResult = [&](TOptional<FIntegerAnimationAttribute> Result) -> TOptional<FString>
					{
						if (!Result.IsSet())
						{
							return FString("Overwrite - 0.25 weight - expected to produce a value but received none");
						}

						constexpr int32 ExpectedValue = static_cast<int32>(0.25f * 5);
						if (Result->Value != ExpectedValue)
						{
							return FString::Format(TEXT("Overwrite - 0.25 weight - expected {0}, Received {1}"), { ExpectedValue, Result->Value });
						}

						return TOptional<FString>();
					};
			}
		}
		
		// Accumulate
		AddInfo("Accumulate");
		{
			// 0.25 weight
			{
				FTransformerBinaryTest<Transformers::FAccumulate, FIntegerAnimationAttribute, EInplaceTransformationSupport::Allow> IntegerTest;
				IntegerTest.AttributeName = "AttrA";
				IntegerTest.InputA.Value = 5;
				IntegerTest.InputB.Value = 8;
				IntegerTest.ApplyTransformer = [](const FValueBundleHeap& InputAValues, const FValueBundleHeap& InputBValues, FValueBundleHeap& OutputValues)
					{
						const auto& TransformerMap = FValueRuntimeRegistry::Get().GetTransformerMap();
						Transformers::FAccumulate::Apply(TransformerMap, InputAValues, InputBValues, 0.25f, OutputValues);
					};
				IntegerTest.IsExpectedResult = [&](TOptional<FIntegerAnimationAttribute> Result) -> TOptional<FString>
					{
						if (!Result.IsSet())
						{
							return FString("Accumulate - 0.25 weight - expected to produce a value but received none");
						}
						constexpr int32 ExpectedValue = static_cast<int32>(5 + (0.25 * 8));
						if (Result->Value != ExpectedValue)
						{
							return FString::Format(TEXT("Accumulate - 0.25 weight - expected {0}, received {1}"), { ExpectedValue, Result->Value });
						}
						return TOptional<FString>();
					};

				IntegerTest.Run(*this, SetA);
			}
		}

		// Interpolate
		AddInfo("Interpolate");
		{
			// 0.25 weight
			{
				FTransformerBinaryTest<Transformers::FInterpolate, FIntegerAnimationAttribute, EInplaceTransformationSupport::Allow> IntegerTest;
				IntegerTest.AttributeName = "AttrA";
				IntegerTest.InputA.Value = 5;
				IntegerTest.InputB.Value = 8;
				IntegerTest.ApplyTransformer = [](const FValueBundleHeap& InputAValues, const FValueBundleHeap& InputBValues, FValueBundleHeap& OutputValues)
					{
						const auto& TransformerMap = FValueRuntimeRegistry::Get().GetTransformerMap();
						Transformers::FInterpolate::Apply(TransformerMap, InputAValues, InputBValues, 0.25f, OutputValues);
					};
				IntegerTest.IsExpectedResult = [&](TOptional<FIntegerAnimationAttribute> Result) -> TOptional<FString>
					{
						if (!Result.IsSet())
						{
							return FString("Interpolate - 0.25 weight -expected to produce a value but received none");
						}
						constexpr int32 ExpectedValue = static_cast<int32>((0.75 * 5) + (0.25 * 8));
						if (Result->Value != ExpectedValue)
						{
							return FString::Format(TEXT("Interpolate - 0.25 weight - expected {0}, received {1}"), { ExpectedValue, Result->Value });
						}
						return TOptional<FString>();
					};

				IntegerTest.Run(*this, SetA);
			}
		}

		// Layer
		AddInfo("Layer");
		{
			// Value in base but not in layer
			{
				// Weight 0.25
				{
					FTransformerBinaryTest<Transformers::FLayer, FIntegerAnimationAttribute, EInplaceTransformationSupport::Allow> IntegerTest;
					IntegerTest.AttributeName = "AttrA";
					IntegerTest.WeightB = 0.25f;
					IntegerTest.InputA = FIntegerAnimationAttribute({ 5 });
					IntegerTest.InputB.Reset();
					IntegerTest.IsExpectedResult = [&](TOptional<FIntegerAnimationAttribute> Result) -> TOptional<FString>
						{
							if (!Result.IsSet())
							{
								return FString("Layer - 0.25 weight, case 1 - expected to produce a value but received none");
							}
							
							constexpr int32 ExpectedValue = 5;
							if (Result->Value != ExpectedValue)
							{
								return FString::Format(TEXT("Layer - 0.25 weight, case 1 - expected {0}, received {1}"), { ExpectedValue, Result->Value });
							}
							
							return TOptional<FString>();
						};

					IntegerTest.Run(*this, SetA, SetB);
				}
			}

			// Value in layer but not in base
			{
				// Weight 0.25
				{
					FTransformerBinaryTest<Transformers::FLayer, FIntegerAnimationAttribute, EInplaceTransformationSupport::Allow> IntegerTest;
					IntegerTest.AttributeName = "AttrC";
					IntegerTest.WeightB = 0.25f;
					IntegerTest.InputA.Reset();
					IntegerTest.InputB = FIntegerAnimationAttribute({ 8 });
					IntegerTest.IsExpectedResult = [&](TOptional<FIntegerAnimationAttribute> Result) -> TOptional<FString>
						{
							if (Result.IsSet())
							{
								return FString("Layer - 0.25 weight, case 2 - expected to not produce a value but received one");
							}
														
							return TOptional<FString>();
						};

					IntegerTest.Run(*this, SetB, SetC);
				}
			}
			
			// Value in both base and layer
			{
				// Weight 0.25
				{
					FTransformerBinaryTest<Transformers::FLayer, FIntegerAnimationAttribute, EInplaceTransformationSupport::Allow> IntegerTest;
					IntegerTest.AttributeName = "AttrB";
					IntegerTest.WeightB = 0.25f;
					IntegerTest.InputA = FIntegerAnimationAttribute({ 5 });
					IntegerTest.InputB = FIntegerAnimationAttribute({ 8 });
					IntegerTest.IsExpectedResult = [&](TOptional<FIntegerAnimationAttribute> Result) -> TOptional<FString>
						{
							if (!Result.IsSet())
							{
								return FString("Layer - 0.25 weight, case 3 - expected to produce a value but received none");
							}
							
							constexpr int32 ExpectedValue = static_cast<int32>((0.75f * 5) + (0.25F * 8));
							if (Result->Value != ExpectedValue)
							{
								return FString::Format(TEXT("Layer - 0.25 weight, case 3 - expected {0}, received {1}"), { ExpectedValue, Result->Value });
							}
							
							return TOptional<FString>();
						};

					IntegerTest.Run(*this, SetA, SetB);
				}
			}
		}

		// Make Additive Space
		AddInfo("MakeAdditiveSpace");
		{
			// 0.25 weight
			{
				FTransformerBinaryTest<Transformers::FMakeAdditiveSpace, FIntegerAnimationAttribute, EInplaceTransformationSupport::Disallow> IntegerTest;
				IntegerTest.AttributeName = "AttrA";
				IntegerTest.InputA.Value = 5;
				IntegerTest.InputB.Value = 8;
				IntegerTest.ApplyTransformer = [](const FValueBundleHeap& InputAValues, const FValueBundleHeap& InputBValues, FValueBundleHeap& OutputValues)
					{
						const auto& TransformerMap = FValueRuntimeRegistry::Get().GetTransformerMap();
						Transformers::FMakeAdditiveSpace::Apply(TransformerMap, InputAValues, InputBValues, OutputValues);
					};
				IntegerTest.IsExpectedResult = [&](TOptional<FIntegerAnimationAttribute> Result) -> TOptional<FString>
					{
						if (!Result.IsSet())
						{
							return FString("MakeAdditiveSpace - expected to produce a value but received none");
						}
						constexpr int32 ExpectedValue = 8 - 5;
						if (Result->Value != ExpectedValue)
						{
							return FString::Format(TEXT("MakeAdditiveSpace - expected {0}, received {1}"), { ExpectedValue, Result->Value });
						}
						return TOptional<FString>();
					};

				IntegerTest.Run(*this, SetA);
			}
		}

		// Apply Additive Space
		AddInfo("ApplyAdditiveSpace");
		{
			// 0.25 weight
			{
				FTransformerBinaryTest<Transformers::FMakeAdditiveSpace, FIntegerAnimationAttribute, EInplaceTransformationSupport::Allow> IntegerTest;
				IntegerTest.AttributeName = "AttrA";
				IntegerTest.InputA.Value = 5;
				IntegerTest.InputB.Value = 8;
				IntegerTest.ApplyTransformer = [](const FValueBundleHeap& InputAValues, const FValueBundleHeap& InputBValues, FValueBundleHeap& OutputValues)
					{
						const_cast<FValueBundleHeap&>(InputBValues).SetValueSpace(FValueSpace(EValueSpaceType::Local, true));
						
						const auto& TransformerMap = FValueRuntimeRegistry::Get().GetTransformerMap();
						Transformers::FApplyAdditiveSpace::Apply(TransformerMap, InputAValues, InputBValues, 0.25f, OutputValues);
					};
				IntegerTest.IsExpectedResult = [&](TOptional<FIntegerAnimationAttribute> Result) -> TOptional<FString>
					{
						if (!Result.IsSet())
						{
							return FString("ApplyAdditiveSpace - 0.25 weight - expected to produce a value but received none");
						}
						constexpr int32 ExpectedValue = 5 + static_cast<int32>(0.25f * 8);
						if (Result->Value != ExpectedValue)
						{
							return FString::Format(TEXT("ApplyAdditiveSpace - 0.25 weight - expected {0}, received {1}"), { ExpectedValue, Result->Value });
						}
						return TOptional<FString>();
					};

				IntegerTest.Run(*this, SetA);
			}
		}
		
		// Sanitize
		// Nothing to do
		
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS