// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FShaderAuditSession;
class SWidget;

class IShaderTableModel
{
public:
	virtual ~IShaderTableModel() {}

	virtual TConstArrayView<FName> GetRows() const = 0;
	virtual TConstArrayView<FName> GetColumns() const = 0;
	virtual TSharedRef<SWidget> GetCellWidget(FName Row, FName Column) const = 0;
};


class FMaterialDomainByShaderFrequencyModel : public IShaderTableModel
{
public:
	FMaterialDomainByShaderFrequencyModel(TSharedPtr<FShaderAuditSession> InSession);

	TConstArrayView<FName> GetRows() const override;
	TConstArrayView<FName> GetColumns() const override;
	TSharedRef<SWidget> GetCellWidget(FName Row, FName Column) const override;

private:
	TSharedPtr<FShaderAuditSession> Session;
	TArray<FName> MaterialDomains;
	TArray<FName> ShaderFrequencies;
	TMap<FName, TMap<FName, int32>> Counts;   // [Domain][Frequency] -> count
	TMap<FName, TMap<FName, uint64>> Sizes;   // [Domain][Frequency] -> bytes
};

class FVertexFactoryByShaderFrequencyModel : public IShaderTableModel
{
public:
	FVertexFactoryByShaderFrequencyModel(TSharedPtr<FShaderAuditSession> InSession);

	TConstArrayView<FName> GetRows() const override;
	TConstArrayView<FName> GetColumns() const override;
	TSharedRef<SWidget> GetCellWidget(FName Row, FName Column) const override;

private:
	TSharedPtr<FShaderAuditSession> Session;
	TArray<FName> VertexFactories;
	TArray<FName> ShaderFrequencies;
	TMap<FName, TMap<FName, int32>> Counts;   // [VFType][Frequency] -> count
	TMap<FName, TMap<FName, uint64>> Sizes;   // [VFType][Frequency] -> bytes
};

class FVertexFactoryByShaderTypeModel : public IShaderTableModel
{
public:
	FVertexFactoryByShaderTypeModel(TSharedPtr<FShaderAuditSession> InSession);

	TConstArrayView<FName> GetRows() const override;
	TConstArrayView<FName> GetColumns() const override;
	TSharedRef<SWidget> GetCellWidget(FName Row, FName Column) const override;

private:
	TSharedPtr<FShaderAuditSession> Session;
	TArray<FName> ComboList;  // "VFType / ShaderType" combined names
	TArray<FName> Columns;
	TMap<FName, int32> CountPerCombo;
	TMap<FName, uint64> SizePerCombo;
};

class FMaterialStatModel : public IShaderTableModel
{
public:
	FMaterialStatModel(TSharedPtr<FShaderAuditSession> InSession);

	TConstArrayView<FName> GetRows() const override;
	TConstArrayView<FName> GetColumns() const override;
	TSharedRef<SWidget> GetCellWidget(FName Row, FName Column) const override;

private:
	TSharedPtr<FShaderAuditSession> Session;
	TArray<FName> MaterialList;
	TArray<FName> Columns;
	TMap<FName, int32> ShaderCountPerMaterial;
	TMap<FName, uint64> SizePerMaterial;
	TMap<FName, FName> MaterialDisplayNames; // full-path key -> leaf-name label
};
