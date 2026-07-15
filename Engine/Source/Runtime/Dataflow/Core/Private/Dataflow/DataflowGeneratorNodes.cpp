// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowGeneratorNodes.h"
#include "Dataflow/DataflowCore.h"

#include "Math/RandomStream.h"
#include "Dataflow/DataflowUtils.h"

namespace UE::Dataflow
{
	void RegisterGeneratorNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowRandomGeneratorNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowLinearSequenceGeneratorNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowPerlinNoiseGeneratorNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowSimpleRegexGeneratorNode);
	}
}

// --------------------------------------------------------------------------------------------------------------------

void FDataflowValueGenerator::GenerateValues(TArray<double>& InOutValues) const
{
	if (Generator)
	{
		Generator->GenerateValues(InOutValues);
	}
}

// --------------------------------------------------------------------------------------------------------------------

struct FDataflowRandomGenerator : public IDataflowValueGenerator
{
	int32 Seed = 0;
	double RandomMin = 0.0;
	double RandomMax = 1.0;
	bool bReverseOrder = false;

	virtual void GenerateValues(TArray<double>& InOutValues) const override;
};

void FDataflowRandomGenerator::GenerateValues(TArray<double>& InOutValues) const
{
	FRandomStream Stream(Seed);
	for (double& Value : InOutValues)
	{
		Value = Stream.FRandRange(RandomMin, RandomMax);
	}

	if (bReverseOrder)
	{
		Algo::Reverse(InOutValues);
	}
}

// --------------------------------------------------------------------------------------------------------------------

FDataflowRandomGeneratorNode::FDataflowRandomGeneratorNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Seed);
	RegisterInputConnection(&RandomMin);
	RegisterInputConnection(&RandomMax);
	RegisterOutputConnection(&Generator);

	RandomMin.Value = 0.0;
	RandomMax.Value = 1.0;
}

void FDataflowRandomGeneratorNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Generator))
	{
		TSharedPtr<FDataflowRandomGenerator> GeneratorPtr = MakeShared<FDataflowRandomGenerator>();

		GeneratorPtr->Seed = GetValue(Context, &Seed);
		GeneratorPtr->RandomMin = GetValue(Context, &RandomMin);
		GeneratorPtr->RandomMax = GetValue(Context, &RandomMax);
		GeneratorPtr->bReverseOrder = bReverseOrder;

		SetValue(Context, FDataflowValueGenerator(GeneratorPtr), &Generator);
	}
}

// --------------------------------------------------------------------------------------------------------------------

struct FDataflowLinearSequenceGenerator : public IDataflowValueGenerator
{
	double Start = 0.0;
	double Increment = 1.0;
	bool bReverseOrder = false;

	virtual void GenerateValues(TArray<double>& InOutValues) const override;
};

void FDataflowLinearSequenceGenerator::GenerateValues(TArray<double>& InOutValues) const
{
	double Val = Start;

	for (double& Value : InOutValues)
	{
		Value = Val;
		Val += Increment;
	}

	if (bReverseOrder)
	{
		Algo::Reverse(InOutValues);
	}
}

FDataflowLinearSequenceGeneratorNode::FDataflowLinearSequenceGeneratorNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Start);
	RegisterInputConnection(&Increment);
	RegisterOutputConnection(&Generator);

	Start.Value = 0.0;
	Increment.Value = 1.0;
}

void FDataflowLinearSequenceGeneratorNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Generator))
	{
		TSharedPtr<FDataflowLinearSequenceGenerator> GeneratorPtr = MakeShared<FDataflowLinearSequenceGenerator>();

		GeneratorPtr->Start = GetValue(Context, &Start);
		GeneratorPtr->Increment = GetValue(Context, &Increment);
		GeneratorPtr->bReverseOrder = bReverseOrder;

		SetValue(Context, FDataflowValueGenerator(GeneratorPtr), &Generator);
	}
}

// --------------------------------------------------------------------------------------------------------------------

struct FDataflowPerlinNoiseGenerator : public IDataflowValueGenerator
{
	float Frequency = 1.0;
	float Offset = 0.0;
	float NoiseMin = -1.0;
	float NoiseMax = 1.0;
	bool bClamp = false;
	bool bReverseOrder = false;

	virtual void GenerateValues(TArray<double>& InOutValues) const override;
};

static float Fit(float InMin, float InMax, float InNewMin, float InNewMax, float InValue)
{
	if (InMax - InMin > SMALL_NUMBER)
	{
		return (InValue - InMin) / (InMax - InMin) * (InNewMax - InNewMin) + InNewMin;
	}

	return InValue;
}

void FDataflowPerlinNoiseGenerator::GenerateValues(TArray<double>& InOutValues) const
{
	int32 Idx = 0;
	for (double& Value : InOutValues)
	{
		float PerlinNoiseVal = FMath::PerlinNoise1D(0.1234 * ((float)Idx * Frequency + Offset));
		// Original range for Perlin [-1.0, 1.0]
		// Lets remap it to [NoiseMin, NoiseMax]
		Value = Fit(-1.f, 1.f, NoiseMin, NoiseMax, PerlinNoiseVal);
		
		if (bClamp)
		{
			Value = FMath::Clamp(Value, NoiseMin, NoiseMax);
		}

		Idx++;
	}

	if (bReverseOrder)
	{
		Algo::Reverse(InOutValues);
	}
}

FDataflowPerlinNoiseGeneratorNode::FDataflowPerlinNoiseGeneratorNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Frequency);
	RegisterInputConnection(&Offset);
	RegisterInputConnection(&NoiseMin);
	RegisterInputConnection(&NoiseMax);
	RegisterOutputConnection(&Generator);
}

void FDataflowPerlinNoiseGeneratorNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Generator))
	{
		TSharedPtr<FDataflowPerlinNoiseGenerator> GeneratorPtr = MakeShared<FDataflowPerlinNoiseGenerator>();

		GeneratorPtr->Frequency = GetValue(Context, &Frequency);
		GeneratorPtr->Offset = GetValue(Context, &Offset);

		float InNoiseMin = FMath::Min(GetValue(Context, &NoiseMin), GetValue(Context, &NoiseMax));
		float InNoiseMax = FMath::Max(GetValue(Context, &NoiseMin), GetValue(Context, &NoiseMax));

		GeneratorPtr->NoiseMin = InNoiseMin;
		GeneratorPtr->NoiseMax = InNoiseMax;
		GeneratorPtr->bClamp = bClamp;
		GeneratorPtr->bReverseOrder = bReverseOrder;

		SetValue(Context, FDataflowValueGenerator(GeneratorPtr), &Generator);
	}
}

// --------------------------------------------------------------------------------------------------------------------

struct FDataflowSimpleRegexGenerator : public IDataflowValueGenerator
{
	FString List;
	bool bReverseOrder = false;

	virtual void GenerateValues(TArray<double>& InOutValues) const override;

	virtual ~FDataflowSimpleRegexGenerator() = default;
};

void FDataflowSimpleRegexGenerator::GenerateValues(TArray<double>& InOutValues) const
{
	TArray<int32> OutIntArray;

	using namespace UE::Dataflow::Utils;

	EErrorCode ErrorCode = ParseIndicesStr(List, OutIntArray);

	if (ErrorCode == EErrorCode::None)
	{
		if (InOutValues.Num() == OutIntArray.Num())
		{
			int32 Idx = 0;
			for (double& Value : InOutValues)
			{
				if (Idx < OutIntArray.Num())
				{
					Value = (double)OutIntArray[Idx++];
				}
			}
		}
	}

	if (bReverseOrder)
	{
		Algo::Reverse(InOutValues);
	}
}

FDataflowSimpleRegexGeneratorNode::FDataflowSimpleRegexGeneratorNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterOutputConnection(&Generator);
}

void FDataflowSimpleRegexGeneratorNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Generator))
	{
		TSharedPtr<FDataflowSimpleRegexGenerator> GeneratorPtr = MakeShared<FDataflowSimpleRegexGenerator>();

		GeneratorPtr->List = List;
		GeneratorPtr->bReverseOrder = bReverseOrder;

		SetValue(Context, FDataflowValueGenerator(GeneratorPtr), &Generator);
	}
}

// --------------------------------------------------------------------------------------------------------------------







