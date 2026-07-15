// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderAuditUtils.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/AssetUserData.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInsights.h"
#include "MaterialShared.h"
#include "Misc/AssetRegistryInterface.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "ShaderAuditCore.h" // for LogShaderAudit
#include "ShaderAuditModule.h"
#include "RHIShaderFormatDefinitions.inl"
#include "RHIShaderPlatform.h"
#include "UObject/Class.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/UObjectGlobals.h"

namespace UE::ShaderAudit::Utils
{

// ============================================================================
// Config loading
// ============================================================================

// Parse "/Script/Module.ClassName" into a FTopLevelAssetPath.
// Returns false if the string is missing the dot separator.
static bool TryParseClassPath(const FString& ClassPath, FTopLevelAssetPath& OutPath)
{
	int32 DotIdx = INDEX_NONE;
	if (!ClassPath.FindLastChar(TEXT('.'), DotIdx))
	{
		return false;
	}
	OutPath = FTopLevelAssetPath(FName(ClassPath.Left(DotIdx)), FName(ClassPath.Mid(DotIdx + 1)));
	return true;
}

static FMaterialResolverConfig LoadMaterialResolverConfig()
{
	FMaterialResolverConfig C;
	const FString ConfigPath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Plugins/ShaderAudit/Config/ShaderAudit.ini"));

	if (!FPaths::FileExists(ConfigPath))
	{
		UE_LOGF(LogShaderAudit, Log,
			"No material resolver config at %ls (game-specific features disabled)", *ConfigPath);
		return C;
	}

	FConfigFile Config;
	Config.Read(ConfigPath);

	// [SubObjectResolver]
	if (const FConfigSection* ResolverSection = Config.FindSection(TEXT("SubObjectResolver")))
	{
		if (const FConfigValue* ClassVal = ResolverSection->Find(FName(TEXT("AssetUserDataClass"))))
		{
			TryParseClassPath(ClassVal->GetValue(), C.AssetUserDataClassPath);
		}
	}

	// [SubObjectPrimaries]
	if (const FConfigSection* PrimariesSection = Config.FindSection(TEXT("SubObjectPrimaries")))
	{
		if (const FConfigValue* ClassVal = PrimariesSection->Find(FName(TEXT("MappingDataClass"))))
		{
			TryParseClassPath(ClassVal->GetValue(), C.MappingDataClassPath);
		}

		for (const TPair<FName, FConfigValue>& Pair : *PrimariesSection)
		{
			if (Pair.Key != FName(TEXT("MappingDataClass")))
			{
				C.SubObjectPrimaryProps.Emplace(Pair.Key.ToString(), FName(*Pair.Value.GetValue()));
			}
		}
	}

	// [ShaderRootClasses]
	if (const FConfigSection* RootSection = Config.FindSection(TEXT("ShaderRootClasses")))
	{
		TArray<FString> ClassPaths;
		RootSection->MultiFind(FName(TEXT("Class")), ClassPaths, /*bMaintainOrder*/ true);
		for (const FString& ClassPath : ClassPaths)
		{
			FTopLevelAssetPath Path;
			if (TryParseClassPath(ClassPath, Path))
			{
				C.ExtraRootClasses.Add(Path);
			}
			else
			{
				UE_LOGF(LogShaderAudit, Warning,
					"[ShaderRootClasses] skipping malformed class path: %ls", *ClassPath);
			}
		}
	}


	C.bValid = C.AssetUserDataClassPath.IsValid() || C.MappingDataClassPath.IsValid();

	if (C.bValid)
	{
		UE_LOGF(LogShaderAudit, Display, "Loaded material resolver config from %ls", *ConfigPath);
		if (C.AssetUserDataClassPath.IsValid())
		{
			UE_LOGF(LogShaderAudit, Display, "  SubObject resolver: %ls",
				*C.AssetUserDataClassPath.ToString());
		}
		if (C.MappingDataClassPath.IsValid())
		{
			UE_LOGF(LogShaderAudit, Display, "  SubObject primaries: %ls (%d mappings)",
				*C.MappingDataClassPath.ToString(), C.SubObjectPrimaryProps.Num());
		}
		if (C.ExtraRootClasses.Num() > 0)
		{
			UE_LOGF(LogShaderAudit, Display, "  ShaderRootClasses: %d extra class(es)", C.ExtraRootClasses.Num());
		}
	}
	else
	{
		UE_LOGF(LogShaderAudit, Warning,
			"Material resolver config at %ls has no valid class paths", *ConfigPath);
	}

	return C;
}

const FMaterialResolverConfig& GetMaterialResolverConfig()
{
	static FMaterialResolverConfig Instance = LoadMaterialResolverConfig();
	return Instance;
}

// ============================================================================
// BuildMaterialParentMap
// ============================================================================

FMaterialParentMapResult BuildMaterialParentMap(const FString& PathFilter)
{
	FMaterialParentMapResult Result;

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.WaitForCompletion();

	// Enumerate every UMaterialInstance (bSearchSubClasses=true catches UMaterialInstanceConstant,
	// UMaterialInstanceDynamic, and any game-specific subclasses).
	TArray<FAssetData> MICAssets;
	AssetRegistry.GetAssetsByClass(
		UMaterialInstance::StaticClass()->GetClassPathName(), MICAssets, /*bSearchSubClasses*/ true);

	// Enumerate root shader-producing asset classes: UMaterial (always) plus any
	// extra classes listed under [ShaderRootClasses] in ShaderAudit.ini.
	TArray<FAssetData> RootAssets;
	AssetRegistry.GetAssetsByClass(
		UMaterial::StaticClass()->GetClassPathName(), RootAssets, /*bSearchSubClasses*/ false);
	for (const FTopLevelAssetPath& ExtraClass : GetMaterialResolverConfig().ExtraRootClasses)
	{
		if (FindObject<UClass>(ExtraClass))
		{
			AssetRegistry.GetAssetsByClass(ExtraClass, RootAssets, /*bSearchSubClasses*/ false);
		}
	}

	Result.TotalMaterialInterfaces = MICAssets.Num() + RootAssets.Num();
	Result.Pairs.Reserve(Result.TotalMaterialInterfaces);

	// Track every package path we emit so the redirector pass can filter cheaply.
	TSet<FString> KnownPackages;
	KnownPackages.Reserve(Result.TotalMaterialInterfaces);

	static const FName ParentTagName(TEXT("Parent"));

	// --- UMaterialInstance pass: read child->parent edges from the AR tag ---
	for (const FAssetData& Asset : MICAssets)
	{
		const FString PackagePath = Asset.PackageName.ToString();

		if (!PathFilter.IsEmpty() && !PackagePath.StartsWith(PathFilter))
		{
			continue;
		}

		const FAssetRegistryExportPath ParentExportPath =
			Asset.GetTagValueRef<FAssetRegistryExportPath>(ParentTagName);
		if (!ParentExportPath)
		{
			++Result.MissingParentTag;
			continue;
		}

		const FString ParentPackagePath = ParentExportPath.Package.ToString();
		if (ParentPackagePath.IsEmpty())
		{
			++Result.MissingParentTag;
			continue;
		}

		FMaterialParentEntry& Entry = Result.Pairs.AddDefaulted_GetRef();
		Entry.Child = PackagePath;
		Entry.Parent = ParentPackagePath;
		KnownPackages.Add(PackagePath);
		KnownPackages.Add(ParentPackagePath);
	}

	// --- Root assets pass: UMaterial + config-driven extra classes (no parent) ---
	for (const FAssetData& Asset : RootAssets)
	{
		const FString PackagePath = Asset.PackageName.ToString();

		if (!PathFilter.IsEmpty() && !PackagePath.StartsWith(PathFilter))
		{
			continue;
		}

		FMaterialParentEntry& Entry = Result.Pairs.AddDefaulted_GetRef();
		Entry.Child = PackagePath;
		// Entry.Parent intentionally left empty -- these are hierarchy roots.
		KnownPackages.Add(PackagePath);
	}

	// --- ObjectRedirector pass: add redirector->canonical edges so stale SHK paths resolve ---
	{
		static const FName DestinationTagName(TEXT("DestinationObject"));

		TArray<FAssetData> RedirectorAssets;
		AssetRegistry.GetAssetsByClass(
			UE::AssetRegistry::GetClassPathObjectRedirector(), RedirectorAssets, /*bSearchSubClasses*/ false);

		for (const FAssetData& Asset : RedirectorAssets)
		{
			const FString RedirectorPkg = Asset.PackageName.ToString();

			if (!PathFilter.IsEmpty() && !RedirectorPkg.StartsWith(PathFilter))
			{
				continue;
			}

			FString DestTagValue;
			if (!Asset.GetTagValue(DestinationTagName, DestTagValue) || DestTagValue.IsEmpty() || DestTagValue == TEXT("None"))
			{
				continue;
			}

			const FString CanonicalPkg = FSoftObjectPath(DestTagValue).GetLongPackageName();
			if (KnownPackages.Contains(CanonicalPkg))
			{
				FMaterialParentEntry& Entry = Result.Pairs.AddDefaulted_GetRef();
				Entry.Child = RedirectorPkg;
				Entry.Parent = CanonicalPkg;
			}
		}
	}

	// --- Sub-object primary discovery (game-specific, config-driven) ---
	const FMaterialResolverConfig& Config = GetMaterialResolverConfig();
	if (Config.MappingDataClassPath.IsValid())
	{
		if (UClass* MappingClass = FindObject<UClass>(Config.MappingDataClassPath))
		{
			TArray<FAssetData> MappingAssets;
			AssetRegistry.GetAssetsByClass(Config.MappingDataClassPath, MappingAssets, /*bSearchSubClasses*/ false);

			UObject* MappingInstance = MappingAssets.Num() > 0 ? MappingAssets[0].GetAsset() : nullptr;
			if (MappingInstance)
			{
			for (const TPair<FString, FName>& PropMapping : Config.SubObjectPrimaryProps)
				{
					const FString& LeafName = PropMapping.Key;
					const FName& PropName = PropMapping.Value;

					FProperty* Prop = MappingClass->FindPropertyByName(PropName);
					FSoftObjectProperty* SoftProp = Prop ? CastField<FSoftObjectProperty>(Prop) : nullptr;
					if (!SoftProp)
					{
						continue;
					}

					const void* ValuePtr = SoftProp->ContainerPtrToValuePtr<void>(MappingInstance);
					check(ValuePtr);

					const FSoftObjectPath SoftPath =
						SoftProp->GetPropertyValue(ValuePtr).GetUniqueID();
					if (SoftPath.IsNull())
					{
						continue;
					}

					FSubObjectPrimary& Primary = Result.SubObjectPrimaries.AddDefaulted_GetRef();
					Primary.LeafName = LeafName;
					Primary.PrimaryPackagePath = SoftPath.GetLongPackageName();
				}

				if (Result.SubObjectPrimaries.Num() > 0)
				{
					UE_LOGF(LogShaderAudit, Display,
					"BuildMaterialParentMap: found %d sub-object primary materials via reflection",
					Result.SubObjectPrimaries.Num());
				}
			}
		}
	}

	UE_LOGF(LogShaderAudit, Display,
		"BuildMaterialParentMap: %d entries from %d material interfaces + %d roots",
		Result.Pairs.Num(), MICAssets.Num(), RootAssets.Num());

	return Result;
}

// ============================================================================
// ResolveMaterialPath
// ============================================================================

UMaterialInterface* ResolveMaterialPath(const FString& MaterialPath, FString* OutError)
{
	check(!MaterialPath.IsEmpty());

	// Fast path: direct load covers everything except sub-object paths.
	if (UMaterialInterface* Result = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
	{
		return Result;
	}

	// Sub-object paths contain ':' (e.g. "/Game/Pkg/Asset.Asset:SubObject.LeafName").
	// The direct load above will have failed for these since ':' isn't a standard UE
	// object path separator — fall through to the ini-driven resolver.
	int32 ColonIndex = INDEX_NONE;
	if (!MaterialPath.FindChar(TEXT(':'), ColonIndex))
	{
		if (OutError) { *OutError = TEXT("load_failed"); }
		return nullptr;
	}

	const FString OuterPath = MaterialPath.Left(ColonIndex);
	const FString SubPath   = MaterialPath.Mid(ColonIndex + 1);

	// Leaf = last dotted component (e.g. "CharacterOpaque").
	FString LeafName;
	int32 LastDot = INDEX_NONE;
	SubPath.FindLastChar(TEXT('.'), LastDot);
	LeafName = (LastDot != INDEX_NONE) ? SubPath.Mid(LastDot + 1) : SubPath;

	UMaterialInterface* OuterMaterial = LoadObject<UMaterialInterface>(nullptr, *OuterPath);
	if (!OuterMaterial)
	{
		if (OutError) { *OutError = FString::Printf(TEXT("outer_load_failed:%s"), *OuterPath); }
		return nullptr;
	}

	const FMaterialResolverConfig& Config = GetMaterialResolverConfig();
	if (!Config.AssetUserDataClassPath.IsValid())
	{
		if (OutError) { *OutError = TEXT("no_sub_object_resolver_configured"); }
		return nullptr;
	}

	UClass* UserDataClass = FindObject<UClass>(Config.AssetUserDataClassPath);
	if (!UserDataClass)
	{
		if (OutError) { *OutError = TEXT("sub_object_resolver_class_not_found"); }
		return nullptr;
	}

	UAssetUserData* UserData = OuterMaterial->GetAssetUserDataOfClass(UserDataClass);
	if (!UserData)
	{
		if (OutError) { *OutError = TEXT("no_resolver_data_on_outer"); }
		return nullptr;
	}

	// Walk UMaterialInstanceConstant* properties, match by BlueprintGetter suffix
	// ("GetCharacterOpaque" -> "CharacterOpaque").
	for (TFieldIterator<FObjectProperty> It(UserDataClass); It; ++It)
	{
		if (!It->PropertyClass->IsChildOf(UMaterialInstanceConstant::StaticClass()))
		{
			continue;
		}

		FString GetterName = It->GetMetaData(TEXT("BlueprintGetter"));
		if (GetterName.IsEmpty())
		{
			continue;
		}

		FString Suffix = GetterName;
		Suffix.RemoveFromStart(TEXT("Get"));

		if (Suffix == LeafName)
		{
			UObject* Value = It->GetObjectPropertyValue_InContainer(UserData);
			if (UMaterialInterface* SubMaterial = Cast<UMaterialInterface>(Value))
			{
				return SubMaterial;
			}
		}
	}

	if (OutError)
	{
		*OutError = FString::Printf(TEXT("sub_object_material_not_found:%s"), *LeafName);
	}
	return nullptr;
}

// ============================================================================
// ExtractMaterialInsights
// ============================================================================

static void FlattenShaderStringMap(
	const TMap<FString, FString>& In,
	TArray<FHlslSection>& Out)
{
	Out.Reserve(In.Num());
	for (const TPair<FString, FString>& Pair : In)
	{
		FHlslSection& Section = Out.AddDefaulted_GetRef();
		Section.Name = Pair.Key;
		Section.Hlsl = Pair.Value;
	}
}

FMaterialInsightsResult ExtractMaterialInsights(
	const TArray<FString>& Paths,
	const FString& Platform)
{
	FMaterialInsightsResult Result;

	static constexpr int32 MaxPaths = 10000;
	if (Paths.IsEmpty() || Paths.Num() > MaxPaths)
	{
		FMaterialInsightFailure& Failure = Result.Failed.AddDefaulted_GetRef();
		Failure.Path = TEXT("(request)");
		Failure.Reason = Paths.IsEmpty()
			? TEXT("paths_array_empty")
			: FString::Printf(TEXT("paths_count_exceeds_limit_%d"), MaxPaths);
		Result.FailedCount = 1;
		return Result;
	}

	// Platform: accept string name, fall back to the editor's max RHI platform.
	EShaderPlatform ShaderPlatform = GMaxRHIShaderPlatform;
	if (!Platform.IsEmpty())
	{
		const EShaderPlatform Parsed = ShaderFormatNameToShaderPlatform(FName(*Platform));
		if (Parsed != SP_NumPlatforms)
		{
			ShaderPlatform = Parsed;
		}
	}

	// Collect unique paths.
	TArray<FString> UniquePaths;
	UniquePaths.Reserve(Paths.Num());
	for (const FString& Path : Paths)
	{
		if (!Path.IsEmpty())
		{
			UniquePaths.AddUnique(Path);
		}
	}

	UE_LOGF(LogShaderAudit, Display,
		"ExtractMaterialInsights: processing %d materials",
		UniquePaths.Num());

	// Pass 1: resolve + load.
	TArray<TPair<FString, UMaterialInterface*>> LoadedMaterials;
	LoadedMaterials.Reserve(UniquePaths.Num());

	{
		FScopedSlowTask SlowTask(UniquePaths.Num(),
			FText::Format(
				NSLOCTEXT("ShaderAuditUtils", "LoadingMaterialsForInsights", "Loading {0} materials for insights"),
				UniquePaths.Num()),
			GIsEditor && !IsRunningCommandlet());
		SlowTask.MakeDialog();

		for (const FString& Path : UniquePaths)
		{
			SlowTask.EnterProgressFrame(1.f);

			FString Error;
			if (UMaterialInterface* Material = ResolveMaterialPath(Path, &Error))
			{
				LoadedMaterials.Emplace(Path, Material);
			}
			else
			{
				FMaterialInsightFailure& Failure = Result.Failed.AddDefaulted_GetRef();
				Failure.Path = Path;
				Failure.Reason = MoveTemp(Error);
			}
		}
	}

	// Pass 2: translate each loaded material to HLSL and harvest FMaterialInsights.
	Result.Materials.Reserve(LoadedMaterials.Num());

	{
		FScopedSlowTask SlowTask(LoadedMaterials.Num(),
			FText::Format(
				NSLOCTEXT("ShaderAuditUtils", "TranslatingMaterials", "Translating {0} materials"),
				LoadedMaterials.Num()),
			GIsEditor && !IsRunningCommandlet());
		SlowTask.MakeDialog();

		for (const TPair<FString, UMaterialInterface*>& Entry : LoadedMaterials)
		{
			SlowTask.EnterProgressFrame(1.f);

			const FString& Path = Entry.Key;
			UMaterialInterface* Material = Entry.Value;

			FMaterialResource* Resource = Material->GetMaterialResource(ShaderPlatform);
			if (!Resource)
			{
				FMaterialInsightFailure& Failure = Result.Failed.AddDefaulted_GetRef();
				Failure.Path = Path;
				Failure.Reason = TEXT("no_material_resource");
				continue;
			}

			FString FullSource;
			if (!Resource->GetMaterialExpressionSource(FullSource))
			{
				FMaterialInsightFailure& Failure = Result.Failed.AddDefaulted_GetRef();
				Failure.Path = Path;
				Failure.Reason = TEXT("translate_failed");
				continue;
			}

			UMaterialInterface* ResourceMI = Resource->GetMaterialInterface();
			if (!ResourceMI)
			{
				FMaterialInsightFailure& Failure = Result.Failed.AddDefaulted_GetRef();
				Failure.Path = Path;
				Failure.Reason = TEXT("no_material_resource_material_interface");
				continue;
			}

			const FMaterialInsights* Insights = ResourceMI->MaterialInsight.Get();
			if (!Insights)
			{
				FMaterialInsightFailure& Failure = Result.Failed.AddDefaulted_GetRef();
				Failure.Path = Path;
				Failure.Reason = TEXT("no_insights_after_translate");
				continue;
			}

			FMaterialInsight& Out = Result.Materials.AddDefaulted_GetRef();
			Out.Path = Path;
			Out.bUsesNewGenerator = Material->IsUsingNewHLSLGenerator();

			FlattenShaderStringMap(Insights->Legacy_ShaderStringParameters, Out.LegacySections);
			FlattenShaderStringMap(Insights->New_ShaderStringParameters, Out.NewSections);
		}
	}

	Result.Succeeded = Result.Materials.Num();
	Result.FailedCount = Result.Failed.Num();

	UE_LOGF(LogShaderAudit, Display,
		"ExtractMaterialInsights: %d succeeded, %d failed",
		Result.Succeeded, Result.FailedCount);

	return Result;
}

} // namespace UE::ShaderAudit::Utils
