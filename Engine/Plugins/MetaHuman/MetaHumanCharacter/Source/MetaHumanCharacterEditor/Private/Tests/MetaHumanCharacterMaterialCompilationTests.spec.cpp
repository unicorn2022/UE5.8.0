// Copyright Epic Games, Inc. All Rights Reserved.

/*
 * Material Shader Compilation Spec for MetaHumanCharacter Plugin
 *
 * Compiles all materials shipped with the MetaHumanCharacter plugin for every
 * shader platform targeted by the current project, across all material quality
 * levels (Low, Medium, High, Epic). This catches mismatches between material
 * features (e.g. Virtual Textures, Substrate) and project settings (e.g. shader
 * model, ray-tracing) that would otherwise only surface during a cook or at
 * load time in a specific project/quality configuration.
 *
 * Structured as a test spec with nested Describe blocks per platform and shader
 * format so that results are grouped logically in the automation UI.
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Algo/Copy.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"
#include "ShaderCompiler.h"

BEGIN_DEFINE_SPEC(
	FMetaHumanCharacterMaterialCompilationSpec,
	"MetaHumanCreator.MaterialShaderCompilation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	TArray<TStrongObjectPtr<UMaterialInterface>> LoadedMaterials;

	/** The platform that LoadedMaterials currently have cached cook data for, if any. */
	ITargetPlatform* CachedTargetPlatform = nullptr;

	/** Discover and load all materials under the plugin content mount point. */
	void DiscoverAndLoadMaterials()
	{
		// Clean up any state from a previous compilation cycle against the platform it was cached for
		CleanUp();

		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();

		const bool bSynchronousSearch = true;
		AssetRegistry.SearchAllAssets(bSynchronousSearch);

		TArray<FAssetData> MaterialAssets;
		{
			FARFilter Filter;
			Filter.bRecursiveClasses = true;
			Filter.bRecursivePaths = true;
			Filter.PackagePaths.Add(FName(TEXT("/" UE_PLUGIN_NAME)));
			Filter.ClassPaths.Add(UMaterialInterface::StaticClass()->GetClassPathName());
			AssetRegistry.GetAssets(Filter, MaterialAssets);
		}

		if (!TestFalse(TEXT("No materials found under /" UE_PLUGIN_NAME "/"), MaterialAssets.IsEmpty()))
		{
			return;
		}

		// Sort for deterministic ordering
		Algo::SortBy(MaterialAssets, &FAssetData::GetSoftObjectPath, FSoftObjectPathLexicalLess());

		LoadedMaterials.Reset();
		LoadedMaterials.Reserve(MaterialAssets.Num());

		for (const FAssetData& AssetData : MaterialAssets)
		{
			UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(AssetData.GetAsset());
			if (TestNotNull(
					FString::Format(TEXT("Material loaded: {0}"), { AssetData.GetSoftObjectPath().ToString() }),
					MaterialInterface))
			{
				LoadedMaterials.Emplace(MaterialInterface);
			}
		}

		AddInfo(FString::Format(TEXT("Compiling {0} materials under /" UE_PLUGIN_NAME "/"), { LoadedMaterials.Num() }));
	}

	/** Kick off cook-pipeline compilation for all loaded materials and block until complete. */
	void CompileAndWait(ITargetPlatform* TargetPlatform, const FString& PlatformName, const FString& ShaderFormatName)
	{
		if (!TestNotNull(TEXT("GShaderCompilingManager is available"), GShaderCompilingManager))
		{
			return;
		}

		// Track the platform so CleanUp() can target the correct one even if the next compile is for a different platform
		CachedTargetPlatform = TargetPlatform;

		for (const TStrongObjectPtr<UMaterialInterface>& Material : LoadedMaterials)
		{
			Material->BeginCacheForCookedPlatformData(TargetPlatform);
		}

		TSet<UMaterialInterface*> PendingMaterials;
		for (const TStrongObjectPtr<UMaterialInterface>& Material : LoadedMaterials)
		{
			if (!Material->IsCachedCookedPlatformDataLoaded(TargetPlatform))
			{
				PendingMaterials.Add(Material.Get());
			}
		}

		constexpr double TimeoutSeconds = 300.0;
		const double StartTime = FPlatformTime::Seconds();

		while (!PendingMaterials.IsEmpty())
		{
			const bool bLimitExecTime = false;
			const bool bBlockOnGlobalShaderCompletion = false;
			GShaderCompilingManager->ProcessAsyncResults(bLimitExecTime, bBlockOnGlobalShaderCompletion);

			// Remove materials whose shader compilation has finished
			for (TSet<UMaterialInterface*>::TIterator It(PendingMaterials); It; ++It)
			{
				if ((*It)->IsCachedCookedPlatformDataLoaded(TargetPlatform))
				{
					It.RemoveCurrent();
				}
			}

			const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
			if (ElapsedSeconds > TimeoutSeconds)
			{
				AddError(FString::Format(
					TEXT("Timed out after {0}s waiting for shader compilation on {1} [{2}]. {3} materials still pending."),
					{ ElapsedSeconds, PlatformName, ShaderFormatName, PendingMaterials.Num() }));
				break;
			}

			FPlatformProcess::Sleep(0.1f);
		}

		// Drain any remaining async work
		const bool bLimitExecTime = false;
		const bool bBlockOnGlobalShaderCompletion = false;
		GShaderCompilingManager->ProcessAsyncResults(bLimitExecTime, bBlockOnGlobalShaderCompletion);
	}

	/** Clean up cached cook data for all loaded materials against the platform they were cached for. */
	void CleanUp()
	{
		if (CachedTargetPlatform != nullptr)
		{
			for (const TStrongObjectPtr<UMaterialInterface>& Material : LoadedMaterials)
			{
				Material->ClearCachedCookedPlatformData(CachedTargetPlatform);
			}

			FlushRenderingCommands();
			CachedTargetPlatform = nullptr;
		}

		LoadedMaterials.Reset();
	}

END_DEFINE_SPEC(FMetaHumanCharacterMaterialCompilationSpec)

void FMetaHumanCharacterMaterialCompilationSpec::Define()
{
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	const TArray<ITargetPlatform*>& ActivePlatforms = TPM.GetActiveTargetPlatforms();

	for (ITargetPlatform* Platform : ActivePlatforms)
	{
		TArray<FName> ShaderFormats;
		Platform->GetAllTargetedShaderFormats(ShaderFormats);

		if (ShaderFormats.IsEmpty())
		{
			AddInfo(FString::Format(TEXT("Skipping platform {0}: no targeted shader formats"), { Platform->PlatformName() }));
			continue;
		}

		const FString PlatformName = Platform->PlatformName();

		Describe(PlatformName, [this, Platform, PlatformName, ShaderFormats]()
		{
			for (const FName& ShaderFormat : ShaderFormats)
			{
				const FString ShaderFormatName = ShaderFormat.ToString();
				const EShaderPlatform ShaderPlatform = ShaderFormatToLegacyShaderPlatform(ShaderFormat);

				Describe(ShaderFormatName, [this, Platform, PlatformName, ShaderFormatName, ShaderPlatform]()
				{
					// Flag scoped per (platform, format) block so each combination compiles independently
					TSharedPtr<bool> bCompiled = MakeShared<bool>(false);

					// Compile once on first It(), reuse for subsequent quality checks
					BeforeEach([this, Platform, PlatformName, ShaderFormatName, bCompiled]()
					{
						if (!*bCompiled)
						{
							DiscoverAndLoadMaterials();
							CompileAndWait(Platform, PlatformName, ShaderFormatName);
							*bCompiled = true;
						}
					});

					for (int32 QualityLevelIndex = 0; QualityLevelIndex < EMaterialQualityLevel::Num; ++QualityLevelIndex)
					{
						const EMaterialQualityLevel::Type QualityLevel = static_cast<EMaterialQualityLevel::Type>(QualityLevelIndex);
						const FString QualityName = LexToString(QualityLevel);

						It(FString::Format(TEXT("should compile all materials at {0} quality"), { QualityName }),
							[this, ShaderPlatform, ShaderFormatName, QualityLevel, QualityName]()
						{
							for (const TStrongObjectPtr<UMaterialInterface>& Material : LoadedMaterials)
							{
								const FMaterialResource* Resource = Material->GetMaterialResource(
									ShaderPlatform,
									QualityLevel);

								if (!Resource)
								{
									continue;
								}

								const TArray<FString>& CompileErrors = Resource->GetCompileErrors();
								for (const FString& CompileError : CompileErrors)
								{
									AddError(FString::Format(
										TEXT("[{0}] [{1}] {2}: {3}"),
										{ ShaderFormatName, QualityName, Material->GetPathName(), CompileError }));
								}
							}
						});
					}

				});
			}
		});
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
