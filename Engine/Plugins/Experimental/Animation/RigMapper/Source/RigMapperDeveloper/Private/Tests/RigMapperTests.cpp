// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "RigMapperDefinition.h"
#include "RigMapper.h"
#include "RigMapperProcessor.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "Animation/AnimSequence.h"
#include "UObject/ConstructorHelpers.h"


#if WITH_DEV_AUTOMATION_TESTS

DEFINE_LOG_CATEGORY_STATIC(LogRigMapperTest, Verbose, All)


TArray<URigMapperDefinition*> CreateValidRigMapperDefinitions1()
{
	TArray<URigMapperDefinition*> Definitions;
	Definitions.SetNumUninitialized(2);

	// create the first set of feature definitions, two of each type
	FRigMapperFeatureDefinitions Features1;

	TMap<FString, double> Inputs1;
	Inputs1.Add(FString(TEXT("InputVal1")), 0.25f);
	Inputs1.Add(FString(TEXT("InputVal2")), 0.25f);
	Inputs1.Add(FString(TEXT("InputVal3")), 0.5f);
	FRigMapperWsFeature WSFeature1(FString(TEXT("TestWSFeature1")));
	WSFeature1.Inputs = Inputs1;
	TMap<FString, double> Inputs2;
	Inputs2.Add(FString(TEXT("InputVal4")), 0.4f);
	Inputs2.Add(FString(TEXT("InputVal5")), 0.6f);
	FRigMapperWsFeature WSFeature2(FString(TEXT("TestWSFeature2")));
	WSFeature2.Inputs = Inputs2;
	Features1.WeightedSums.Add(WSFeature1);
	Features1.WeightedSums.Add(WSFeature2);

	FRigMapperMultiplyFeature MultFeature1(FString(TEXT("TestMultFeature1")));
	MultFeature1.Inputs = { FString(TEXT("InputVal1")), FString(TEXT("InputVal2")) };
	FRigMapperMultiplyFeature MultFeature2(FString(TEXT("TestMultFeature2")));
	MultFeature2.Inputs = { FString(TEXT("InputVal2")), FString(TEXT("InputVal3")) };
	Features1.Multiply.Add(MultFeature1);
	Features1.Multiply.Add(MultFeature2);

	FRigMapperSdkFeature SDKFeature1(FString(TEXT("TestSDKFeature1")));
	SDKFeature1.Input = { FString(TEXT("InputVal2")) };
	SDKFeature1.Keys = { { 0, 0 }, { 0.5, 0.6 }, { 1.0, 1.0 } };
	FRigMapperSdkFeature SDKFeature2(FString(TEXT("TestSDKFeature2")));
	SDKFeature2.Input = { FString(TEXT("InputVal5")) };
	SDKFeature2.Keys = { { 0, 0.25 }, { 1, 0.5 } };
	Features1.SDKs.Add(SDKFeature1);
	Features1.SDKs.Add(SDKFeature2);

	Definitions[0] = NewObject<URigMapperDefinition>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	if (Definitions[0] != nullptr)
	{
		Definitions[0]->Inputs = { FString{ TEXT("InputVal1")}, FString{ TEXT("InputVal2")}, FString{ TEXT("InputVal3")}, FString{ TEXT("InputVal4")}, FString{ TEXT("InputVal5")} };
		Definitions[0]->Features = Features1;
		TMap<FString, FString> Outputs;
		Outputs.Add(FString(TEXT("OutputVal1")), FString(TEXT("TestWSFeature1")));
		Outputs.Add(FString(TEXT("OutputVal2")), FString(TEXT("TestWSFeature2")));
		Outputs.Add(FString(TEXT("OutputVal3")), FString(TEXT("TestMultFeature1")));
		Outputs.Add(FString(TEXT("OutputVal4")), FString(TEXT("TestMultFeature2")));
		Outputs.Add(FString(TEXT("OutputVal5")), FString(TEXT("TestSDKFeature1")));
		Outputs.Add(FString(TEXT("OutputVal6")), FString(TEXT("TestSDKFeature2")));
		Definitions[0]->Outputs = Outputs;
		Definitions[0]->NullOutputs = { FString{ TEXT("OutputVal7")}, FString{ TEXT("OutputVal8")} };
	}

	// create the second set of feature definitions, one of each type
	FRigMapperFeatureDefinitions Features2;

	TMap<FString, double> Inputs3;
	Inputs3.Add(FString(TEXT("OutputVal1")), 0.2f);
	Inputs3.Add(FString(TEXT("OutputVal2")), 0.7f);
	Inputs3.Add(FString(TEXT("OutputVal3")), 0.1f);
	FRigMapperWsFeature WSFeature3(FString(TEXT("TestWSFeature3")));
	WSFeature3.Inputs = Inputs3;
	Features2.WeightedSums.Add(WSFeature3);

	FRigMapperMultiplyFeature MultFeature3(FString(TEXT("TestMultFeature3")));
	MultFeature3.Inputs = { FString(TEXT("OutputVal4")), FString(TEXT("OutputVal5")) };
	Features2.Multiply.Add(MultFeature3);

	FRigMapperSdkFeature SDKFeature3(FString(TEXT("TestSDKFeature3")));
	SDKFeature3.Input = { FString(TEXT("OutputVal6")) };
	SDKFeature3.Keys = { { 0, 0 }, { 1.0, 0.8 } };
	Features2.SDKs.Add(SDKFeature3);

	Definitions[1] = NewObject<URigMapperDefinition>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	if (Definitions[1] != nullptr)
	{
		Definitions[1]->Inputs = { FString{ TEXT("OutputVal1")}, FString{ TEXT("OutputVal2")}, FString{ TEXT("OutputVal3")},
			FString{ TEXT("OutputVal4")}, FString{ TEXT("OutputVal5")}, FString{ TEXT("OutputVal6")} };
		Definitions[1]->Features = Features2;
		TMap<FString, FString> Outputs;
		Outputs.Add(FString(TEXT("OutputVal9")), FString(TEXT("TestWSFeature3")));
		Outputs.Add(FString(TEXT("OutputVal10")), FString(TEXT("TestMultFeature3")));
		Outputs.Add(FString(TEXT("OutputVal11")), FString(TEXT("TestSDKFeature3")));
		Definitions[1]->Outputs = Outputs;
		Definitions[1]->NullOutputs = { };
	}

	return Definitions;
}

TArray<URigMapperDefinition*> CreateValidRigMapperDefinitions2()
{
	TArray<URigMapperDefinition*> Definitions = CreateValidRigMapperDefinitions1();

	// modify the second definition so it uses an Null Output from the 1st stage
	Definitions[1]->Inputs = { FString{ TEXT("OutputVal1")}, FString{ TEXT("OutputVal2")}, FString{ TEXT("OutputVal3") }, FString{ TEXT("OutputVal8")}, // OutputVal8 is a Null Output from the previous stage
		FString{ TEXT("OutputVal4")}, FString{ TEXT("OutputVal5")}, FString{ TEXT("OutputVal6")} };
	TMap<FString, double> Inputs;
	Inputs.Add(FString(TEXT("OutputVal1")), 0.2f);
	Inputs.Add(FString(TEXT("OutputVal2")), 0.7f);
	Inputs.Add(FString(TEXT("OutputVal8")), 0.1f);
	Definitions[1]->Features.WeightedSums[0].Inputs = Inputs;

	return Definitions;
}

TArray<URigMapperDefinition*> CreateInvalidRigMapperDefinitions1()
{
	TArray<URigMapperDefinition*> Definitions = CreateValidRigMapperDefinitions2();

	// remove the NullOutputs from the first definition which makes the baked combination of the two layers invalid
	Definitions[0]->NullOutputs = {};

	return Definitions;
}

TArray<URigMapperDefinition*> CreateInvalidRigMapperDefinitions2()
{
	TArray<URigMapperDefinition*> Definitions = CreateValidRigMapperDefinitions1();

	// remove one of the inputs for the first definition so it is referenced by a feature but no longer exists
	Definitions[0]->Inputs = { FString{ TEXT("InputVal1")}, FString{ TEXT("InputVal2")}, FString{ TEXT("InputVal3")}, FString{ TEXT("InputVal4")}}; // removed InputVal5

	// in the second definition, add a NullOutput which has the same name as one of the actual outputs
	Definitions[1]->NullOutputs = { FString{ TEXT("OutputVal11")} };

	return Definitions;
}




IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigMapperDefinitionTest, "RigMapper.RigMapperDefinition", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRigMapperDefinitionTest::RunTest(const FString& InParameters)
{
	UE_LOGF(LogRigMapperTest, Log, "Starting RigMapperDefinitionsTest1: No errors or warnings expected");
	
	TArray<URigMapperDefinition*> Definitions = CreateValidRigMapperDefinitions1();
	URigMapperDefinition* RigMapperDefinition1 = Definitions[0];

	if (!RigMapperDefinition1)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to create RigMapperDefinition1 asset");
		return false;
	}

	UE_LOGF(LogRigMapperTest, Log, "Checking validity");

	// check that asset is valid
	if (!RigMapperDefinition1->IsDefinitionValid())
	{
		UE_LOGF(LogRigMapperTest, Error, "RigMapperDefinition1 should be valid");
		return false;
	}

	UE_LOGF(LogRigMapperTest, Log, "Checking validation");
	
	// also check Validate method
	if (!RigMapperDefinition1->Validate())
	{
		UE_LOGF(LogRigMapperTest, Error, "RigMapperDefinition1 should be valid");
		return false;
	}

	UE_LOGF(LogRigMapperTest, Log, "Checking Json Export");

	// test that we can save and re-load the asset from a json string
	FString JsonString1;
	bool bExported = RigMapperDefinition1->ExportAsJsonString(JsonString1);
	if (!bExported)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to export RigMapperDefinition1 asset as a Json String");
		return false;
	}

	UE_LOGF(LogRigMapperTest, Log, "Checking Json import from previous export");
	
	// load back in to a second definition
	URigMapperDefinition* RigMapperDefinition2 = NewObject<URigMapperDefinition>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	bool bImported = RigMapperDefinition2->LoadFromJsonString(JsonString1);
	if (!bImported)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to import RigMapperDefinition asset from a Json String");
		return false;
	}

	UE_LOGF(LogRigMapperTest, Log, "Checking Json export from previous import");
	
	FString JsonString2;
	bExported = RigMapperDefinition2->ExportAsJsonString(JsonString2);
	if (!bExported)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to export RigMapperDefinition2 asset as a Json String");
		return false;
	}
	
	// check the same json string and also that functionally correct
	if (JsonString1 != JsonString2)
	{
		UE_LOGF(LogRigMapperTest, Error, "RigMapperDefinition Json export does not give the same result when re-imported");
		return false;
	}

	if (RigMapperDefinition2->Inputs != RigMapperDefinition1->Inputs)
	{
		UE_LOGF(LogRigMapperTest, Error, "RigMapperDefinition Json export does not give the same result when re-imported (Inputs)");
		return false;
	}

	if (RigMapperDefinition2->NullOutputs != RigMapperDefinition1->NullOutputs)
	{
		UE_LOGF(LogRigMapperTest, Error, "RigMapperDefinition Json export does not give the same result when re-imported (NullOutputs)");
		return false;
	}

	TArray<FString> OutputsKeys1, OutputsKeys2;
	RigMapperDefinition1->Outputs.GetKeys(OutputsKeys1);
	RigMapperDefinition2->Outputs.GetKeys(OutputsKeys2);
	if (OutputsKeys1 != OutputsKeys2)
	{
		UE_LOGF(LogRigMapperTest, Error, "RigMapperDefinition Json export does not give the same result when re-imported (Outputs)");
		return false;
	}
	for (int32 Key = 0; Key < OutputsKeys1.Num(); ++Key)
	{
		if (RigMapperDefinition1->Outputs[OutputsKeys1[Key]] != RigMapperDefinition2->Outputs[OutputsKeys1[Key]])
		{
			UE_LOGF(LogRigMapperTest, Error, "RigMapperDefinition Json export does not give the same result when re-imported (Outputs)");
			return false;
		}
	}
	
	if (RigMapperDefinition1->Features != RigMapperDefinition2->Features)
	{
		UE_LOGF(LogRigMapperTest, Error, "RigMapperDefinition Json export does not give the same result when re-imported (Features)");
		return false;
	}

	UE_LOGF(LogRigMapperTest, Log, "Checking Json file export");
	
	// repeat the test above but from file
	FString Path = FPaths::ProjectSavedDir() / TEXT("test_export.json");
	bExported = RigMapperDefinition1->ExportAsJsonFile({ Path });
	if (!bExported)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to export RigMapperDefinition1 asset as a Json file");
		return false;
	}

	UE_LOGF(LogRigMapperTest, Log, "Checking Json file export round trip");

	URigMapperDefinition* RigMapperDefinition3 = NewObject<URigMapperDefinition>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	bImported = RigMapperDefinition3->LoadFromJsonString(JsonString1);
	if (!bImported)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to import RigMapperDefinition asset from file");
		return false;
	}
	FString JsonString3;
	bExported = RigMapperDefinition3->ExportAsJsonString(JsonString3);
	if (!bExported || JsonString3 != JsonString1 )
	{
		UE_LOGF(LogRigMapperTest, Error, "File-based json export / import of RigMapperDefinition did not work");
		return false;
	}

	// test Empty method
	RigMapperDefinition2->Empty();
	if (!RigMapperDefinition2->Inputs.IsEmpty() || !RigMapperDefinition2->Outputs.IsEmpty() || !RigMapperDefinition2->Features.WeightedSums.IsEmpty() 
		|| !RigMapperDefinition2->Features.SDKs.IsEmpty() || !RigMapperDefinition2->Features.Multiply.IsEmpty()
		|| !RigMapperDefinition2->NullOutputs.IsEmpty())
	{
		UE_LOGF(LogRigMapperTest, Error, "RigMapperDefinition expected to be empty and is not");
		return false;
	}


	UE_LOGF(LogRigMapperTest, Log, "Starting RigMapperDefinitionsTest2: Error(s) and Warning(s) expected");
	
	// construct a couple of invalid definitions and check these are flagged as invalid
	TArray<URigMapperDefinition*> Definitions2 = CreateInvalidRigMapperDefinitions2();
	
	if (Definitions2[0]->IsDefinitionValid())
	{
		UE_LOGF(LogRigMapperTest, Error, "RigMapperDefinition should be invalid");
		return false;
	}

	if (Definitions2[1]->IsDefinitionValid())
	{
		UE_LOGF(LogRigMapperTest, Error, "RigMapperDefinition should be invalid");
		return false;
	}

	URigMapperDefinition* DupNameDef = NewObject<URigMapperDefinition>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	DupNameDef->Inputs = { TEXT("In1") };

	FRigMapperWsFeature WS1(TEXT("DupName"));
	WS1.Inputs.Add(TEXT("In1"), 1.0);
	FRigMapperMultiplyFeature Mul1(TEXT("DupName"));   // same name as WS1
	Mul1.Inputs = { TEXT("In1"), TEXT("In1") };

	DupNameDef->Features.WeightedSums.Add(WS1);
	DupNameDef->Features.Multiply.Add(Mul1);

	DupNameDef->Outputs = {};
	DupNameDef->NullOutputs = {};

	FRigMapperValidationContext DupContext;
	const bool bIsValid = DupNameDef->IsDefinitionValid(&DupContext, /*bWarn=*/false, /*bForce=*/true);

	if (bIsValid)
	{
		UE_LOGF(LogRigMapperTest, Error, "Definition with duplicate feature names should not validate")
		return false;
	}

	if (!DupContext.HasErrors())
	{
		UE_LOGF(LogRigMapperTest, Error, "Duplicate feature names should populate errors in the validation context")
		return false;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigMapperTest, "RigMapper.RigMapper", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

using FRigMapperIndexMap = TArray<int32>;


// the three functions below are adapted from FAnimNode_RigMapper; added here so we can test outside of the anim node

FRigMapperIndexMap MakeIndexMap(const TSharedPtr<FacialRigMapping::FRigMapper>& InOutputRig, const TSharedPtr<FacialRigMapping::FRigMapper>& InInputRig)
{
	const TArray<FName> FromOutputs = InOutputRig->GetOutputNames();
	const TArray<FName> ToInputs =  InInputRig->GetInputNames();

	FRigMapperIndexMap IndexMapping;

	IndexMapping.Reserve(ToInputs.Num());

	for (const FName& Input : ToInputs)
	{
		IndexMapping.Add(FromOutputs.Find(Input));
	}

	return IndexMapping;
}


bool InitializeRigMapping(const TArray<URigMapperDefinition*> & InRigMapperDefinitions, TArray<TSharedPtr<FacialRigMapping::FRigMapper>>& OutRigMappers, 
	TArray<FRigMapperIndexMap>& OutIndexMaps)
{
	OutRigMappers.Empty();
	OutIndexMaps.Empty();

	if (InRigMapperDefinitions.IsEmpty())
	{
		return false;
	}
	for (int32 i = 0; i < InRigMapperDefinitions.Num(); i++)
	{
		if (!InRigMapperDefinitions[i])
		{
			UE_LOGF(LogTemp, Error, "Invalid definition at index %d", i)
			return false;
		}
	}
	OutRigMappers.Empty(InRigMapperDefinitions.Num());
	OutIndexMaps.Empty(InRigMapperDefinitions.Num() - 1);

	for (const URigMapperDefinition* Definition : InRigMapperDefinitions)
	{
		TSharedPtr<FacialRigMapping::FRigMapper> RigMapper = MakeShared<FacialRigMapping::FRigMapper>();

		if (!RigMapper->LoadDefinition(Definition))
		{
			UE_LOGF(LogTemp, Error, "Could not load definition %ls", *Definition->GetPathName())
			OutRigMappers.Empty();
			return false;
		}

		if (OutRigMappers.Num() > 0)
		{
			OutIndexMaps.Add(MakeIndexMap(OutRigMappers.Last(), RigMapper));
		}

		OutRigMappers.Add(RigMapper);
	}

	return true;
}

void EvaluateRigMapping(const TMap<FString, double>& InInputs, const TArray<FRigMapperIndexMap>& InIndexMaps, TArray<TSharedPtr<FacialRigMapping::FRigMapper>>& InOutRigMappers, TMap<FString, double> & OutOutputs)
{
	OutOutputs.Empty();

	for (int32 i = 0; i < InOutRigMappers.Num(); i++)
	{
		const TSharedPtr<FacialRigMapping::FRigMapper> RigMapper = InOutRigMappers[i];
		RigMapper->SetDirty();

		// Set first rig mapper's input values from current pose
		if (i == 0)
		{

			const TArray<FName>& InputNames = RigMapper->GetInputNames();
			for (int32 n = 0; n < InputNames.Num(); n++)
			{
				if (const double* FoundInput = InInputs.Find(InputNames[n].ToString()); FoundInput != nullptr)
				{
					RigMapper->SetDirectValue(n, *FoundInput);
				}
			}
		}
		else
		{
			// Other RigMappers: Link/Map prev rig mapper outputs to current inputs
			check(InIndexMaps.IsValidIndex(i - 1))
			const FRigMapperIndexMap& IndexMapping = InIndexMaps[i - 1];

			// Get output values from previous rig mapper
			TArray<double> PrevOutputValuesInOrder;
			{
				InOutRigMappers[i - 1]->GetOutputValuesInOrder(PrevOutputValuesInOrder);
			}

			// Set input values for current rig mapper
			{
				for (int32 n = 0; n < IndexMapping.Num(); n++)
				{
					if (const int32 Index = IndexMapping[n]; Index != INDEX_NONE)
					{
						RigMapper->SetDirectValue(n, PrevOutputValuesInOrder[Index]);
					}
				}
			}
		}
	}

	// Set outputs from last rig mapper's output values 
	if (!InOutRigMappers.IsEmpty())
	{
		OutOutputs.Empty();
		for (const TPair<FName, double> OutputPair : InOutRigMappers.Last()->GetOutputValues())
		{
			OutOutputs.Add(OutputPair.Key.ToString(), OutputPair.Value);
		}
	}
}

bool FRigMapperTest::RunTest(const FString& InParameters)
{
	TArray<URigMapperDefinition*> RigMapperDefinitions;
	RigMapperDefinitions = CreateValidRigMapperDefinitions1();
	if (RigMapperDefinitions.Num() != 2)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to create RigMapperDefinition assets");
		return false;
	}

	TArray<TSharedPtr<FacialRigMapping::FRigMapper>> RigMappers;
	TArray<FRigMapperIndexMap> IndexMaps;

	bool bInitialized = InitializeRigMapping(RigMapperDefinitions, RigMappers, IndexMaps);
	if (!bInitialized)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to initialize RigMapping");
		return false;
	}

	// set up some sample inputs
	TMap<FString, double> Inputs;
	Inputs.Add(FString(TEXT("InputVal1")), 0.25);
	Inputs.Add(FString(TEXT("InputVal2")), 0.4);
	Inputs.Add(FString(TEXT("InputVal3")), 0.5);
	Inputs.Add(FString(TEXT("InputVal4")), 0.6);
	Inputs.Add(FString(TEXT("InputVal5")), 0.75);

	// evaluate the rig mappers; this evaluates the whole thing in the same way in which this is done in the anim node
	TMap<FString, double> Outputs;
	EvaluateRigMapping(Inputs, IndexMaps, RigMappers, Outputs);

	// check that the outputs are correct

	// Inputs
	//	InputVal1 = 0.25
	//	InputVal2 = 0.4
	//	InputVal3 = 0.5
	//	InputVal4 = 0.6
	//	InputVal5 = 0.75
	// 
	// Layer1
	//	TestWSFeature1 = InputVal1 * 0.25 + InputVal2 * 0.25 + InputVal3 * 0.5 = 0.25 * 0.25 + 0.4 * 0.25 + 0.5 * 0.5 = 0.4125
	//	TestWSFeature2 = InputVal4 * 0.4 + InputVal5 * 0.6 = 0.6 * 0.4 + 0.75 * 0.6 = 0.69
	//	TestMultFeature1 = InputVal1 * InputVal2 = 0.25 * 0.4 = 0.1
	//	TestMultFeature2 = InputVal2 * InputVal3 = 0.4 * 0.5 = 0.2
	//	TestSDKFeature1 = InputVal2 SDK values (0,0) (0.5,0.6) (1.0,1.0) = 0.48
	//	TestSDKFeature2 = InputVal5 SDK values (0,0.25) (1,0.5) = 0.4375
	// 
	//	Outputs:
	// 		OutputVal1 = TestWSFeature1 = 0.4125
	//		OutputVal2 = TestWSFeature2 = 0.69
	//		OutputVal3 = TestMultFeature1 = 0.1
	//		OutputVal4 = TestMultFeature2 = 0.2
	//		OutputVal5 = TestSDKFeature1 = 0.48
	//		OutputVal6 = TestSDKFeature2 = 0.4375
	// 
	// Layer2
	//	TestWSFeature3 = OutputVal1 * 0.2 + OutputVal2 * 0.7 + OutputVal3 * 0.1 = 0.4125 * 0.2 + 0.69 * 0.7 + 0.1 * 0.1 = 0.0925
	//	TestMultFeature3 = OutputVal4 * OutputVal5 = 0.096
	//	TestSDKFeature3 = OutputVal6 SDK values (0,0) (1.0, 0.8) = 0.35
	//
	//	Outputs:
	//		OutputVal9 = TestWSFeature3 = 0.5755
	//		OutputVal10 = TestMultFeature3 = 0.096
	//		OutputVal11 = TestSDKFeature3 = 0.35

	const double* OutputVal9 = Outputs.Find(FString(TEXT("OutputVal9")));
	if (!OutputVal9)
	{
		UE_LOGF(LogRigMapperTest, Error, "Could not find Output Value 9 (TestWSFeature3)");
		return false;
	}
	if (!FMath::IsNearlyEqual(*OutputVal9, 0.5755, SMALL_NUMBER))
	{
		UE_LOGF(LogRigMapperTest, Error, "Invalid value for Output Value 9 (TestWSFeature3): %f instead of 0.5755", *OutputVal9);
		return false;
	}
	const double* OutputVal10 = Outputs.Find(FString(TEXT("OutputVal10")));
	if (!OutputVal10)
	{
		UE_LOGF(LogRigMapperTest, Error, "Could not find Output Value 10 (TestMultFeature3)");
		return false;
	}
	if (!FMath::IsNearlyEqual(*OutputVal10, 0.096, SMALL_NUMBER))
	{
		UE_LOGF(LogRigMapperTest, Error, "Invalid value for Output Value 10 (TestMultFeature3): %f instead of 0.096", *OutputVal10);
		return false;
	}
	const double* OutputVal11 = Outputs.Find(FString(TEXT("OutputVal11")));
	if (!OutputVal11)
	{
		UE_LOGF(LogRigMapperTest, Error, "Could not find Output Value 11 (TestSdkFeature3)");
		return false;
	}
	if (!FMath::IsNearlyEqual(*OutputVal11, 0.35, SMALL_NUMBER))
	{
		UE_LOGF(LogRigMapperTest, Error, "Invalid value for Output Value 11 (TestSdkFeature3): %f instead of 0.35", *OutputVal11);
		return false;
	}

	// TODO would also be good to test this in the anim node but beyond the scope of the current tests
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigMapperProcessorTest, "RigMapper.RigMapperProcessor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)


bool FRigMapperProcessorTest::RunTest(const FString& InParameters)
{
	const TArray<URigMapperDefinition*> RigMapperDefinitions = CreateValidRigMapperDefinitions1();
	if (RigMapperDefinitions.Num() != 2)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to create RigMapperDefinition assets");
		return false;
	}

	FRigMapperProcessor Processor(RigMapperDefinitions);
	if (!Processor.IsValid())
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to initialize RigMapperProcessor");
		return false;
	}

	// set up some sample inputs
	TArray<FName> InputNames;
	InputNames.Add(TEXT("InputVal1"));
	InputNames.Add(TEXT("InputVal2"));
	InputNames.Add(TEXT("InputVal3"));
	InputNames.Add(TEXT("InputVal4"));
	InputNames.Add(TEXT("InputVal5"));

	FRigMapperProcessor::FPoseValues InputValues;
	InputValues.Add(0.25f);
	InputValues.Add(0.4f);
	InputValues.Add(0.5f);
	InputValues.Add(0.6f);
	InputValues.Add(0.75f);

	// evaluate the rig mappers; this evaluates the whole thing in the same way in which this is done in the anim node
	FRigMapperProcessor::FPoseValues OutputValues;
	if (!Processor.EvaluateFrame(InputNames, InputValues, OutputValues))
	{
		UE_LOGF(LogRigMapperTest, Error, "RigMapperProcessor failed to evaluate input values");
		return false;
	}
	const TArray<FName>& OutputNames = Processor.GetOutputNames();

	// check that the outputs are correct

	// Inputs
	//	InputVal1 = 0.25
	//	InputVal2 = 0.4
	//	InputVal3 = 0.5
	//	InputVal4 = 0.6
	//	InputVal5 = 0.75
	// 
	// Layer1
	//	TestWSFeature1 = InputVal1 * 0.25 + InputVal2 * 0.25 + InputVal3 * 0.5 = 0.25 * 0.25 + 0.4 * 0.25 + 0.5 * 0.5 = 0.4125
	//	TestWSFeature2 = InputVal4 * 0.4 + InputVal5 * 0.6 = 0.6 * 0.4 + 0.75 * 0.6 = 0.69
	//	TestMultFeature1 = InputVal1 * InputVal2 = 0.25 * 0.4 = 0.1
	//	TestMultFeature2 = InputVal2 * InputVal3 = 0.4 * 0.5 = 0.2
	//	TestSDKFeature1 = InputVal2 SDK values (0,0) (0.5,0.6) (1.0,1.0) = 0.48
	//	TestSDKFeature2 = InputVal5 SDK values (0,0.25) (1,0.5) = 0.4375
	// 
	//	Outputs:
	// 		OutputVal1 = TestWSFeature1 = 0.4125
	//		OutputVal2 = TestWSFeature2 = 0.69
	//		OutputVal3 = TestMultFeature1 = 0.1
	//		OutputVal4 = TestMultFeature2 = 0.2
	//		OutputVal5 = TestSDKFeature1 = 0.48
	//		OutputVal6 = TestSDKFeature2 = 0.4375
	// 
	// Layer2
	//	TestWSFeature3 = OutputVal1 * 0.2 + OutputVal2 * 0.7 + OutputVal3 * 0.1 = 0.4125 * 0.2 + 0.69 * 0.7 + 0.1 * 0.1 = 0.0925
	//	TestMultFeature3 = OutputVal4 * OutputVal5 = 0.096
	//	TestSDKFeature3 = OutputVal6 SDK values (0,0) (1.0, 0.8) = 0.35
	//
	//	Outputs:
	//		OutputVal9 = TestWSFeature3 = 0.5755
	//		OutputVal10 = TestMultFeature3 = 0.096
	//		OutputVal11 = TestSDKFeature3 = 0.35

	int32 OutputIndex = OutputNames.Find(FName(TEXT("OutputVal9")));
	if (!OutputValues.IsValidIndex(OutputIndex))
	{
		UE_LOGF(LogRigMapperTest, Error, "Invalid index for Output Value 9 (TestWSFeature3): %d", OutputIndex);
		return false;
	}
	if (!OutputValues[OutputIndex].IsSet())
	{
		UE_LOGF(LogRigMapperTest, Error, "Invalid value for Output Value 9 (TestWSFeature3)");
		return false;
	}
	if (!FMath::IsNearlyEqual(OutputValues[OutputIndex].GetValue(), 0.5755, 0.00001))
	{
		UE_LOGF(LogRigMapperTest, Error, "Invalid value for Output Value 9 (TestWSFeature3): %f instead of 0.5755", OutputValues[OutputIndex].GetValue());
		return false;
	}

	OutputIndex = OutputNames.Find(FName(TEXT("OutputVal10")));
	if (!OutputValues.IsValidIndex(OutputIndex))
	{
		UE_LOGF(LogRigMapperTest, Error, "Invalid index for Output Value 10 (TestMultFeature3): %d", OutputIndex);
		return false;
	}
	if (!OutputValues[OutputIndex].IsSet())
	{
		UE_LOGF(LogRigMapperTest, Error, "Invalid value for Output Value 10 (TestMultFeature3)");
		return false;
	}
	if (!FMath::IsNearlyEqual(OutputValues[OutputIndex].GetValue(), 0.096, 0.00001))
	{
		UE_LOGF(LogRigMapperTest, Error, "Invalid value for Output Value 10 (TestMultFeature3): %f instead of 0.096", OutputValues[OutputIndex].GetValue());
		return false;
	}

	OutputIndex = OutputNames.Find(FName(TEXT("OutputVal11")));
	if (!OutputValues.IsValidIndex(OutputIndex))
	{
		UE_LOGF(LogRigMapperTest, Error, "Invalid index for Output Value 11 (TestSdkFeature3): %d", OutputIndex);
		return false;
	}
	if (!OutputValues[OutputIndex].IsSet())
	{
		UE_LOGF(LogRigMapperTest, Error, "Invalid value for Output Value 11 (TestSdkFeature3)");
		return false;
	}
	if (!FMath::IsNearlyEqual(OutputValues[OutputIndex].GetValue(), 0.35, 0.00001))
	{
		UE_LOGF(LogRigMapperTest, Error, "Invalid value for Output Value 11 (TestSdkFeature3): %f instead of 0.35", OutputValues[OutputIndex].GetValue());
		return false;
	}

	// TODO would also be good to test this in the anim node but beyond the scope of the current tests
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(RigMapperLinkedDefinitionsTest, "RigMapper.RigMapperLinkedDefinitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool RigMapperLinkedDefinitionsTest::RunTest(const FString& InParameters)
{
	UE_LOGF(LogRigMapperTest, Log, "Starting RigMapperLinkedDefinitionsTest1: No error or warning expected");
	
	// create two rigmapper definitions
	TArray<URigMapperDefinition*> RigMapperDefinitions1 = CreateValidRigMapperDefinitions1();
	if (RigMapperDefinitions1.Num() != 2)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to create RigMapperDefinition assets");
		return false;
	}

	// link the definitions and bake
	URigMapperLinkedDefinitions * LinkedDefinitions1 = NewObject<URigMapperLinkedDefinitions>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	if (!LinkedDefinitions1)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to create a RigMapperLinkedDefinitions");
		return false;
	}

	LinkedDefinitions1->BakedDefinition = NewObject<URigMapperDefinition>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	if (!LinkedDefinitions1)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to create RigMapperDefinition");
		return false;
	}

	LinkedDefinitions1->SourceDefinitions = RigMapperDefinitions1;
	bool bBaked = LinkedDefinitions1->BakeDefinitions();

	if (!bBaked)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to bake definitions");
		return false;
	}

	// now evaluate the result of the baked definitions and check that the results are as expected
	TArray<URigMapperDefinition*> BakedRigMapperDefinitions;
	BakedRigMapperDefinitions.SetNumUninitialized(1);
	BakedRigMapperDefinitions[0] = LinkedDefinitions1->BakedDefinition;

	TArray<TSharedPtr<FacialRigMapping::FRigMapper>> RigMappers;
	TArray<FRigMapperIndexMap> IndexMaps;

	bool bInitialized = InitializeRigMapping(BakedRigMapperDefinitions, RigMappers, IndexMaps);
	if (!bInitialized)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to initialize RigMapping");
		return false;
	}

	// set up some sample inputs
	TMap<FString, double> Inputs;
	Inputs.Add(FString(TEXT("InputVal1")), 0.25);
	Inputs.Add(FString(TEXT("InputVal2")), 0.4);
	Inputs.Add(FString(TEXT("InputVal3")), 0.5);
	Inputs.Add(FString(TEXT("InputVal4")), 0.6);
	Inputs.Add(FString(TEXT("InputVal5")), 0.75);

	// evaluate the rig mappers; this evaluates the whole thing in the same way in which this is done in the anim node
	TMap<FString, double> Outputs;
	EvaluateRigMapping(Inputs, IndexMaps, RigMappers, Outputs);


	const double* OutputVal9 = Outputs.Find(FString(TEXT("OutputVal9")));
	const double* OutputVal10 = Outputs.Find(FString(TEXT("OutputVal10")));
	const double* OutputVal11 = Outputs.Find(FString(TEXT("OutputVal11")));

	if (!OutputVal9 || !OutputVal10 || !OutputVal11 || !FMath::IsNearlyEqual(*OutputVal9, 0.5755, SMALL_NUMBER)
		|| !FMath::IsNearlyEqual(*OutputVal10, 0.096, SMALL_NUMBER) || !FMath::IsNearlyEqual(*OutputVal11, 0.35, SMALL_NUMBER))
	{
		UE_LOGF(LogRigMapperTest, Error, "Output values for tested baked rig-mapping are not as expected");
		return false;
	}

	// test other methods
	if (!LinkedDefinitions1->AreLinkedDefinitionsValid())
	{
		UE_LOGF(LogRigMapperTest, Error, "LinkedDefinitions are expected to be valid, and are not");
		return false;
	}

	if (!LinkedDefinitions1->Validate())
	{
		UE_LOGF(LogRigMapperTest, Error, "LinkedDefinitions are expected to be valid, and are not");
		return false;
	}

	// no explicit test for GetBakedInputRec here

	UE_LOGF(LogRigMapperTest, Log, "Starting RigMapperLinkedDefinitionsTest2: No error or warning expected");
	
	// test for a case where one of the NullOutputs is used; this should still be valid
	TArray<URigMapperDefinition*> RigMapperDefinitions2 = CreateValidRigMapperDefinitions2();

	URigMapperLinkedDefinitions* LinkedDefinitions2 = NewObject<URigMapperLinkedDefinitions>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	if (!LinkedDefinitions2)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to create a RigMapperLinkedDefinitions");
		return false;
	}

	LinkedDefinitions2->BakedDefinition = NewObject<URigMapperDefinition>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	if (!LinkedDefinitions2)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to create RigMapperDefinition");
		return false;
	}

	LinkedDefinitions2->SourceDefinitions = RigMapperDefinitions2;
	bBaked = LinkedDefinitions2->BakeDefinitions();

	if (!bBaked)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to bake definitions");
		return false;
	}

	if (!LinkedDefinitions2->AreLinkedDefinitionsValid())
	{
		UE_LOGF(LogRigMapperTest, Error, "LinkedDefinitions are expected to be valid, and are not");
		return false;
	}

	// now test for a couple of invalid cases and check that validation catches these

	UE_LOGF(LogRigMapperTest, Log, "Starting RigMapperLinkedDefinitionsTest3: Warning(s) expected, validation should still pass");

	// case 1 is definitions where the second set has an input that does not match any
	// output or NullOutput of the first set. Under the relaxed validation rules this is
	// allowed (the input will simply have no value at runtime), but a warning is emitted.
	TArray<URigMapperDefinition*> RigMapperDefinitions3 = CreateInvalidRigMapperDefinitions1();

	URigMapperLinkedDefinitions* LinkedDefinitions3 = NewObject<URigMapperLinkedDefinitions>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	if (!LinkedDefinitions3)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to create a RigMapperLinkedDefinitions")
		return false;
	}

	LinkedDefinitions3->BakedDefinition = NewObject<URigMapperDefinition>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	if (!LinkedDefinitions3->BakedDefinition)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to create RigMapperDefinition")
		return false;
	}

	LinkedDefinitions3->SourceDefinitions = RigMapperDefinitions3;

	FRigMapperValidationContext BakeContext3;
	bBaked = LinkedDefinitions3->BakeDefinitions(&BakeContext3);

	if (!bBaked)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to bake definitions 3 (relaxed rules - bake should succeed)")
		return false;
	}

	FRigMapperValidationContext ValidateContext3;
	if (!LinkedDefinitions3->AreLinkedDefinitionsValid(&ValidateContext3))
	{
		UE_LOGF(LogRigMapperTest, Error, "LinkedDefinitions 3 are expected to be valid (with warnings), but validation returned false")
		return false;
	}

	if (!ValidateContext3.HasWarnings())
	{
		UE_LOGF(LogRigMapperTest, Error, "LinkedDefinitions 3 should produce at least one warning about unmatched inputs, but produced none")
		return false;
	}

	if (ValidateContext3.HasErrors())
	{
		UE_LOGF(LogRigMapperTest, Error, "LinkedDefinitions 3 should not produce any errors, but produced %d", ValidateContext3.GetErrorCount())
		return false;
	}

	UE_LOGF(LogRigMapperTest, Log, "Starting RigMapperLinkedDefinitionsTest4: Error(s) expected");

	// case 2 is an example where the individual definitions are invalid (NullOutput shadows a real Output, and a feature references an input that no longer exists in Inputs)
	TArray<URigMapperDefinition*> RigMapperDefinitions4 = CreateInvalidRigMapperDefinitions2();

	URigMapperLinkedDefinitions* LinkedDefinitions4 = NewObject<URigMapperLinkedDefinitions>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	if (!LinkedDefinitions4)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to create a RigMapperLinkedDefinitions")
		return false;
	}

	LinkedDefinitions4->BakedDefinition = NewObject<URigMapperDefinition>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	if (!LinkedDefinitions4->BakedDefinition)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to create RigMapperDefinition")
		return false;
	}

	LinkedDefinitions4->SourceDefinitions = RigMapperDefinitions4;

	FRigMapperValidationContext BakeContext4;
	bBaked = LinkedDefinitions4->BakeDefinitions(&BakeContext4);

	if (bBaked)
	{
		UE_LOGF(LogRigMapperTest, Error, "Successfully baked invalid definitions 4 which is not expected")
		return false;
	}

	FRigMapperValidationContext ValidateContext4;
	if (LinkedDefinitions4->AreLinkedDefinitionsValid(&ValidateContext4))
	{
		UE_LOGF(LogRigMapperTest, Error, "LinkedDefinitions 4 are expected to be invalid, and are actually valid")
		return false;
	}

	if (!ValidateContext4.HasErrors())
	{
		UE_LOGF(LogRigMapperTest, Error, "LinkedDefinitions 4 should produce at least one error, but produced none")
		return false;
	}

	UE_LOGF(LogRigMapperTest, Log, "Starting RigMapperLinkedDefinitionsTest5: NullOutput pass-through should be silent (no warnings, no errors)");

	// Definitions2 specifically uses a NullOutput (OutputVal8) from layer 1 as an input to layer 2. The bake path should silently drop it. No warning, no error.
	TArray<URigMapperDefinition*> RigMapperDefinitions5 = CreateValidRigMapperDefinitions2();

	URigMapperLinkedDefinitions* LinkedDefinitions5 = NewObject<URigMapperLinkedDefinitions>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	LinkedDefinitions5->BakedDefinition = NewObject<URigMapperDefinition>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
	LinkedDefinitions5->SourceDefinitions = RigMapperDefinitions5;

	FRigMapperValidationContext Context5;
	bBaked = LinkedDefinitions5->BakeDefinitions(&Context5);

	if (!bBaked)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to bake definitions 5 (NullOutput pass-through should succeed)")
			return false;
	}

	if (Context5.HasErrors())
	{
		UE_LOGF(LogRigMapperTest, Error, "NullOutput pass-through produced %d unexpected error(s)", Context5.GetErrorCount())
			return false;
	}

	if (Context5.HasWarnings())
	{
		UE_LOGF(LogRigMapperTest, Error, "NullOutput pass-through produced %d unexpected warning(s) — silent drop expected", Context5.GetWarningCount())
			return false;
	}

	return true;
}

TArray<TMap<FString, double>> GetTestSequenceCurves(UAnimSequence* InTestAnimSequence, FString InControlSubStr)
{
	const FAnimationCurveData& CurveData = InTestAnimSequence->GetDataModel()->GetCurveData();
	bool bFirstCurve = true;
	TArray<float> RefKeyTimes;
	TArray<TMap<FString, double>> CurveDataRigMapperInputs;

	for (const FFloatCurve& Curve : CurveData.FloatCurves)
	{
		const FString CurveNameStr = Curve.GetName().ToString();

		if (CurveNameStr.Contains(InControlSubStr))
		{
			TArray<float> KeyTimes, KeyValues;
			Curve.GetKeys(KeyTimes, KeyValues);

			if (bFirstCurve)
			{
				bFirstCurve = false;
				CurveDataRigMapperInputs.SetNum(KeyTimes.Num());
				RefKeyTimes = KeyTimes; // we just use the keys from the first curve for simplicity so we don't rely on the anim sequence being keyed every frame; we could add all keys here
			}

			for (int32 KeyCount = 0; KeyCount < RefKeyTimes.Num(); ++KeyCount)
			{
				CurveDataRigMapperInputs[KeyCount].Add(CurveNameStr, Curve.Evaluate(RefKeyTimes[KeyCount]));
			}
		}
	}

	return CurveDataRigMapperInputs;
}


IMPLEMENT_COMPLEX_AUTOMATION_TEST(FTestRigMapperTestRawDefinitionsRoundTrip, "RigMapper.RigMapperTestRawDefinitionsRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FTestRigMapperTestRawDefinitionsRoundTrip::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add("Verify correctness of RM_FNL_FNH and RM_FNH_FNL Rig Mapper Definitions (round trip low->high followed by high->low)");
	OutTestCommands.Add("FN");

	OutBeautifiedNames.Add("Verify correctness of RM_CDL_CDH and RM_CDH_CDL Rig Mapper Definitions (round trip low->high followed by high->low)");
	OutTestCommands.Add("CD");

	OutBeautifiedNames.Add("Verify correctness of RM_MHL_MHH and RM_MHH_MHL Rig Mapper Definitions (round trip low->high followed by high->low)");
	OutTestCommands.Add("MH");

	OutBeautifiedNames.Add("Verify correctness of RM_FNH_FNM and RM_FNM_FNH Rig Mapper Definitions (round trip low->high followed by high->low); note, we have to test this by adding extra low to high and high to low layers for the FN legacy rig");
	OutTestCommands.Add("FNH_FNM");

	// NB no MHH_FNM test as RM_FNM_MHH mapping does not exist
	// NB no CDH_FNM test as RM_FNM_CDH mapping does not exist

}

bool AreMapsEqualWithTolerance(const TMap<FString, double>& InExpectedMap, const TMap<FString, double>& InActualMap, const FString& InIgnoreBelowZeroDiscrepanciesContainsString = TEXT(""),
	const TArray<FString>& InAllowedMissingOutputControls = {}, double Tolerance = KINDA_SMALL_NUMBER)
{
	// Iterate through the first map
	bool bResult = true;

	for (const TPair<FString, double>& ExpectedPair : InExpectedMap)
	{
		// Check if the second map contains the same key
		const double* ValueInActualMap = InActualMap.Find(ExpectedPair.Key);
		if (ValueInActualMap == nullptr)
		{
			// Key not found in the second map
			if (!InAllowedMissingOutputControls.Find(ExpectedPair.Key))
			{
				UE_LOGF(LogRigMapperTest, Error, "Control %ls with value %f not found in actual output", *ExpectedPair.Key, ExpectedPair.Value);
				bResult = false;
			}
			continue;
		}
		// Compare the values for the same key within the tolerance
		else if (!FMath::IsNearlyEqual(ExpectedPair.Value, *ValueInActualMap, Tolerance))
		{
			// if the control name contains InIgnoreBelowZeroDiscrepanciesContainsString and one value is zero and the
			// other is < zero, just flag this as a warning, not an error
			if (!InIgnoreBelowZeroDiscrepanciesContainsString.IsEmpty() && ExpectedPair.Key.Contains(InIgnoreBelowZeroDiscrepanciesContainsString)&&
				((FMath::IsNearlyEqual(ExpectedPair.Value, 0) && *ValueInActualMap < 0) ||
				(FMath::IsNearlyEqual(*ValueInActualMap, 0) && ExpectedPair.Value < 0)))
			{
				UE_LOGF(LogRigMapperTest, Warning, "Control %ls contains different expected and actual values; mote that curves get clamped in range 0-1 by the animation system so this is not a problem.: %f, %f", *ExpectedPair.Key, ExpectedPair.Value, *ValueInActualMap);
			}
			else
			{
				UE_LOGF(LogRigMapperTest, Error, "Control %ls contains different expected and actual values: %f, %f", *ExpectedPair.Key, ExpectedPair.Value, *ValueInActualMap);
				bResult = false;
			}
		}
	}

	for (const TPair<FString, double>& ActualPair : InActualMap)
	{
		// Check if the first map contains the same key
		const double* ValueInExpectedMap = InExpectedMap.Find(ActualPair.Key);
		if (ValueInExpectedMap == nullptr)
		{
			// Key not found in the first map
			UE_LOGF(LogRigMapperTest, Error, "Additional control %ls found in actual output", *ActualPair.Key);
			bResult = false;
		}
	}

	return bResult;
}


bool FTestRigMapperTestRawDefinitionsRoundTrip::RunTest(const FString& InParameters)
{
	bool bResult = true;
	URigMapperDefinition* HighToLowDef = nullptr;
	URigMapperDefinition* LowToHighDef = nullptr;
	UAnimSequence* TestAnimSequence = nullptr;
	FString TestAnimSequencePath;
	FString LowToHighDefPath;
	FString HighToLowDefPath;
	FString ControlSubStr;
	FString CurveIdentifier;

	const double Tolerance = 0.00001; 

	// load in the appropriate Rig Mapper Definitions for the case we are testing
	if (InParameters == "FN")
	{
		LowToHighDefPath = TEXT("/RigMapper/Definitions/Raw/RM_FNL_FNH.RM_FNL_FNH");
		HighToLowDefPath = TEXT("/RigMapper/Definitions/Raw/RM_FNH_FNL.RM_FNH_FNL");
		TestAnimSequencePath = TEXT("/Game/AutomationTestData/RigMapper/AnimSequences/Fortnite_Base_Head_ROM.Fortnite_Base_Head_ROM");
		ControlSubStr = TEXT("_pose");
		CurveIdentifier = TEXT("_pose");
	}
	else if (InParameters == "CD")
	{
		LowToHighDefPath = TEXT("/RigMapper/Definitions/Raw/RM_CDL_CDH.RM_CDL_CDH");
		HighToLowDefPath = TEXT("/RigMapper/Definitions/Raw/RM_CDH_CDL.RM_CDH_CDL");
		TestAnimSequencePath = TEXT("/Game/AutomationTestData/RigMapper/AnimSequences/FACIAL_3L_RIG_ROM.FACIAL_3L_RIG_ROM");
		ControlSubStr = TEXT("CTRL_");
		CurveIdentifier = TEXT("CTRL_expressions");
	}
	else if (InParameters == "MH")
	{
		LowToHighDefPath = TEXT("/RigMapper/Definitions/Raw/RM_MHL_MHH.RM_MHL_MHH");
		HighToLowDefPath = TEXT("/RigMapper/Definitions/Raw/RM_MHH_MHL.RM_MHH_MHL");
		TestAnimSequencePath = TEXT("/Game/AutomationTestData/RigMapper/AnimSequences/AS_Invictus_MH3.AS_Invictus_MH3");
		ControlSubStr = TEXT("CTRL_");
		CurveIdentifier = TEXT("CTRL_expressions");
	}
	else if (InParameters == "FNH_FNM")
	{
		LowToHighDefPath = TEXT("/RigMapper/Definitions/Raw/RM_FNL_FNH.RM_FNL_FNH");
		HighToLowDefPath = TEXT("/RigMapper/Definitions/Raw/RM_FNH_FNL.RM_FNH_FNL");
		TestAnimSequencePath = TEXT("/Game/AutomationTestData/RigMapper/AnimSequences/Fortnite_Base_Head_ROM.Fortnite_Base_Head_ROM");
		ControlSubStr = TEXT("_pose");
		CurveIdentifier = TEXT("_pose");
	}

	// load in the definitions and test sequence
	LowToHighDef = LoadObject<URigMapperDefinition>((UObject*)GetTransientPackage(), *LowToHighDefPath);
	if (!LowToHighDef)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to load asset from path: %ls", *LowToHighDefPath);
		return false;
	}

	HighToLowDef = LoadObject<URigMapperDefinition>((UObject*)GetTransientPackage(), *HighToLowDefPath);

	if (!HighToLowDef)
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to load asset from path: %ls", *HighToLowDefPath);
		return false;
	}

	TestAnimSequence = LoadObject<UAnimSequence>((UObject*)GetTransientPackage(), *TestAnimSequencePath);

	if (TestAnimSequence)
	{
		const FAnimationCurveData& CurveData = TestAnimSequence->GetDataModel()->GetCurveData();
		TArray<TMap<FString, double>> CurveDataRigMapperInputs = GetTestSequenceCurves(TestAnimSequence, ControlSubStr);

		// special cases for FN anim sequence; we need to make sure that opposing poses are not activated at the same time
		TArray< TPair<FString, FString>> OppositePoses = { { TEXT("L_frown_pose"), TEXT("L_smile_pose") }, { TEXT("R_frown_pose"), TEXT("R_smile_pose") }, 
			{ TEXT("R_lower_lip_up_pose"), TEXT("R_lower_lip_down_pose") }, { TEXT("L_lower_lip_up_pose"), TEXT("L_lower_lip_down_pose") },
			{ TEXT("R_upper_lip_lower_pose"), TEXT("R_upper_lip_raiser_pose") }, { TEXT("L_upper_lip_lower_pose"), TEXT("L_upper_lip_raiser_pose") } };
		if (InParameters == "FN" || InParameters == "FNH_FNM")
		{
			for (int32 Frame = 0; Frame < CurveDataRigMapperInputs.Num(); ++Frame)
			{
				for (int32 SpecialCase = 0; SpecialCase < OppositePoses.Num(); ++SpecialCase)
				{
					if (CurveDataRigMapperInputs[Frame].Find(OppositePoses[SpecialCase].Key) && CurveDataRigMapperInputs[Frame].Find(OppositePoses[SpecialCase].Value))
					{
						if (CurveDataRigMapperInputs[Frame][OppositePoses[SpecialCase].Key] > 0 && CurveDataRigMapperInputs[Frame][OppositePoses[SpecialCase].Value] > 0)
						{
							CurveDataRigMapperInputs[Frame][OppositePoses[SpecialCase].Key] = 0; // arbitrarily set the first of the two poses to 0 so no ambiguity
						}
					}
				}
			}
		}


		// run the round-trip test
		TArray<URigMapperDefinition*> RigMapperDefinitions;
		if (InParameters == "FNH_FNM")
		{
			// special case; we need to load in extra layers converting FNH_FNM and back as our anim sequence obviously only
			// uses raw curves
			FString FNH_FNMDefPath = TEXT("/RigMapper/Definitions/Raw/RM_FNH_FNM.RM_FNH_FNM");
			URigMapperDefinition* FNH_FNM = LoadObject<URigMapperDefinition>((UObject*)GetTransientPackage(), *FNH_FNMDefPath);
			if (!FNH_FNM)
			{
				UE_LOGF(LogRigMapperTest, Error, "Failed to load asset from path: %ls", *FNH_FNMDefPath);
				return false;
			}
			FString FNM_FNHDefPath = TEXT("/RigMapper/Definitions/Raw/RM_FNM_FNH.RM_FNM_FNH");
			URigMapperDefinition* FNM_FNH = LoadObject<URigMapperDefinition>((UObject*)GetTransientPackage(), *FNM_FNHDefPath);
			if (!FNM_FNH)
			{
				UE_LOGF(LogRigMapperTest, Error, "Failed to load asset from path: %ls", *FNM_FNHDefPath);
				return false;
			}
			RigMapperDefinitions = { LowToHighDef, FNH_FNM, FNM_FNH, HighToLowDef };
		}
		else
		{
			RigMapperDefinitions = { LowToHighDef, HighToLowDef };
		}
		TArray<TSharedPtr<FacialRigMapping::FRigMapper>> RigMappers;
		TArray<FRigMapperIndexMap> IndexMaps;

		bool bInitialized = InitializeRigMapping(RigMapperDefinitions, RigMappers, IndexMaps);
		if (!bInitialized)
		{
			UE_LOGF(LogRigMapperTest, Error, "Failed to initialize RigMapping");
			return false;
		}

		if (CurveDataRigMapperInputs.IsEmpty())
		{
			UE_LOGF(LogRigMapperTest, Error, "Expected test data to contain at least one frame");
			return false;
		}

		for (int32 Frame = 0; Frame < CurveDataRigMapperInputs.Num(); ++Frame)
		{
			// fill in any inputs missing in the anim sequence
			for (int32 Input = 0; Input < LowToHighDef->Inputs.Num(); ++Input)
			{
				if (!CurveDataRigMapperInputs[Frame].Find(LowToHighDef->Inputs[Input]))
				{
					CurveDataRigMapperInputs[Frame].Add(LowToHighDef->Inputs[Input], 0);
					if (Frame == 0)
					{
						UE_LOGF(LogRigMapperTest, Warning, "Missing value in test anim sequence for control: %ls , setting to 0", *LowToHighDef->Inputs[Input]);
					}
				}
			}
		}

		for (int32 Frame = 0; Frame < CurveDataRigMapperInputs.Num(); ++Frame)
		{
			// evaluate the rig mappers; this evaluates the whole thing in the same way in which this is done in the anim node
			TMap<FString, double> Outputs;
			EvaluateRigMapping(CurveDataRigMapperInputs[Frame], IndexMaps, RigMappers, Outputs);

			// check that the round trip gives outputs which are identical to the inputs
			if (!AreMapsEqualWithTolerance(CurveDataRigMapperInputs[Frame], Outputs, CurveIdentifier, {}, Tolerance))
			{
				UE_LOGF(LogRigMapperTest, Error, "Frame %d contains different values for LowToHigh->HighToLow round trip test", Frame);
				bResult = false;
			}
		}
	}
	else
	{
		// warn, but don't fail, as this test will only work in Beehive project
		UE_LOGF(LogRigMapperTest, Warning, "Failed to load test animation sequence from path: %ls . Note that this test data is only available in project Sandbox/Anim/Beehive", *TestAnimSequencePath);
	}

	return bResult;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FTestRigMapperCompareBakedVsUnbakedPluginDefinitions, "RigMapper.RigMapperCompareBakedVsUnbakedPluginDefinitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FTestRigMapperCompareBakedVsUnbakedPluginDefinitions::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add("Verify that baked definition RM_MHL_FNL gives the same result as the stack of individual unbaked definitions");
	OutTestCommands.Add("MHL_FNL");

	OutBeautifiedNames.Add("Verify that baked definition RM_MHL_CDL gives the same result as the stack of individual unbaked definitions");
	OutTestCommands.Add("MHL_CDL");

	OutBeautifiedNames.Add("Verify that baked definition RM_CDL_FNL gives the same result as the stack of individual unbaked definitions");
	OutTestCommands.Add("CDL_FNL");
}

bool FTestRigMapperCompareBakedVsUnbakedPluginDefinitions::RunTest(const FString& InParameters)
{
	bool bResult = true;
	UAnimSequence* TestAnimSequence = nullptr;
	TArray<FString> UnbakedDefinitionPaths;
	FString TestAnimSequencePath;
	FString BakedDefinitionPath;
	const FString ControlSubStr = TEXT("CTRL_");
	const FString CurveIdentifier = TEXT("CTRL_expressions");

	const double Tolerance = 0.00001;

	// load in the appropriate Rig Mapper Definitions for the case we are testing
	if (InParameters == "MHL_FNL")
	{
		BakedDefinitionPath = TEXT("/RigMapper/Definitions/Baked/RM_MHL_FNL.RM_MHL_FNL");
		UnbakedDefinitionPaths = { TEXT("/RigMapper/Definitions/Raw/RM_MHL_MHH.RM_MHL_MHH"),
			TEXT("/RigMapper/Definitions/Raw/RM_MHH_FNM.RM_MHH_FNM"),
			TEXT("/RigMapper/Definitions/Raw/RM_FNM_FNH.RM_FNM_FNH"),
			TEXT("/RigMapper/Definitions/Raw/RM_FNH_FNL.RM_FNH_FNL")
		};
		TestAnimSequencePath = TEXT("/Game/AutomationTestData/RigMapper/AnimSequences/AS_Invictus_MH3.AS_Invictus_MH3");
	}
	else if (InParameters == "MHL_CDL")
	{
		BakedDefinitionPath = TEXT("/RigMapper/Definitions/Baked/RM_MHL_CDL.RM_MHL_CDL");
		UnbakedDefinitionPaths = { TEXT("/RigMapper/Definitions/Raw/RM_MHL_MHH.RM_MHL_MHH"), 
			TEXT("/RigMapper/Definitions/Raw/RM_MHH_CDH.RM_MHH_CDH"),
			TEXT("/RigMapper/Definitions/Raw/RM_CDH_CDL.RM_CDH_CDL")
		};
		TestAnimSequencePath = TEXT("/Game/AutomationTestData/RigMapper/AnimSequences/AS_Invictus_MH3.AS_Invictus_MH3");
	}
	else if (InParameters == "CDL_FNL")
	{
		BakedDefinitionPath = TEXT("/RigMapper/Definitions/Baked/RM_CDL_FNL.RM_CDL_FNL");
		UnbakedDefinitionPaths = { TEXT("/RigMapper/Definitions/Raw/RM_CDL_CDH.RM_CDL_CDH"),
			TEXT("/RigMapper/Definitions/Raw/RM_CDH_FNM.RM_CDH_FNM"),
			TEXT("/RigMapper/Definitions/Raw/RM_FNM_FNL.RM_FNM_FNL"),
		};
		TestAnimSequencePath = TEXT("/Game/AutomationTestData/RigMapper/AnimSequences/FACIAL_3L_RIG_ROM.FACIAL_3L_RIG_ROM");
	}

	// load in the definitions and test sequence
	TArray<TSharedPtr<FacialRigMapping::FRigMapper>> RigMappersUnbaked, RigMappersBaked;
	TArray<URigMapperDefinition*> RigMapperDefinitionsUnbaked, RigMapperDefinitionsBaked;
	RigMapperDefinitionsUnbaked.SetNum(UnbakedDefinitionPaths.Num());
	RigMapperDefinitionsBaked.SetNum(1);
	TArray<FRigMapperIndexMap> IndexMapsUnbaked, IndexMapsBaked;

	RigMapperDefinitionsBaked[0] = LoadObject<URigMapperDefinition>((UObject*)GetTransientPackage(), *BakedDefinitionPath);
	if (!RigMapperDefinitionsBaked[0])
	{
		UE_LOGF(LogRigMapperTest, Error, "Failed to load asset from path: %ls", *BakedDefinitionPath);
		return false;
	}

	for (int32 Def = 0; Def < RigMapperDefinitionsUnbaked.Num(); ++Def)
	{
		RigMapperDefinitionsUnbaked[Def] = LoadObject<URigMapperDefinition>((UObject*)GetTransientPackage(), *UnbakedDefinitionPaths[Def]);
		if (!RigMapperDefinitionsUnbaked[Def])
		{
			UE_LOGF(LogRigMapperTest, Error, "Failed to load asset from path: %ls", *UnbakedDefinitionPaths[Def]);
			return false;
		}
	}

	TestAnimSequence = LoadObject<UAnimSequence>((UObject*)GetTransientPackage(), *TestAnimSequencePath);

	if (TestAnimSequence)
	{
		// run the comparison test
		const FAnimationCurveData& CurveData = TestAnimSequence->GetDataModel()->GetCurveData();
		TArray<TMap<FString, double>> CurveDataRigMapperInputs = GetTestSequenceCurves(TestAnimSequence, ControlSubStr);

		bool bInitialized = InitializeRigMapping(RigMapperDefinitionsUnbaked, RigMappersUnbaked, IndexMapsUnbaked);
		if (!bInitialized)
		{
			UE_LOGF(LogRigMapperTest, Error, "Failed to initialize unbaked RigMapping");
			return false;
		}

		bInitialized = InitializeRigMapping(RigMapperDefinitionsBaked, RigMappersBaked, IndexMapsBaked);
		if (!bInitialized)
		{
			UE_LOGF(LogRigMapperTest, Error, "Failed to initialize baked RigMapping");
			return false;
		}

		if (CurveDataRigMapperInputs.IsEmpty())
		{
			UE_LOGF(LogRigMapperTest, Error, "Expected test data to contain at least one frame");
			return false;
		}

		for (int32 Frame = 0; Frame < CurveDataRigMapperInputs.Num(); ++Frame)
		{
			// fill in any inputs missing in the anim sequence
			for (int32 Input = 0; Input < RigMapperDefinitionsBaked[0]->Inputs.Num(); ++Input)
			{
				if (!CurveDataRigMapperInputs[Frame].Find(RigMapperDefinitionsBaked[0]->Inputs[Input]))
				{
					CurveDataRigMapperInputs[Frame].Add(RigMapperDefinitionsBaked[0]->Inputs[Input], 0);
					if (Frame == 0)
					{
						UE_LOGF(LogRigMapperTest, Warning, "Missing value in test anim sequence for control: %ls , setting to 0", *RigMapperDefinitionsBaked[0]->Inputs[Input]);
					}
				}
			}
		}

		for (int32 Frame = 0; Frame < CurveDataRigMapperInputs.Num(); ++Frame)
		{
			// evaluate the rig mappers for both baked and unbaked cases; this evaluates the whole thing in the same way in which this is done in the anim node
			TMap<FString, double> OutputsBaked, OutputsUnbaked;
			EvaluateRigMapping(CurveDataRigMapperInputs[Frame], IndexMapsBaked, RigMappersBaked, OutputsBaked);
			EvaluateRigMapping(CurveDataRigMapperInputs[Frame], IndexMapsUnbaked, RigMappersUnbaked, OutputsUnbaked);

			// check that the round trip gives outputs which are identical to the inputs
			if (!AreMapsEqualWithTolerance(OutputsUnbaked, OutputsBaked, CurveIdentifier, {}, Tolerance))
			{
				UE_LOGF(LogRigMapperTest, Error, "Frame %d contains different values for unbaked vs. baked definition test", Frame);
				bResult = false;
			}
		}
	}
	else
	{
		// warn, but don't fail, as this test will only work in Beehive project
		UE_LOGF(LogRigMapperTest, Warning, "Failed to load test animation sequence from path: %ls . Note that this test data is only available in project Sandbox/Anim/Beehive", *TestAnimSequencePath);
	}

	return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigMapperValidationContextTest, "RigMapper.RigMapperValidationContext", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRigMapperValidationContextTest::RunTest(const FString& InParameters)
{
	FRigMapperValidationContext Context;

	// Empty state
	if (Context.HasErrors() || Context.HasWarnings())
	{
		UE_LOGF(LogRigMapperTest, Error, "Newly constructed context should be empty")
		return false;
	}
	if (Context.GetErrorCount() != 0 || Context.GetWarningCount() != 0)
	{
		UE_LOGF(LogRigMapperTest, Error, "Empty context counts should be 0")
		return false;
	}

	// Add warnings and errors
	Context.AddWarning(TEXT("Warning A"));
	Context.AddWarning(TEXT("Warning B"), 2);
	Context.AddError(TEXT("Error A"), 1);

	if (Context.GetWarningCount() != 2 || Context.GetErrorCount() != 1)
	{
		UE_LOGF(LogRigMapperTest, Error, "Expected 2 warnings and 1 error, got %d warnings and %d errors",
			Context.GetWarningCount(), Context.GetErrorCount())
		return false;
	}

	if (!Context.HasWarnings() || !Context.HasErrors())
	{
		UE_LOGF(LogRigMapperTest, Error, "HasWarnings/HasErrors should return true after adding messages")
		return false;
	}

	// Verify message contents and severity/index propagation
	if (Context.Messages.Num() != 3)
	{
		UE_LOGF(LogRigMapperTest, Error, "Expected 3 messages, got %d", Context.Messages.Num())
		return false;
	}

	if (Context.Messages[0].Severity != ERigMapperValidationSeverity::Warning ||
		Context.Messages[0].Message != TEXT("Warning A") ||
		Context.Messages[0].DefinitionIndex != INDEX_NONE)
	{
		UE_LOGF(LogRigMapperTest, Error, "Message[0] (Warning A) was not stored correctly")
		return false;
	}

	if (Context.Messages[1].DefinitionIndex != 2)
	{
		UE_LOGF(LogRigMapperTest, Error, "Message[1] should have DefinitionIndex=2, got %d", Context.Messages[1].DefinitionIndex)
		return false;
	}

	if (Context.Messages[2].Severity != ERigMapperValidationSeverity::Error ||
		Context.Messages[2].DefinitionIndex != 1)
	{
		UE_LOGF(LogRigMapperTest, Error, "Message[2] (Error A) was not stored correctly")
		return false;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRigMapperMathOpTest, "RigMapper.RigMapperMathOp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRigMapperMathOpTest::RunTest(const FString& InParameters)
{
	// Helper: builds a single Definition asset containing one MathOp feature with the given operation and inputs (node names or constants). 
	// The definition has three plain inputs "A", "B", "C" available for wiring. The MathOp produces "OutVal".
	auto MakeMathOpDefinition = [](ERigMapperMathOperation Op, const TArray<FRigMapperMathInput>& InMathInputs) -> URigMapperDefinition*
		{
			URigMapperDefinition* Def = NewObject<URigMapperDefinition>((UObject*)GetTransientPackage(), NAME_None, RF_Transient);
			if (!Def)
			{
				return nullptr;
			}
			Def->Inputs = { TEXT("A"), TEXT("B"), TEXT("C") };

			FRigMapperMathFeature Feature(TEXT("MathFeature"));
			Feature.Operation = Op;
			Feature.Inputs = InMathInputs;
			Def->Features.MathOps.Add(Feature);

			TMap<FString, FString> Outputs;
			Outputs.Add(TEXT("OutVal"), TEXT("MathFeature"));
			Def->Outputs = Outputs;
			Def->NullOutputs = {};
			return Def;
		};

	auto MakeNodeInput = [](const FString& NodeName) -> FRigMapperMathInput
		{
			FRigMapperMathInput In;
			In.NodeName = NodeName;
			In.ConstantValue = 0.0;
			return In;
		};

	auto MakeConstInput = [](double ConstantValue) -> FRigMapperMathInput
		{
			FRigMapperMathInput In;
			In.NodeName = FString();
			In.ConstantValue = ConstantValue;
			return In;
		};

	// Helper: runs the definition with given input values and returns the output value.
	// Returns false on init/evaluation failure.
	auto EvaluateMathOp = [this](URigMapperDefinition* InDef, const TMap<FString, double>& InInputs, double& OutValue) -> bool
		{
			TArray<URigMapperDefinition*> Definitions = { InDef };
			TArray<TSharedPtr<FacialRigMapping::FRigMapper>> RigMappers;
			TArray<FRigMapperIndexMap> IndexMaps;
			if (!InitializeRigMapping(Definitions, RigMappers, IndexMaps))
			{
				return false;
			}
			TMap<FString, double> Outputs;
			EvaluateRigMapping(InInputs, IndexMaps, RigMappers, Outputs);
			const double* Found = Outputs.Find(TEXT("OutVal"));
			if (!Found)
			{
				return false;
			}
			OutValue = *Found;
			return true;
		};

	struct FCase
	{
		const TCHAR* Name;
		ERigMapperMathOperation Op;
		TArray<FRigMapperMathInput> Inputs;
		TMap<FString, double> InputValues;
		double Expected;
	};

	// Cases for each of the ops. Inputs use node names where possible to exercise the node lookup path;
	// constant inputs are used selectively where they make the test clearer (Clamp/Lerp ranges).
	const TArray<FCase> Cases = {
		// --- Unary ops ---
		{ TEXT("Abs(-0.75)"),       ERigMapperMathOperation::Abs,      { MakeNodeInput(TEXT("A")) },                                          { { TEXT("A"), -0.75 } },                                  0.75 },
		{ TEXT("Negate(0.4)"),      ERigMapperMathOperation::Negate,   { MakeNodeInput(TEXT("A")) },                                          { { TEXT("A"), 0.4 } },                                   -0.4 },
		{ TEXT("Floor(2.7)"),       ERigMapperMathOperation::Floor,    { MakeNodeInput(TEXT("A")) },                                          { { TEXT("A"), 2.7 } },                                    2.0 },
		{ TEXT("Ceil(2.1)"),        ERigMapperMathOperation::Ceil,     { MakeNodeInput(TEXT("A")) },                                          { { TEXT("A"), 2.1 } },                                    3.0 },
		{ TEXT("Round(2.6)"),       ERigMapperMathOperation::Round,    { MakeNodeInput(TEXT("A")) },                                          { { TEXT("A"), 2.6 } },                                    3.0 },

		// --- Binary/N-ary ---
		{ TEXT("Min(0.4, 0.7, 0.2)"),    ERigMapperMathOperation::Min,      { MakeNodeInput(TEXT("A")), MakeNodeInput(TEXT("B")), MakeNodeInput(TEXT("C")) }, { { TEXT("A"), 0.4 }, { TEXT("B"), 0.7 }, { TEXT("C"), 0.2 } }, 0.2 },
		{ TEXT("Max(0.4, 0.7, 0.2)"),    ERigMapperMathOperation::Max,      { MakeNodeInput(TEXT("A")), MakeNodeInput(TEXT("B")), MakeNodeInput(TEXT("C")) }, { { TEXT("A"), 0.4 }, { TEXT("B"), 0.7 }, { TEXT("C"), 0.2 } }, 0.7 },
		{ TEXT("Sum(0.1, 0.2, 0.3)"),    ERigMapperMathOperation::Sum,      { MakeNodeInput(TEXT("A")), MakeNodeInput(TEXT("B")), MakeNodeInput(TEXT("C")) }, { { TEXT("A"), 0.1 }, { TEXT("B"), 0.2 }, { TEXT("C"), 0.3 } }, 0.6 },
		{ TEXT("Average(0.2, 0.4, 0.6)"), ERigMapperMathOperation::Average,  { MakeNodeInput(TEXT("A")), MakeNodeInput(TEXT("B")), MakeNodeInput(TEXT("C")) }, { { TEXT("A"), 0.2 }, { TEXT("B"), 0.4 }, { TEXT("C"), 0.6 } }, 0.4 },
		{ TEXT("Multiply(0.5, 0.4)"),     ERigMapperMathOperation::Multiply, { MakeNodeInput(TEXT("A")), MakeNodeInput(TEXT("B")) },                                  { { TEXT("A"), 0.5 }, { TEXT("B"), 0.4 } },                          0.2 },
		{ TEXT("Divide(0.6, 0.2)"),       ERigMapperMathOperation::Divide,   { MakeNodeInput(TEXT("A")), MakeNodeInput(TEXT("B")) },                                  { { TEXT("A"), 0.6 }, { TEXT("B"), 0.2 } },                          3.0 },

		// --- Ternary ---
		// Clamp(input=1.2, min=0.0, max=1.0) -> 1.0
		{ TEXT("Clamp(1.2, [0, 1])"),     ERigMapperMathOperation::Clamp,    { MakeNodeInput(TEXT("A")), MakeConstInput(0.0), MakeConstInput(1.0) },                  { { TEXT("A"), 1.2 } },                                              1.0 },
		// Clamp(input=-0.3, min=0.0, max=1.0) -> 0.0
		{ TEXT("Clamp(-0.3, [0, 1])"),    ERigMapperMathOperation::Clamp,    { MakeNodeInput(TEXT("A")), MakeConstInput(0.0), MakeConstInput(1.0) },                  { { TEXT("A"), -0.3 } },                                             0.0 },
		// Clamp(input=0.5, min=0.0, max=1.0) -> 0.5 (passthrough)
		{ TEXT("Clamp(0.5, [0, 1])"),     ERigMapperMathOperation::Clamp,    { MakeNodeInput(TEXT("A")), MakeConstInput(0.0), MakeConstInput(1.0) },                  { { TEXT("A"), 0.5 } },                                              0.5 },
		// Lerp(A=0.0, B=1.0, Alpha=0.25) -> 0.25
		{ TEXT("Lerp(0, 1, 0.25)"),       ERigMapperMathOperation::Lerp,     { MakeNodeInput(TEXT("A")), MakeNodeInput(TEXT("B")), MakeNodeInput(TEXT("C")) },        { { TEXT("A"), 0.0 }, { TEXT("B"), 1.0 }, { TEXT("C"), 0.25 } },     0.25 },

		// --- Edge cases ---
		// Divide by zero: yields 0.0 per implementation
		{ TEXT("Divide(1.0, 0.0) zero divisor"), ERigMapperMathOperation::Divide, { MakeNodeInput(TEXT("A")), MakeNodeInput(TEXT("B")) }, { { TEXT("A"), 1.0 }, { TEXT("B"), 0.0 } }, 0.0 },
		// Multiply with a zero early exits at zero
		{ TEXT("Multiply(0.0, 0.5, 0.9) short circuit"), ERigMapperMathOperation::Multiply, { MakeNodeInput(TEXT("A")), MakeNodeInput(TEXT("B")), MakeNodeInput(TEXT("C")) }, { { TEXT("A"), 0.0 }, { TEXT("B"), 0.5 }, { TEXT("C"), 0.9 } }, 0.0 },
		// Pure constant inputs (no node names), exercises the constant value path
		{ TEXT("Sum(constants only)"),     ERigMapperMathOperation::Sum,      { MakeConstInput(0.1), MakeConstInput(0.2), MakeConstInput(0.3) },                       { /* no node inputs needed */ },                                      0.6 },
	};

	for (const FCase& Case : Cases)
	{
		URigMapperDefinition* Def = MakeMathOpDefinition(Case.Op, Case.Inputs);
		if (!Def)
		{
			UE_LOGF(LogRigMapperTest, Error, "Failed to create definition for case '%ls'", Case.Name)
			return false;
		}

		double OutValue = 0.0;
		if (!EvaluateMathOp(Def, Case.InputValues, OutValue))
		{
			UE_LOGF(LogRigMapperTest, Error, "Evaluation failed for case '%ls'", Case.Name)
			return false;
		}

		if (!FMath::IsNearlyEqual(OutValue, Case.Expected, SMALL_NUMBER))
		{
			UE_LOGF(LogRigMapperTest, Error, "Case '%ls' expected %f, got %f", Case.Name, Case.Expected, OutValue)
			return false;
		}
	}

	// --- Failure cases that should NOT evaluate successfully ---

	// --- Validation: IsValid should reject MathOp with too few inputs ---
	{
		URigMapperDefinition* Def = MakeMathOpDefinition(
			ERigMapperMathOperation::Lerp,
			{ MakeNodeInput(TEXT("A")), MakeNodeInput(TEXT("B")) });  // Lerp requires 3

		FRigMapperValidationContext Context;
		const bool bIsValid = Def->IsDefinitionValid(&Context, /*bWarn=*/false, /*bForce=*/true);
		if (bIsValid)
		{
			UE_LOGF(LogRigMapperTest, Error, "Definition with Lerp having only 2 inputs should not be valid")
			return false;
		}
		if (!Context.HasErrors())
		{
			UE_LOGF(LogRigMapperTest, Error, "Lerp with too few inputs should populate errors in the validation context")
			return false;
		}
	}
	// MathOp too many inputs: Abs is unary, giving it 2 inputs should warn but stay valid
	{
		URigMapperDefinition* Def = MakeMathOpDefinition(
			ERigMapperMathOperation::Abs,
			{ MakeNodeInput(TEXT("A")), MakeNodeInput(TEXT("B")) });

		FRigMapperValidationContext Context;
		const bool bIsValid = Def->IsDefinitionValid(&Context, /*bWarn=*/false, /*bForce=*/true);
		if (!bIsValid)
		{
			UE_LOGF(LogRigMapperTest, Error, "Abs with 2 inputs should stay valid (warning, not error)")
				return false;
		}
		if (!Context.HasWarnings() || Context.HasErrors())
		{
			UE_LOGF(LogRigMapperTest, Error, "Abs with 2 inputs should produce exactly a warning (no errors)")
				return false;
		}
	}

	return true;
}

#endif