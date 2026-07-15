// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProductionSettings.h"

#include "ProductionFunctionLibrary.generated.h"

#define UE_API CINEASSEMBLYTOOLSEDITOR_API

class UCineAssembly;
class UCineAssemblySchema;

/**
 * Library of Blueprint/Python accessible functions to interface with the Cinematic Production Settings
 */
UCLASS(MinimalAPI)
class UProductionFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Returns the Production Settings object */
	UFUNCTION(BlueprintPure, Category = "Cine Assembly Tools")
	static UE_API UProductionSettings* GetProductionSettings();

	/** Returns an array of all available Cinematic Productions */
	UFUNCTION(BlueprintPure, Category = "Cine Assembly Tools")
	static UE_API TArray<FCinematicProduction> GetAllProductions();

	/** Get the Cinematic Production matching the input ProductionID (if it exists) */
	UFUNCTION(BlueprintPure, Category = "Cine Assembly Tools", meta=(ReturnDisplayName="IsValid"))
	static UE_API bool GetProduction(FGuid ProductionID, FCinematicProduction& Production);

	/** Get the active Cinematic Production (if it exists) */
	UFUNCTION(BlueprintPure, Category = "Cine Assembly Tools", meta=(ReturnDisplayName = "IsValid"))
	static UE_API bool GetActiveProduction(FCinematicProduction& Production);

	/**
	 * Sets the input Production as the current Active Production
	 * If no input is provided, the Active Production will be set to None
	 */
	UFUNCTION(BlueprintCallable, Category = "Cine Assembly Tools")
	static UE_API void SetActiveProduction(FCinematicProduction Production = FCinematicProduction());

	/** Sets the Production matching the input ProductionID as the current Active Production */
	UFUNCTION(BlueprintCallable, Category = "Cine Assembly Tools")
	static UE_API void SetActiveProductionByID(FGuid ProductionID);

	/** Sets the active Cinematic Production to None */
	UFUNCTION(BlueprintCallable, Category = "Cine Assembly Tools")
	static UE_API void ClearActiveProduction();

	/** Returns true if input ProductionID matches the ID of the current Active Production */
	UFUNCTION(BlueprintPure, Category = "Cine Assembly Tools")
	static UE_API bool IsActiveProduction(FGuid ProductionID);

	/** Add the input Cinematic Production to the Production Settings' list of productions */
	UFUNCTION(BlueprintCallable, Category = "Cine Assembly Tools")
	static UE_API void AddProduction(FCinematicProduction Production);

	/** Removes the Cinematic Production matching the input ProductionID from the Production Settings' list of productions */
	UFUNCTION(BlueprintCallable, Category = "Cine Assembly Tools")
	static UE_API void DeleteProduction(FGuid ProductionID);

	/** Renames the Cinematic Production matching the input ProductionID */
	UFUNCTION(BlueprintCallable, Category = "Cine Assembly Tools")
	static UE_API void RenameProduction(FGuid ProductionID, FString NewName);

	/** Given a ProductionID and an Extended Data Struct type, returns the Extended Data object for that production if found. */
	UFUNCTION(BlueprintCallable, Category = "Cine Assembly Tools")
	static UE_API bool GetProductionExtendedData(FGuid ProductionID, const UScriptStruct* DataStruct, FInstancedStruct& OutData);

	/** Given a ProductionID and an Extended Data Struct, sets the struct data on the production. */
	UFUNCTION(BlueprintCallable, Category = "Cine Assembly Tools")
	static UE_API bool SetProductionExtendedData(FGuid ProductionID, const FInstancedStruct& Data);

	/**
	 * Note: Prefer the CineAssembly Editor Function Library version of CreateAssembly. The Production Function Library version is kept for backwards compatibility.
	 *
	 * Create a new CineAssembly asset using the input Schema, Level, and Metadata.
	 * If bUseDefaultNameFromSchema is true, the default assembly name from the specified schema will be used as the new asset name.
	 * It is important that any metadata required for resolving asset naming tokens is provided to this function so that the Assembly and SubAssemblies are all named correctly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cine Assembly Tools", meta = (AutoCreateRefTerm = "Metadata"))
	static UE_API UCineAssembly* CreateAssembly(UCineAssemblySchema* Schema, TSoftObjectPtr<UWorld> Level, TSoftObjectPtr<UCineAssembly> ParentAssembly, const TMap<FString, FString>& Metadata, const FString& Path, const FString& Name, bool bUseDefaultNameFromSchema = true);
};

#undef UE_API
