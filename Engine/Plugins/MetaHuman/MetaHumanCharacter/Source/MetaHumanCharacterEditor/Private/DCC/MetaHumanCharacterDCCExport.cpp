// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterDCCExport.h"

#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanSDKEditor.h"
#include "Subsystem/MetaHumanCharacterBodyTextureUtils.h"
#include "MetaHumanCharacterThumbnailRenderer.h"
#include "Subsystem/MetaHumanCharacterBuild.h"
#include "MetaHumanCharacterPaletteUnpackHelpers.h"
#include "MetaHumanCharacterAnalytics.h"
#include "MetaHumanCharacterGeneratedAssets.h"

#include "TG_Material.h"
#include "TG_Graph.h"
#include "Blueprint/TG_AsyncExportTask.h"
#include "Editor/EditorEngine.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Logging/StructuredLog.h"
#include "Framework/Application/SlateApplication.h"
#include "DesktopPlatformModule.h"
#include "Developer/FileUtilities/Public/FileUtilities/ZipArchiveWriter.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/ScopeExit.h"
#include "DNAUtils.h"
#include "Misc/EngineVersion.h"
#include "ObjectTools.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "MetaHumanCharacterPaletteProjectSettings.h"
#include "JsonObjectConverter.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Misc/AssertionMacros.h"
#include "IMessageLogListing.h"
#include "MessageLogModule.h"

#include "Misc/ObjectThumbnail.inl"
#include "Async/ParallelFor.h"
#include "Templates/Function.h"

#include <atomic>

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"


namespace UE::MetaHuman
{
	static bool ExportCommonSourceAssets(const FString& DCCRootPath, TSharedPtr<FZipArchiveWriter> InArchiveWriter, const FString& OutputFolder, const FString& InMasksFolder, TArray<FString>& OutMaskFiles)
	{
		bool bResult = true;

		auto AddFolderFilesToArchive = [DCCRootPath, InArchiveWriter, OutputFolder, &bResult](const FString& SubFolder, TArray<FString>& OutWrittenFiles)
			{
				// Adding common source files.
				const FString FullSubFolder = DCCRootPath / SubFolder;
				const FString SourceFolder = FPaths::ConvertRelativePathToFull(FullSubFolder);

				TArray<FString> FoundFiles;
				IFileManager::Get().FindFiles(FoundFiles, *SourceFolder);
				for (FString& SourceAssetFile : FoundFiles)
				{
					const FString FullAssetPath = FPaths::ConvertRelativePathToFull(SourceFolder / SourceAssetFile);
					bool bFileWritten = false;
					if (InArchiveWriter)
					{
						TArray<uint8> Data;
						if (FFileHelper::LoadFileToArray(Data, *FullAssetPath))
						{
							InArchiveWriter->AddFile(SubFolder / SourceAssetFile, Data, FDateTime::Now());
							bFileWritten = true;
						}
						else
						{
							const FText Message = FText::Format(
								LOCTEXT("DCCExportFailure_CopyCommonAsset", "Failed to copy {0}."),
								FText::FromString(FullAssetPath));

							FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);
							bResult = false;
						}
					}
					else
					{
						IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
						FString DestinationFolder = OutputFolder / SubFolder;
						PlatformFile.CreateDirectoryTree(*DestinationFolder);
						FString ToFile = DestinationFolder / SourceAssetFile;
						if (PlatformFile.CopyFile(*ToFile, *FullAssetPath))
						{
							bFileWritten = true;
						}
						else
						{
							const FText Message = FText::Format(
								LOCTEXT("DCCExportFailure_CopyCommonAsset", "Failed to copy {0}."),
								FText::FromString(FullAssetPath));

							FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);
							bResult = false;
						}
					}

					if (bFileWritten)
					{
						OutWrittenFiles.Add(SourceAssetFile);
					}
				}
			};

		AddFolderFilesToArchive(InMasksFolder, OutMaskFiles);

		return bResult;
	}

	struct FPendingImageWrite
	{
		FImage Image;
		FString ImageName;
		FString OutputFolder;
		// Optional callback invoked on the calling thread after this image is successfully written to disk / archive.
		// Use this to record manifest entries that should only exist if the file actually landed.
		TFunction<void()> OnSuccess;
	};

	static bool FlushPendingImageWrites(TArray<FPendingImageWrite>& InPendingWrites, TSharedPtr<FZipArchiveWriter> InArchiveWriter)
	{
		const int32 Num = InPendingWrites.Num();
		if (Num == 0)
		{
			return true;
		}

		TArray<TArray64<uint8>> CompressedData;
		CompressedData.SetNum(Num);
		std::atomic<int32> NumErrors{0};

		// Parallel PNG compression
		ParallelFor(Num, [&](int32 Index)
		{
			if (!FImageUtils::CompressImage(CompressedData[Index], TEXT("png"), InPendingWrites[Index].Image))
			{
				++NumErrors;
			}
		});

		if (NumErrors > 0)
		{
			for (int32 Index = 0; Index < Num; ++Index)
			{
				if (CompressedData[Index].IsEmpty())
				{
					const FText Message = FText::Format(LOCTEXT("DCCExportFailure_CompressTimage", "Failed to compress image {0}."), FText::FromString(InPendingWrites[Index].ImageName));
					FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);
				}
			}
			return false;
		}

		// Write results
		if (InArchiveWriter)
		{
			// Archive writer is not thread-safe, sequential writes
			for (int32 Index = 0; Index < Num; ++Index)
			{
				InArchiveWriter->AddFile(InPendingWrites[Index].ImageName + TEXT(".png"), CompressedData[Index], FDateTime::Now());
				if (InPendingWrites[Index].OnSuccess)
				{
					InPendingWrites[Index].OnSuccess();
				}
			}
		}
		else
		{
			// File saves are thread-safe (different files), parallelize
			TArray<bool> SaveFailed;
			SaveFailed.Init(false, Num);

			ParallelFor(Num, [&](int32 Index)
			{
				const FString ImagePath = InPendingWrites[Index].OutputFolder / InPendingWrites[Index].ImageName + TEXT(".png");
				if (!FFileHelper::SaveArrayToFile(CompressedData[Index], *ImagePath))
				{
					SaveFailed[Index] = true;
				}
			});

			bool bAnySaveFailed = false;
			for (int32 Index = 0; Index < Num; ++Index)
			{
				if (SaveFailed[Index])
				{
					const FText Message = FText::Format(LOCTEXT("DCCExportFailure_SaveImage", "Failed to save image {0}."), FText::FromString(InPendingWrites[Index].ImageName));
					FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);
					bAnySaveFailed = true;
				}
				else if (InPendingWrites[Index].OnSuccess)
				{
					InPendingWrites[Index].OnSuccess();
				}
			}

			if (bAnySaveFailed)
			{
				return false;
			}
		}

		return true;
	}

	static bool WriteToArchive(const FString& Filename, const FString& RootPackagePath, TSharedPtr<FZipArchiveWriter> ArchiveWriter, const FString& OutputFolder)
	{
		FString RelativeFilename = Filename;
		FPaths::MakePathRelativeTo(RelativeFilename, *RootPackagePath);
		if (ArchiveWriter)
		{
			TArray<uint8> Data;
			if (!FFileHelper::LoadFileToArray(Data, *Filename))
			{
				const FText Message = FText::Format(LOCTEXT("DCCExportFailure_LoadFile", "Failed to load file {0}."), FText::FromString(Filename));
				FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);
				return false;
			}		
			ArchiveWriter->AddFile(RelativeFilename, Data, FDateTime::Now());
		}
		else
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			const FString ToFile = OutputFolder / RelativeFilename;
			const FString ToFileDir = FPaths::GetPath(ToFile);
			if (!PlatformFile.DirectoryExists(*ToFileDir))
			{
				PlatformFile.CreateDirectoryTree(*ToFileDir);
			}
			if (!PlatformFile.CopyFile(*ToFile, *Filename))
			{
				const FText Message = FText::Format(
					LOCTEXT("DCCExportFailure_CopyFileToOutputFolder", "Failed to copy {0}."),
					FText::FromString(Filename));

				FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);
				return false;
			}
		}
		return true;
	}

	static bool ExportDNAFiles(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, TSharedPtr<FZipArchiveWriter> ArchiveWriter, const FString& OutputFolder, FMetaHumanExportDCCManifest& OutManifest)
	{
		bool bResult = true;

		const FString HeadDNAFile = TEXT("head.dna");
		const FString BodyDNAFile = TEXT("body.dna");

		if (InMetaHumanCharacter->HasFaceDNA())
		{
			TArray<uint8> FaceDNABuffer = InMetaHumanCharacter->GetFaceDNABuffer();
			bool bFaceWritten = false;
			if (ArchiveWriter)
			{
				ArchiveWriter->AddFile(*HeadDNAFile, FaceDNABuffer, FDateTime::Now());
				bFaceWritten = true;
			}
			else
			{
				FString FullPath = OutputFolder / HeadDNAFile;
				if (FFileHelper::SaveArrayToFile(FaceDNABuffer, *FullPath))
				{
					bFaceWritten = true;
				}
				else
				{
					FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_FaceDNANotSaved", "Character asset face DNA could not be saved."))
						->AddToken(FUObjectToken::Create(InMetaHumanCharacter));
					bResult = false;
				}
			}

			if (bFaceWritten)
			{
				OutManifest.Dna.Head = HeadDNAFile;
			}
		}
		else
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_NoFaceDNA", "Character asset has no face DNA."))
				->AddToken(FUObjectToken::Create(InMetaHumanCharacter));
			bResult = false;
		}

		TNotNull<UMetaHumanCharacterEditorSubsystem*> MetaHumanCharacterSubsystem = GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>();

		// TODO: not use actor body skel mesh?
		if (const USkeletalMesh* ConstBodySkeletalMesh = MetaHumanCharacterSubsystem->GetBodyEditMesh(InMetaHumanCharacter))
		{
			// Cast away const-ness for GetAssetUserData. The mesh will not be modified.
			USkeletalMesh* BodySkeletalMesh = const_cast<USkeletalMesh*>(ConstBodySkeletalMesh);
			if (TSharedPtr<IDNAReader> DNAReader = USkelMeshDNAUtils::GetDNAReader(BodySkeletalMesh))
			{
				TSharedRef<IDNAReader> BodyDnaReader = MetaHumanCharacterSubsystem->GetBodyState(InMetaHumanCharacter)->StateToDna(DNAReader->Unwrap());
				TArray<uint8> BodyDnaBuffer;
				SaveDNAToBuffer(&BodyDnaReader.Get(), EDNADataLayer::All, BodyDnaBuffer);
				bool bBodyWritten = false;
				if (ArchiveWriter)
				{
					ArchiveWriter->AddFile(*BodyDNAFile, BodyDnaBuffer, FDateTime::Now());
					bBodyWritten = true;
				}
				else
				{
					FString FullPath = OutputFolder / BodyDNAFile;
					if (FFileHelper::SaveArrayToFile(BodyDnaBuffer, *FullPath))
					{
						bBodyWritten = true;
					}
					else
					{
						FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_BodyDNANotSaved", "Character asset body DNA could not be saved."))
							->AddToken(FUObjectToken::Create(InMetaHumanCharacter));
						bResult = false;
					}
				}

				if (bBodyWritten)
				{
					OutManifest.Dna.Body = BodyDNAFile;
				}
			}
			else
			{
				FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_NoBodyDNA", "Character asset has no body DNA."))
					->AddToken(FUObjectToken::Create(InMetaHumanCharacter)); bResult = false;
			}
		}
		else
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_NoBodySkeletalMesh", "Character asset has no body Skeletal Mesh."))
				->AddToken(FUObjectToken::Create(InMetaHumanCharacter)); 
			bResult = false;
		}

		return bResult;
	}
	static bool BakeTextures(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter,
							 const FMetaHumanCharacterGeneratedAssets& InGeneratedAssets,
							 TMap<EFaceTextureType, FImage>& OutFaceImages,
							 FImage& OutBodyBaseColorImage,
							 FImage& OutBodyNormalImage,
							 FImage& OutEyeColorImage,
							 FImage& OutFaceSRMFImage,
							 FImage& OutBodySRMFImage,
							 FImage& OutTeethColorImage,
							 FImage& OutTeethNormalImage,
							 const FString& TempAssetPath,
							 bool bInBakeMakeup)
	{
		bool bResult = true;

		UTextureGraphInstance* SkinTextureGraph = LoadObject<UTextureGraphInstance>(nullptr, TEXT("/Script/Engine.TextureGraphInstance'/" UE_PLUGIN_NAME "/TextureGraphs/TGI_SkinDCC.TGI_SkinDCC'"));
		if (!SkinTextureGraph)
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_NoFaceTextureGraph", "No Texture Graph for baking the face is assigned to the pipeline"));
			return false;
		}

		UTextureGraphInstance* EyesTextureGraph = LoadObject<UTextureGraphInstance>(nullptr, TEXT("/Script/TextureGraph.TextureGraphInstance'/" UE_PLUGIN_NAME "/TextureGraphs/TGI_Eye_Sclera_sRGB.TGI_Eye_Sclera_sRGB'"));
		if (!EyesTextureGraph)
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_NoEyeIrisTextureGraph", "No Texture Graph for baking the eye textures is assigned to the pipeline"));
			return false;
		}

		UTextureGraphInstance* TeethSRGBTextureGraph = LoadObject<UTextureGraphInstance>(nullptr, TEXT("/Script/TextureGraph.TextureGraphInstance'/" UE_PLUGIN_NAME "/TextureGraphs/TGI_Teeth_sRGB.TGI_Teeth_sRGB'"));
		if (!TeethSRGBTextureGraph)
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_NoTeethSRGBTextureGraph", "No Texture Graph for baking the teeth (sRGB) is assigned to the pipeline"));
			return false;
		}

		UTextureGraphInstance* TeethLinearTextureGraph = LoadObject<UTextureGraphInstance>(nullptr, TEXT("/Script/TextureGraph.TextureGraphInstance'/" UE_PLUGIN_NAME "/TextureGraphs/TGI_Teeth.TGI_Teeth'"));
		if (!TeethLinearTextureGraph)
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_NoTeethTextureGraph", "No Texture Graph for baking the teeth is assigned to the pipeline"));
			return false;
		}

		UTextureGraphInstance* SkinTGI = DuplicateObject<UTextureGraphInstance>(SkinTextureGraph, nullptr);
		UTextureGraphInstance* EyesTGI = DuplicateObject<UTextureGraphInstance>(EyesTextureGraph, nullptr);
		UTextureGraphInstance* TeethSRGBTGI = DuplicateObject<UTextureGraphInstance>(TeethSRGBTextureGraph, nullptr);
		UTextureGraphInstance* TeethLinearTGI = DuplicateObject<UTextureGraphInstance>(TeethLinearTextureGraph, nullptr);

		check(SkinTGI);
		check(EyesTGI);
		check(TeethSRGBTGI);
		check(TeethLinearTGI);

		// Get materials from the generated assets
		FMetaHumanCharacterFaceMaterialSet FaceMaterials = FMetaHumanCharacterSkinMaterials::GetHeadMaterialsFromMesh(InGeneratedAssets.FaceMesh);
		// TryGenerateCharacterAssets guarantees BodyMesh has at least one material slot
		UMaterialInstance* BodyMaterialInstance = Cast<UMaterialInstance>(InGeneratedAssets.BodyMesh->GetMaterials()[0].MaterialInterface);

		// Texture graphs only accept material instance constants
		TStrongObjectPtr<UMaterialInstanceConstant> FaceMaterial{ UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(FaceMaterials.Skin[EMetaHumanCharacterSkinMaterialSlot::LOD0], SkinTGI) };
		TStrongObjectPtr<UMaterialInstanceConstant> EyeMaterial{ UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(FaceMaterials.EyeLeft, EyesTGI) };
		TStrongObjectPtr<UMaterialInstanceConstant> BodyMaterial{ UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(BodyMaterialInstance, SkinTGI) };
		// Single MIC used for both TeethSRGBTGI and TeethLinearTGI; the second param is just the outer for the new object
		TStrongObjectPtr<UMaterialInstanceConstant> TeethMaterial{ UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(FaceMaterials.Teeth, TeethSRGBTGI) };

		// Get the animated map textures from the generated assets
		UTexture* BaseColorCM1Texture = InGeneratedAssets.SynthesizedFaceTextures.FindRef(EFaceTextureType::Basecolor_Animated_CM1);
		bResult &= (BaseColorCM1Texture != nullptr);
		UTexture* BaseColorCM2Texture = InGeneratedAssets.SynthesizedFaceTextures.FindRef(EFaceTextureType::Basecolor_Animated_CM2);
		bResult &= (BaseColorCM2Texture != nullptr);
		UTexture* BaseColorCM3Texture = InGeneratedAssets.SynthesizedFaceTextures.FindRef(EFaceTextureType::Basecolor_Animated_CM3);
		bResult &= (BaseColorCM3Texture != nullptr);
		UTexture* NormalWM1Texture = InGeneratedAssets.SynthesizedFaceTextures.FindRef(EFaceTextureType::Normal_Animated_WM1);
		bResult &= (NormalWM1Texture != nullptr);
		UTexture* NormalWM2Texture = InGeneratedAssets.SynthesizedFaceTextures.FindRef(EFaceTextureType::Normal_Animated_WM2);
		bResult &= (NormalWM2Texture != nullptr);
		UTexture* NormalWM3Texture = InGeneratedAssets.SynthesizedFaceTextures.FindRef(EFaceTextureType::Normal_Animated_WM3);
		bResult &= (NormalWM3Texture != nullptr);

		if (!bResult)
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_MissingSynthesizedTextures", "Failed to find one or more synthesized face textures in the generated assets"));
			return false;
		}

		auto SetMaterialInput = [](UTextureGraphInstance* TextureGraphInstance, FName InputName, UMaterialInstanceConstant* Material)
			{
				if (FVarArgument* MaterialArgument = TextureGraphInstance->InputParams.VarArguments.Find(InputName))
				{
					FTG_Material MaterialValue;
					MaterialValue.SetMaterial(Material);
					MaterialArgument->Var.SetAs(MaterialValue);
				}
				else
				{
					FMessageLog(UE::MetaHuman::MessageLogName)
						.Error(FText::Format(LOCTEXT("DCCExportFailure_NoMaterialInput", "Failed to find material input '{0}' in Texture Graph"), FText::FromName(InputName)))
						->AddToken(FUObjectToken::Create(TextureGraphInstance));
					
					return false;
				}

				return true;
			};

		auto SetTextureInput = [](UTextureGraphInstance* TextureGraphInstance, FName InputName, UTexture* Texture)
			{
				if (FVarArgument* Argument = TextureGraphInstance->InputParams.VarArguments.Find(InputName))
				{
					FTG_Texture TextureValue;
					TextureValue.Descriptor.bIsSRGB = false;
					TextureValue.Descriptor.TextureFormat = ETG_TextureFormat::BGRA8;
					TextureValue.TexturePath = Texture->GetPathName();
					Argument->Var.SetAs(TextureValue);
				}
				else
				{
					FMessageLog(UE::MetaHuman::MessageLogName)
						.Error(FText::Format(LOCTEXT("DCCExportFailure_NoTexureInput", "Failed to find texture input '{0}' in Texture Graph"), FText::FromName(InputName)))
						->AddToken(FUObjectToken::Create(TextureGraphInstance));

					return false;
				}

				return true;
			};

		auto SetBoolInput = [](UTextureGraphInstance* TextureGraphInstance, FName InputName, bool Value)
			{
				if (FVarArgument* Argument = TextureGraphInstance->InputParams.VarArguments.Find(InputName))
				{
					Argument->Var.SetAs(Value);
				}
				else
				{
					FMessageLog(UE::MetaHuman::MessageLogName)
						.Error(FText::Format(LOCTEXT("DCCExportFailure_NoBoolInput", "Failed to find bool input named '{0}' in Texture Graph"), FText::FromName(InputName)))
						->AddToken(FUObjectToken::Create(TextureGraphInstance));

					return false;
				}

				return true;
			};

		// Set Skin Material Inputs
		bool bInputsOk = true;

		bInputsOk &= SetMaterialInput(SkinTGI, TEXT("Face Material sRGB"), FaceMaterial.Get());
		bInputsOk &= SetMaterialInput(SkinTGI, TEXT("Face Material"), FaceMaterial.Get());
		bInputsOk &= SetMaterialInput(SkinTGI, TEXT("Body Material sRGB"), BodyMaterial.Get());

		// Set Eye Material Input
		bInputsOk &= SetMaterialInput(EyesTGI, TEXT("Material"), EyeMaterial.Get());

		// Set Teeth Material Inputs
		bInputsOk &= SetMaterialInput(TeethSRGBTGI, TEXT("Material"), TeethMaterial.Get());
		bInputsOk &= SetMaterialInput(TeethLinearTGI, TEXT("Material"), TeethMaterial.Get());

		// Set Skin Texture Inputs
		bInputsOk &= SetTextureInput(SkinTGI, TEXT("AnimatedMap_CM1"), BaseColorCM1Texture);
		bInputsOk &= SetTextureInput(SkinTGI, TEXT("AnimatedMap_CM2"), BaseColorCM2Texture);
		bInputsOk &= SetTextureInput(SkinTGI, TEXT("AnimatedMap_CM3"), BaseColorCM3Texture);

		bInputsOk &= SetTextureInput(SkinTGI, TEXT("AnimatedMap_WM1"), NormalWM1Texture);
		bInputsOk &= SetTextureInput(SkinTGI, TEXT("AnimatedMap_WM2"), NormalWM2Texture);
		bInputsOk &= SetTextureInput(SkinTGI, TEXT("AnimatedMap_WM3"), NormalWM3Texture);

		// Enable or disable the baking of makeup in the skin texture graph
		bInputsOk &= SetBoolInput(SkinTGI, TEXT("Bake Makeup"), bInBakeMakeup);

		// Disable BakeSclera to get the full eye base color
		bInputsOk &= SetBoolInput(EyesTGI, TEXT("BakeSclera"), false);

		if (!bInputsOk)
		{
			return false;
		}

		TMap<FName, TSoftObjectPtr<UTexture>> GeneratedTextures;

		const TSortedMap<UTextureGraphInstance*, FString> OutputSuffix =
		{
			{ SkinTGI, TEXT("_Skin") },
			{ EyesTGI, TEXT("_Eye") },
			{ TeethSRGBTGI, TEXT("_Teeth_sRGB") },
			{ TeethLinearTGI, TEXT("_Teeth") }
		};

		for (UTextureGraphInstance* TextureGraphInstance : { SkinTGI, EyesTGI, TeethSRGBTGI, TeethLinearTGI })
		{
			// find the output of the TG
			for (TPair<FTG_Id, FTG_OutputSettings>& Pair : TextureGraphInstance->OutputSettingsMap)
			{
				// The Texture Graph team has provided us with this temporary workaround to get the 
				// output parameter name.
				//
				// The hard coded constant will be removed when a proper solution is available.
				const int32 PinIndex = 3;
				const FTG_Id PinId(Pair.Key.NodeIdx(), PinIndex);

				FTG_OutputSettings& OutputSettings = Pair.Value;

				FName ParamName = TextureGraphInstance->Graph()->GetParamName(PinId);

				if (ParamName.IsNone())
				{
					ParamName = OutputSettings.OutputName;
				}

				FString OutputName = FString::Format(TEXT("{0}{1}"), { ParamName.ToString(), OutputSuffix[TextureGraphInstance] });

				OutputSettings.FolderPath = *TempAssetPath;
				OutputSettings.BaseName = FName{ TEXT("T_") + InMetaHumanCharacter->GetName() + TEXT("_") + OutputName };
				// Get a path to the generated texture
				const FString PackageName = OutputSettings.FolderPath.ToString() / OutputSettings.BaseName.ToString();
				const FString AssetPath = FString::Format(TEXT("{0}.{1}"), { PackageName, OutputSettings.BaseName.ToString() });
				TSoftObjectPtr<UTexture> GeneratedTexture{ FSoftObjectPath(AssetPath) };

				GeneratedTextures.Emplace(OutputName, GeneratedTexture);
			}

			// export the TG textures
			const bool bOverwriteTextures = true;
			const bool bSave = false;
			const bool bExportAll = false;
			const bool bDisableCache = true;
			UTG_AsyncExportTask* Task = UTG_AsyncExportTask::TG_AsyncExportTask(TextureGraphInstance, bOverwriteTextures, bSave, bExportAll, bDisableCache);
			Task->ActivateBlocking(nullptr);
		}

		for (const TPair<FName, TSoftObjectPtr<UTexture>>& GeneratedTexturePair : GeneratedTextures)
		{
			FName TextureName = GeneratedTexturePair.Key;
			TSoftObjectPtr<UTexture> GeneratedTexture = GeneratedTexturePair.Value;

			if (UTexture2D* ActualTexture = Cast<UTexture2D>(GeneratedTexture.LoadSynchronous()))
			{
				FImage Image;
				if (FImageUtils::GetTexture2DSourceImage(ActualTexture, Image))
				{
					if (TextureName == TEXT("Out_Face_BaseColor_Skin"))
					{
						OutFaceImages.Emplace(EFaceTextureType::Basecolor, Image);
					}
					else if (TextureName == TEXT("Out_Face_Normal_Skin"))
					{
						OutFaceImages.Emplace(EFaceTextureType::Normal, Image);
					}
					else if (TextureName == TEXT("Out_Body_BaseColor_Skin"))
					{
						OutBodyBaseColorImage = Image;
					}
					else if (TextureName == TEXT("Out_Body_Normal_Skin"))
					{
						OutBodyNormalImage = Image;
					}
					else if (TextureName == TEXT("Out_AnimatedMap_CM1_Skin"))
					{
						OutFaceImages.Emplace(EFaceTextureType::Basecolor_Animated_CM1, Image);
					}
					else if (TextureName == TEXT("Out_AnimatedMap_CM2_Skin"))
					{
						OutFaceImages.Emplace(EFaceTextureType::Basecolor_Animated_CM2, Image);
					}
					else if (TextureName == TEXT("Out_AnimatedMap_CM3_Skin"))
					{
						OutFaceImages.Emplace(EFaceTextureType::Basecolor_Animated_CM3, Image);
					}
					else if (TextureName == TEXT("Out_AnimatedMap_WM1_Skin"))
					{
						OutFaceImages.Emplace(EFaceTextureType::Normal_Animated_WM1, Image);
					}
					else if (TextureName == TEXT("Out_AnimatedMap_WM2_Skin"))
					{
						OutFaceImages.Emplace(EFaceTextureType::Normal_Animated_WM2, Image);
					}
					else if (TextureName == TEXT("Out_AnimatedMap_WM3_Skin"))
					{
						OutFaceImages.Emplace(EFaceTextureType::Normal_Animated_WM3, Image);
					}
					else if (TextureName == TEXT("Out_BaseColor_Eye"))
					{
						OutEyeColorImage = Image;
					}
					else if (TextureName == TEXT("Out_Face_SRMF_Skin"))
					{
						OutFaceSRMFImage = Image;
					}
					else if (TextureName == TEXT("Out_Body_SRMF_Skin"))
					{
						OutBodySRMFImage = Image;
					}
					else if (TextureName == TEXT("Out_BaseColor_Teeth_sRGB"))
					{
						OutTeethColorImage = Image;
					}
					else if (TextureName == TEXT("Out_Normal_Teeth"))
					{
						OutTeethNormalImage = Image;
					}
				}
				else
				{
					FMessageLog(UE::MetaHuman::MessageLogName)
						.Error(FText::Format(LOCTEXT("DCCExportFailure_NoSourceData", "No source data for the generated baked texture '{0}'"), FText::FromName(TextureName)))
						->AddToken(FUObjectToken::Create(ActualTexture));
					bResult = false;
				}
			}
			else
			{
				FMessageLog(UE::MetaHuman::MessageLogName)
					.Error(FText::Format(LOCTEXT("DCCExportFailure_FailedToLoadGeneratedTexture", "Couldn't find baked texture '{0}'. This should have been produced by the texture graph."),
						   FText::FromName(TextureName)));
				bResult = false;
			}
		}

		return bResult;
	}
	static bool ExportBakedTextures(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FMetaHumanCharacterGeneratedAssets& InGeneratedAssets, TArray<FPendingImageWrite>& OutPendingWrites, const FString& InMapsFolder, const FString& InTempAssetFolderPath, bool bInBakeMakeup, const FString& OutputFolder, FMetaHumanExportDCCManifest& OutManifest)
	{
		TMap<EFaceTextureType, FImage> FaceTextures;
		FImage BodyBaseColorTexture;
		FImage BodyNormalTexture;
		FImage EyeColorTexture;
		FImage FaceSRMFTexture;
		FImage BodySRMFTexture;
		FImage TeethColorTexture;
		FImage TeethNormalTexture;
		if (!BakeTextures(InMetaHumanCharacter, InGeneratedAssets, FaceTextures, BodyBaseColorTexture, BodyNormalTexture, EyeColorTexture, FaceSRMFTexture, BodySRMFTexture, TeethColorTexture, TeethNormalTexture, InTempAssetFolderPath, bInBakeMakeup))
		{
			return false;
		}

		// Each queue records its manifest entry only after the file is actually written, via OnSuccess.
		auto QueuePNG = [&OutPendingWrites, &OutManifest, &OutputFolder](FImage&& InImage, const FString& InMapsRelativeName, const FString& InName)
			{
				FPendingImageWrite Pending;
				Pending.Image = MoveTemp(InImage);
				Pending.ImageName = InMapsRelativeName;
				Pending.OutputFolder = OutputFolder;
				Pending.OnSuccess = [&OutManifest, FileName = InName + TEXT(".png")]()
					{
						OutManifest.Files.Maps.Add(FileName);
					};
				OutPendingWrites.Add(MoveTemp(Pending));
			};

		// Write the Face Textures
		for (const TPair<EFaceTextureType, FImage>& FaceTexturePair : FaceTextures)
		{
			const EFaceTextureType TextureType = FaceTexturePair.Key;
			const FImage& FaceTextureImage = FaceTexturePair.Value;

			const FString TextureTypeName = StaticEnum<EFaceTextureType>()->GetAuthoredNameStringByValue(static_cast<int64>(TextureType));
			const FString FaceTextureName = TEXT("Head_") + TextureTypeName;
			FImage FaceTextureCopy = FaceTextureImage;
			QueuePNG(MoveTemp(FaceTextureCopy), InMapsFolder / FaceTextureName, FaceTextureName);
		}

		// Write the Body basecolor texture
		const FString BodyBaseColorName = StaticEnum<EBodyTextureType>()->GetAuthoredNameStringByValue(static_cast<int64>(EBodyTextureType::Body_Basecolor));
		QueuePNG(MoveTemp(BodyBaseColorTexture), InMapsFolder / BodyBaseColorName, BodyBaseColorName);

		// Write the Body normal texture
		const FString BodyNormalName = StaticEnum<EBodyTextureType>()->GetAuthoredNameStringByValue(static_cast<int64>(EBodyTextureType::Body_Normal));
		QueuePNG(MoveTemp(BodyNormalTexture), InMapsFolder / BodyNormalName, BodyNormalName);

		// Write the eye color texture
		const FString EyesColorName = TEXT("Eyes_Color");
		QueuePNG(MoveTemp(EyeColorTexture), InMapsFolder / EyesColorName, EyesColorName);

		// Write the face SRMF texture
		const FString HeadSRMFName = TEXT("Head_SRMF");
		QueuePNG(MoveTemp(FaceSRMFTexture), InMapsFolder / HeadSRMFName, HeadSRMFName);

		// Write the body SRMF texture
		const FString BodySRMFName = TEXT("Body_SRMF");
		QueuePNG(MoveTemp(BodySRMFTexture), InMapsFolder / BodySRMFName, BodySRMFName);

		// Write the teeth color texture
		const FString TeethColorName = TEXT("Teeth_Color");
		QueuePNG(MoveTemp(TeethColorTexture), InMapsFolder / TeethColorName, TeethColorName);

		// Write the teeth normal texture
		const FString TeethNormalName = TEXT("Teeth_Normal");
		QueuePNG(MoveTemp(TeethNormalTexture), InMapsFolder / TeethNormalName, TeethNormalName);

		return true;
	}
	
	static bool ExportUnmodifiedTextures(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, TSharedPtr<FZipArchiveWriter> ArchiveWriter, const FString& MapsFolder, const FString& DCCRootPath, const FString& OutputFolder, TArray<FPendingImageWrite>& OutPendingWrites, FMetaHumanExportDCCManifest& OutManifest)
	{
		auto AddSourceTextureToArchive = [&OutPendingWrites, &OutputFolder, &OutManifest](const FString& TextureTypeName, UTexture2D* Texture, const FString& InManifestFileName) -> bool
			{
				FImage TextureImage;
				if (Texture && FImageUtils::GetTexture2DSourceImage(Texture, TextureImage))
				{
					FPendingImageWrite Pending;
					Pending.Image = MoveTemp(TextureImage);
					Pending.ImageName = TextureTypeName;
					Pending.OutputFolder = OutputFolder;
					Pending.OnSuccess = [&OutManifest, ManifestFileName = InManifestFileName]()
						{
							OutManifest.Files.Maps.Add(ManifestFileName);
						};
					OutPendingWrites.Add(MoveTemp(Pending));
					return true;
				}

				const FText Message = FText::Format(LOCTEXT("DCCExportFailure_LoadTextureSource", "Failed to load source data for texture {0}."), FText::FromString(TextureTypeName));
				FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);
				return false;
			};

		// Eyes, TODO: use texture graph to get the actively selected eye textures instead of the default textures
		const FString EyesNormalTexturePath = DCCRootPath / TEXT("Defaults/Maps/Eyes_Normal.png");
		bool bResult = true;
		if (FPaths::FileExists(EyesNormalTexturePath))
		{
			// WriteToArchive writes immediately, so the manifest entry can be recorded as soon as the call succeeds.
			bResult = WriteToArchive(EyesNormalTexturePath, DCCRootPath / TEXT("Defaults/"), ArchiveWriter, OutputFolder);
			if (bResult)
			{
				OutManifest.Files.Maps.Add(TEXT("Eyes_Normal.png"));
			}
		}
		else
		{
			const FText Message = FText::Format(LOCTEXT("DCCExportWarning_SkippingEyesNormal", "Skipping Eyes_Normal export: file does not exist at '{0}'"), FText::FromString(EyesNormalTexturePath));
			FMessageLog(UE::MetaHuman::MessageLogName).Warning(Message);
		}

		// Eyelashes opacity mask based on the character's selected eyelash type
		if (InMetaHumanCharacter->HeadModelSettings.Eyelashes.Type != EMetaHumanCharacterEyelashesType::None)
		{
			bResult &= AddSourceTextureToArchive(MapsFolder / TEXT("Eyelashes_Color"), FMetaHumanCharacterSkinMaterials::GetEyelashesMask(InMetaHumanCharacter->HeadModelSettings.Eyelashes), TEXT("Eyelashes_Color.png"));
		}

		return bResult;
	}

	static bool AddManifestToArchive(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, TSharedPtr<FZipArchiveWriter> ArchiveWriter, const FString& OutputFolder, FMetaHumanExportDCCManifest& InOutManifest)
	{
		const FString MetaHumanAssetName = InMetaHumanCharacter->GetName();

		// Fill the fields owned by the manifest writer; per-file fields are populated by the individual exporters.
		InOutManifest.MetaHumanName = MetaHumanAssetName;
		InOutManifest.ExportPluginVersion = IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME)->GetDescriptor().VersionName;
		InOutManifest.ExportEngineVersion = FEngineVersion::Current().ToString();
		InOutManifest.ExportedAt = FDateTime::Now();

		// Maps come in via two paths (baked + unmodified) so insertion order is mixed; sort for stable presentation.
		InOutManifest.Files.Maps.Sort();

		FString JsonString;
		if (!FJsonObjectConverter::UStructToJsonObjectString(InOutManifest, JsonString))
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_ManifestParse", "Failed to parse manifest to json."));
			return false;
		}

		auto Convert = StringCast<ANSICHAR>(*JsonString);
		TConstArrayView<uint8, int32> JsonView(reinterpret_cast<const uint8*>(Convert.Get()), Convert.Length());

		if (ArchiveWriter)
		{
			ArchiveWriter->AddFile("ExportManifest.json", JsonView, FDateTime::Now());
		}
		else
		{
			FString JsonPath = OutputFolder / TEXT("ExportManifest.json");
			if (!FFileHelper::SaveArrayToFile(JsonView, *JsonPath))
			{
				const FText Message = FText::Format(LOCTEXT("DCCExportFailure_SaveJson", "Failed to save json file {0}."), FText::FromString(JsonPath));
				FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);
				return false;
			}
		}

		return true;
	}

	// Add a face thumbnail
	static bool AddThumbnailToArchive(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, TArray<FPendingImageWrite>& OutPendingWrites, const FString& OutputFolder, FMetaHumanExportDCCManifest& OutManifest)
	{
		const FString MetaHumanAssetName = InMetaHumanCharacter->GetName();
		const UMetaHumanCharacter* CharacterConstPtr = InMetaHumanCharacter;

		// Render a thumbnail for the face with higher resolution than the default one
		UMetaHumanCharacterThumbnailRenderer* ThumbnailRenderer = nullptr;
		if (FThumbnailRenderingInfo* RenderInfo = UThumbnailManager::Get().GetRenderingInfo(const_cast<UMetaHumanCharacter*>(CharacterConstPtr)))
		{
			ThumbnailRenderer = Cast<UMetaHumanCharacterThumbnailRenderer>(RenderInfo->Renderer);
		}

		if (ThumbnailRenderer)
		{
			const uint32 Resolution = 1024;

			// Set the renderer camera position to 
			EMetaHumanCharacterThumbnailCameraPosition CurrentCameraPosition = ThumbnailRenderer->CameraPosition;
			ThumbnailRenderer->CameraPosition = EMetaHumanCharacterThumbnailCameraPosition::Character_Face;

			FObjectThumbnail CharacterThumbnail;
			ThumbnailTools::RenderThumbnail(
				const_cast<UMetaHumanCharacter*>(CharacterConstPtr),
				Resolution, Resolution,
				ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush,
				nullptr,
				&CharacterThumbnail);

			// Thumbnail rendering enqueues a rendering command, wait until it's complete
			FlushRenderingCommands();

			FImage ThumbnailImage;
			CharacterThumbnail.GetImage().CopyTo(ThumbnailImage);

			FPendingImageWrite Pending;
			Pending.Image = MoveTemp(ThumbnailImage);
			Pending.ImageName = MetaHumanAssetName;
			Pending.OutputFolder = OutputFolder;
			Pending.OnSuccess = [&OutManifest, ThumbnailFileName = MetaHumanAssetName + TEXT(".png")]()
				{
					OutManifest.Thumbnail = ThumbnailFileName;
				};
			OutPendingWrites.Add(MoveTemp(Pending));

			// Restore the camera position
			ThumbnailRenderer->CameraPosition = CurrentCameraPosition;

			return true;
		}

		FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_GenerateThumbnail", "Failed to generate thumbnail"));
		return false;
	}
	
	static bool ExportCharacterForDCC(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter, const FMetaHumanCharacterEditorDCCExportParameters& InExportParams)
	{
		if (InExportParams.OutputFolderPath.IsEmpty())
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_OutputFolderEmpty", "Output folder not specified."));
			return false;
		}
		const FString AbsPluginContentDir = FPaths::ConvertRelativePathToFull(IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME)->GetContentDir());
		const FString DCCRootPath = AbsPluginContentDir / TEXT("Optional/DCC");
		const FString MapsFolder = TEXT("Maps");
		const FString MasksFolder = TEXT("Masks");
		const FString CharacterName = InMetaHumanCharacter->GetName();
		const FString CharacterPath = InMetaHumanCharacter->GetPathName();
		// Project path for temporary assets used during the DCC export.
		// /Temp/ mount maps to ProjectSavedDir(), hidden from normal Content Browser views and never persisted to /Game/.
		const FString TempAssetFolderPath = FString(TEXT("/Temp/" UE_PLUGIN_NAME "/DCCExport/")) / CharacterName;

		const FString OutputFolder = InExportParams.OutputFolderPath / CharacterName;
		TSharedPtr<FZipArchiveWriter> ArchiveWriter = nullptr;
		if (InExportParams.bExportZipFile)
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			if (!PlatformFile.DirectoryExists(*InExportParams.OutputFolderPath))
			{
				PlatformFile.CreateDirectoryTree(*InExportParams.OutputFolderPath);
			}

			// Check if the archive file path is set and valid
			FString ArchivePath = InExportParams.OutputFolderPath / (InExportParams.ArchiveName.IsEmpty() ? CharacterName : InExportParams.ArchiveName);
			if (FPaths::FileExists(ArchivePath))
			{
				const FText Message = FText::Format(LOCTEXT("DCCExportFailure_ArchiveFileExists", "File {0} exists."), FText::FromString(ArchivePath));
				FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);
				return false;
			}

			if (!FPaths::GetExtension(ArchivePath).Equals("zip", ESearchCase::IgnoreCase))
			{
				ArchivePath = FPaths::SetExtension(ArchivePath, "zip");
			}

			IFileHandle* ArchiveFile = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*ArchivePath);
			if (!ArchiveFile)
			{
				const FText Message = FText::Format(LOCTEXT("DCCExportFailure_CannotOpenArchive", "Failed creating archive {0}."), FText::FromString(ArchivePath));
				FMessageLog(UE::MetaHuman::MessageLogName).Error(Message);
				return false;
			}

			// The zip writer closes the file handle
			ArchiveWriter = MakeShared<FZipArchiveWriter>(ArchiveFile);
		}
		else
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			// Make sure the destination folder exists.
			if (!PlatformFile.DirectoryExists(*OutputFolder))
			{
				PlatformFile.CreateDirectoryTree(*OutputFolder);
			}			
		}	

		FScopedSlowTask ExportDCCTask(6, LOCTEXT("DCCExport_ExportCharacterTaskMessage", "Exporting MetaHuman Character asset for DCC"));
		ExportDCCTask.MakeDialog();

		// Manifest accumulates filenames of everything written below; the manifest itself is serialized last.
		FMetaHumanExportDCCManifest Manifest;
		Manifest.Folders.Maps = MapsFolder;
		Manifest.Folders.Masks = MasksFolder;

		FMetaHumanCharacterGeneratedAssets GeneratedAssets;
		if (!UMetaHumanCharacterEditorSubsystem::Get()->TryGenerateCharacterAssets(InMetaHumanCharacter, GetTransientPackage(), GeneratedAssets))
		{
			FMessageLog(UE::MetaHuman::MessageLogName).Error(LOCTEXT("DCCExportFailure_GenerateAssets", "Failed to generate character assets for DCC export"));
			return false;
		}
		ExportDCCTask.EnterProgressFrame();

		// Face and body DNA
		if (!UE::MetaHuman::ExportDNAFiles(InMetaHumanCharacter, ArchiveWriter, OutputFolder, Manifest))
		{
			return false;
		}
		ExportDCCTask.EnterProgressFrame();

		// Collect all image writes and flush them in parallel at the end
		TArray<FPendingImageWrite> PendingImageWrites;

		if (!UE::MetaHuman::ExportBakedTextures(InMetaHumanCharacter, GeneratedAssets, PendingImageWrites, MapsFolder, TempAssetFolderPath, InExportParams.bBakeFaceMakeup, OutputFolder, Manifest))
		{
			return false;
		}

		ExportDCCTask.EnterProgressFrame();

		if (!UE::MetaHuman::ExportUnmodifiedTextures(InMetaHumanCharacter, ArchiveWriter, MapsFolder, DCCRootPath, OutputFolder, PendingImageWrites, Manifest))
		{
			return false;
		}
		ExportDCCTask.EnterProgressFrame();

		// Copy common assets for all DCC exports
		if (!UE::MetaHuman::ExportCommonSourceAssets(DCCRootPath, ArchiveWriter, OutputFolder, MasksFolder, Manifest.Files.Masks))
		{
			return false;
		}
		ExportDCCTask.EnterProgressFrame();

		// Add thumbnail
		if (!UE::MetaHuman::AddThumbnailToArchive(InMetaHumanCharacter, PendingImageWrites, OutputFolder, Manifest))
		{
			return false;
		}

		// Flush all pending image writes with parallel PNG compression
		if (!UE::MetaHuman::FlushPendingImageWrites(PendingImageWrites, ArchiveWriter))
		{
			return false;
		}

		// Write the manifest last so it reflects everything that was actually exported.
		if (!UE::MetaHuman::AddManifestToArchive(InMetaHumanCharacter, ArchiveWriter, OutputFolder, Manifest))
		{
			return false;
		}

		return true;
	}
} // namespace UE::MetaHuman


void FMetaHumanCharacterEditorDCCExport::ExportCharacterForDCC(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, const FMetaHumanCharacterEditorDCCExportParameters& InExportParams)
{
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	MessageLogModule.GetLogListing(UE::MetaHuman::MessageLogName)->ClearMessages();

	const bool bWasSuccessful = UE::MetaHuman::ExportCharacterForDCC(InMetaHumanCharacter, InExportParams);
	const FText SuccessMessageText = LOCTEXT("CharacterDCCExportSucceeded", "MetaHuman Character DCC export succeeded");
	const FText FailureMessageText = LOCTEXT("CharacterDCCExportFailed", "MetaHuman Character DCC export failed");

	FMetaHumanCharacterEditorBuild::ReportMessageLogErrors(bWasSuccessful, SuccessMessageText, FailureMessageText);
}

#undef LOCTEXT_NAMESPACE
