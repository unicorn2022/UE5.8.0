// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderStatsModel.h"
#include "ShaderAuditSession.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

/** Sort FNames by size descending, count descending, then lexical. Uses flat TMap lookups. */
static void SortBySizeAndCount(TArray<FName>& Names, const TMap<FName, uint64>& SizeMap, const TMap<FName, int32>& CountMap)
{
	Names.Sort([&SizeMap, &CountMap](const FName& A, const FName& B)
	{
		const uint64 SizeA = SizeMap.FindRef(A);
		const uint64 SizeB = SizeMap.FindRef(B);
		if (SizeA != SizeB)
		{
			return SizeA > SizeB;
		}
		const int32 CountA = CountMap.FindRef(A);
		const int32 CountB = CountMap.FindRef(B);
		if (CountA != CountB)
		{
			return CountA > CountB;
		}
		return FNameLexicalLess()(A, B);
	});
}

/** Sort FNames by size descending, count descending, then lexical. Sums across inner maps for cross-tab models. */
static void SortBySizeAndCount(TArray<FName>& Names, const TMap<FName, TMap<FName, uint64>>& SizeMap, const TMap<FName, TMap<FName, int32>>& CountMap)
{
	Names.Sort([&SizeMap, &CountMap](const FName& A, const FName& B)
	{
		auto SumSizes = [&SizeMap](const FName& Key) -> uint64
		{
			uint64 Total = 0;
			if (const auto* Inner = SizeMap.Find(Key)) { for (const auto& P : *Inner) { Total += P.Value; } }
			return Total;
		};
		auto SumCounts = [&CountMap](const FName& Key) -> int32
		{
			int32 Total = 0;
			if (const auto* Inner = CountMap.Find(Key)) { for (const auto& P : *Inner) { Total += P.Value; } }
			return Total;
		};
		const uint64 SizeA = SumSizes(A);
		const uint64 SizeB = SumSizes(B);
		if (SizeA != SizeB)
		{
			return SizeA > SizeB;
		}
		const int32 CountA = SumCounts(A);
		const int32 CountB = SumCounts(B);
		if (CountA != CountB)
		{
			return CountA > CountB;
		}
		return FNameLexicalLess()(A, B);
	});
}

/** Format a count with optional size: "1,234" or "1,234 (56.2 MB)" */
static FText FormatCountAndSize(int32 Count, uint64 SizeBytes)
{
	if (SizeBytes == 0)
	{
		return FText::AsNumber(Count);
	}

	FString SizeStr;
	if (SizeBytes >= 1024 * 1024 * 1024)
	{
		SizeStr = FString::Printf(TEXT("%.1f GB"), SizeBytes / (1024.0 * 1024.0 * 1024.0));
	}
	else if (SizeBytes >= 1024 * 1024)
	{
		SizeStr = FString::Printf(TEXT("%.1f MB"), SizeBytes / (1024.0 * 1024.0));
	}
	else if (SizeBytes >= 1024)
	{
		SizeStr = FString::Printf(TEXT("%.1f KB"), SizeBytes / 1024.0);
	}
	else
	{
		SizeStr = FString::Printf(TEXT("%llu B"), SizeBytes);
	}

	return FText::FromString(FString::Printf(TEXT("%s (%s)"),
		*FText::AsNumber(Count).ToString(), *SizeStr));
}


/** Check if session has bytecode size data (from BytecodeDatabase). */
static bool HasSizeData(const FShaderAuditSession& Session)
{
	for (int32 i = 0; i < Session.UniqueShaders.Num(); ++i)
	{
		if (Session.UniqueShaders[i].CompressedSize > 0)
		{
			return true;
		}
	}
	return false;
}

// ============================================================================
// FMaterialDomainByShaderFrequencyModel
// ============================================================================

FMaterialDomainByShaderFrequencyModel::FMaterialDomainByShaderFrequencyModel(TSharedPtr<FShaderAuditSession> InSession)
	: Session(InSession)
{
	ShaderFrequencies.Add(TEXT("MaterialDomain"));

	if (Session.IsValid())
	{
		bool bHasSize = HasSizeData(*Session);
		for (int32 i = 0; i < Session->UniqueShaders.Num(); ++i)
		{
			const FStableShaderKeyAndValue& Entry = Session->GetShaderEntry(i);
			Counts.FindOrAdd(Entry.MaterialDomain).FindOrAdd(Entry.TargetFrequency)++;
			if (bHasSize)
			{
				Sizes.FindOrAdd(Entry.MaterialDomain).FindOrAdd(Entry.TargetFrequency) += Session->UniqueShaders[i].CompressedSize;
			}
			MaterialDomains.AddUnique(Entry.MaterialDomain);
			ShaderFrequencies.AddUnique(Entry.TargetFrequency);
		}

		SortBySizeAndCount(MaterialDomains, Sizes, Counts);

		// Sort frequency columns alphabetically (skip label column at index 0)
		Algo::Sort(TArrayView<FName>(ShaderFrequencies.GetData() + 1, ShaderFrequencies.Num() - 1), FNameLexicalLess());
	}
}

TConstArrayView<FName> FMaterialDomainByShaderFrequencyModel::GetRows() const
{
	return MaterialDomains;
}

TConstArrayView<FName> FMaterialDomainByShaderFrequencyModel::GetColumns() const
{
	return ShaderFrequencies;
}

TSharedRef<SWidget> FMaterialDomainByShaderFrequencyModel::GetCellWidget(FName Row, FName Column) const
{
	if (!Session.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	if (Column == TEXT("MaterialDomain"))
	{
		return SNew(STextBlock).Text(FText::FromName(Row));
	}

	FText CellText;
	if (const TMap<FName, int32>* FreqMap = Counts.Find(Row))
	{
		if (const int32* Count = FreqMap->Find(Column))
		{
			uint64 Size = 0;
			if (const TMap<FName, uint64>* SizeMap = Sizes.Find(Row))
			{
				if (const uint64* S = SizeMap->Find(Column))
				{
					Size = *S;
				}
			}
			CellText = FormatCountAndSize(*Count, Size);
		}
		else
		{
			CellText = FText::FromString(TEXT("-"));
		}
	}
	else
	{
		CellText = FText::FromString(TEXT("-"));
	}

	return SNew(STextBlock).Text(CellText);
}

// ============================================================================
// FVertexFactoryByShaderFrequencyModel
// ============================================================================

FVertexFactoryByShaderFrequencyModel::FVertexFactoryByShaderFrequencyModel(TSharedPtr<FShaderAuditSession> InSession)
	: Session(InSession)
{
	ShaderFrequencies.Add(TEXT("VertexFactory"));

	if (Session.IsValid())
	{
		bool bHasSize = HasSizeData(*Session);
		for (int32 i = 0; i < Session->UniqueShaders.Num(); ++i)
		{
			const FStableShaderKeyAndValue& Entry = Session->GetShaderEntry(i);
			Counts.FindOrAdd(Entry.VFType).FindOrAdd(Entry.TargetFrequency)++;
			if (bHasSize)
			{
				Sizes.FindOrAdd(Entry.VFType).FindOrAdd(Entry.TargetFrequency) += Session->UniqueShaders[i].CompressedSize;
			}
			VertexFactories.AddUnique(Entry.VFType);
			ShaderFrequencies.AddUnique(Entry.TargetFrequency);
		}

		SortBySizeAndCount(VertexFactories, Sizes, Counts);

		// Sort frequency columns alphabetically (skip label column at index 0)
		Algo::Sort(TArrayView<FName>(ShaderFrequencies.GetData() + 1, ShaderFrequencies.Num() - 1), FNameLexicalLess());
	}
}

TConstArrayView<FName> FVertexFactoryByShaderFrequencyModel::GetRows() const
{
	return VertexFactories;
}

TConstArrayView<FName> FVertexFactoryByShaderFrequencyModel::GetColumns() const
{
	return ShaderFrequencies;
}

TSharedRef<SWidget> FVertexFactoryByShaderFrequencyModel::GetCellWidget(FName Row, FName Column) const
{
	if (!Session.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	if (Column == TEXT("VertexFactory"))
	{
		return SNew(STextBlock).Text(FText::FromName(Row));
	}

	FText CellText;

	if (const TMap<FName, int32>* FreqMap = Counts.Find(Row))
	{
		if (const int32* Count = FreqMap->Find(Column))
		{
			uint64 Size = 0;
			if (const TMap<FName, uint64>* SizeMap = Sizes.Find(Row))
			{
				if (const uint64* S = SizeMap->Find(Column))
				{
					Size = *S;
				}
			}
			CellText = FormatCountAndSize(*Count, Size);
		}
		else
		{
			CellText = FText::FromString(TEXT("-"));
		}
	}
	else
	{
		CellText = FText::FromString(TEXT("-"));
	}

	return SNew(STextBlock).Text(CellText);
}

// ============================================================================
// FVertexFactoryByShaderTypeModel
// ============================================================================

FVertexFactoryByShaderTypeModel::FVertexFactoryByShaderTypeModel(TSharedPtr<FShaderAuditSession> InSession)
	: Session(InSession)
{
	Columns.Add(FName(TEXT("VF / ShaderType")));
	Columns.Add(FName(TEXT("Count")));

	if (Session.IsValid())
	{
		bool bHasSize = HasSizeData(*Session);
		for (int32 i = 0; i < Session->UniqueShaders.Num(); ++i)
		{
			const FStableShaderKeyAndValue& Entry = Session->GetShaderEntry(i);
			FName ComboName(*FString::Printf(TEXT("%s / %s"), *Entry.VFType.ToString(), *Entry.ShaderType.ToString()));
			CountPerCombo.FindOrAdd(ComboName)++;
			if (bHasSize)
			{
				SizePerCombo.FindOrAdd(ComboName) += Session->UniqueShaders[i].CompressedSize;
			}
		}

		CountPerCombo.GetKeys(ComboList);

		SortBySizeAndCount(ComboList, SizePerCombo, CountPerCombo);
	}
}

TConstArrayView<FName> FVertexFactoryByShaderTypeModel::GetRows() const
{
	return ComboList;
}

TConstArrayView<FName> FVertexFactoryByShaderTypeModel::GetColumns() const
{
	return Columns;
}

TSharedRef<SWidget> FVertexFactoryByShaderTypeModel::GetCellWidget(FName Row, FName Column) const
{
	if (!Session.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	if (Column == TEXT("VF / ShaderType"))
	{
		return SNew(STextBlock).Text(FText::FromName(Row));
	}
	else if (Column == TEXT("Count"))
	{
		const int32 Count = CountPerCombo.FindRef(Row);
		const uint64 Size = SizePerCombo.FindRef(Row);
		return SNew(STextBlock).Text(FormatCountAndSize(Count, Size));
	}

	return SNew(STextBlock).Text(FText::GetEmpty());
}

// ============================================================================
// FMaterialStatModel
// ============================================================================

FMaterialStatModel::FMaterialStatModel(TSharedPtr<FShaderAuditSession> InSession)
	: Session(InSession)
{
	Columns.Add(FName(TEXT("Material")));
	Columns.Add(FName(TEXT("Total Shaders")));

	if (Session.IsValid())
	{
		bool bHasSize = HasSizeData(*Session);
		for (int32 i = 0; i < Session->UniqueMaterials.Num(); ++i)
		{
			const FShaderAuditSession::FUniqueMaterial& Mat = Session->UniqueMaterials[i];
			// Use the full path as the map key (unique), leaf name as the display label
			FString LeafName;
			if (!Mat.Path.Split(TEXT("/"), nullptr, &LeafName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
			{
				LeafName = Mat.Path;
			}
			FName MatKey(*Mat.Path);
			FName MatLabel(*LeafName);
			MaterialList.Add(MatKey);
			MaterialDisplayNames.Add(MatKey, MatLabel);

			TSet<FShaderHash> AllHashes;
			for (int32 ShaderIdx : Mat.ShaderIndices)
			{
				const FStableShaderKeyAndValue& StableShaderKeyAndValue = Session->StableShaderKeyAndValueArray[ShaderIdx];
				AllHashes.Add(StableShaderKeyAndValue.OutputHash);
			}
			ShaderCountPerMaterial.Add(MatKey, AllHashes.Num());

			if (bHasSize)
			{
				float MatSize = 0;
				for (const FShaderHash& ShaderHash : AllHashes)
				{
					int32* ShaderIdx = Session->ShaderHashToIndex.Find(ShaderHash);
					if (ShaderIdx)
					{
						int32 MatRefCount = Session->UniqueShaders[*ShaderIdx].MaterialIndices.Num();
						uint32 ShaderSize = Session->UniqueShaders[*ShaderIdx].CompressedSize;
						MatSize += float(ShaderSize) / MatRefCount;
					}
				}
				SizePerMaterial.Add(MatKey, (uint64)MatSize);
			}
		}

		SortBySizeAndCount(MaterialList, SizePerMaterial, ShaderCountPerMaterial);
	}
}

TConstArrayView<FName> FMaterialStatModel::GetRows() const
{
	return MaterialList;
}

TConstArrayView<FName> FMaterialStatModel::GetColumns() const
{
	return Columns;
}

TSharedRef<SWidget> FMaterialStatModel::GetCellWidget(FName Row, FName Column) const
{
	if (!Session.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	if (Column == TEXT("Material"))
	{
		const FName* DisplayName = MaterialDisplayNames.Find(Row);
		return SNew(STextBlock).Text(FText::FromName(DisplayName ? *DisplayName : Row));
	}
	else if (Column == TEXT("Total Shaders"))
	{
		const int32 Count = ShaderCountPerMaterial.FindRef(Row);
		const uint64 Size = SizePerMaterial.FindRef(Row);
		return SNew(STextBlock).Text(FormatCountAndSize(Count, Size));
	}

	return SNew(STextBlock).Text(FText::GetEmpty());
}
