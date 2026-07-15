// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ExportMaterialProxy.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "AssetCompilingManager.h"

/**
 * Regression test for FORT-1072299: FExportMaterialProxy crashes during HLOD build.
 *
 * CL 51712304 added checkf assertions in FMaterialShaderMap::SubmitCompileJobs that verify
 * all shader types in the layout are present in the ShaderMapId dependencies. FExportMaterialProxy
 * has an overly broad ShouldCache (accepts any vertex/pixel shader on FLocalVertexFactory) that
 * causes shaders to pass ShouldCache in SubmitCompileJobs even when they were not included in
 * the ShaderMapId by GetDependentShaderAndVFTypes (which uses the same ShouldCache but a
 * potentially different layout due to conservative vs compilation shader parameters).
 *
 * This test creates a minimal FExportMaterialProxy and verifies it compiles without assertion.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FExportMaterialProxyAnisotropyTest,
	"MaterialBaking.ExportMaterialProxy.ShaderDependencyConsistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExportMaterialProxyAnisotropyTest::RunTest(const FString& Parameters)
{
	UMaterial* Material = NewObject<UMaterial>();

	// BaseColor: constant grey
	UMaterialExpressionConstant3Vector* BaseColorExpr = NewObject<UMaterialExpressionConstant3Vector>(Material);
	BaseColorExpr->Constant = FLinearColor(0.5f, 0.5f, 0.5f);
	Material->GetEditorOnlyData()->BaseColor.Connect(0, BaseColorExpr);
	Material->GetExpressionCollection().AddExpression(BaseColorExpr);

	Material->SetShadingModel(MSM_DefaultLit);

	// Compile the material synchronously
	Material->PostEditChange();
	FAssetCompilingManager::Get().FinishAllCompilation();

	FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
	if (!TestNotNull(TEXT("Material resource must exist after compilation"), Resource))
	{
		return false;
	}

	// Create FExportMaterialProxy for BaseColor baking.
	// This exercises the code path through SubmitCompileJobs where the checkf on
	// ShaderMapId.ContainsShaderType fires if the proxy's ShouldCache/layout are inconsistent.
	FExportMaterialProxy* Proxy = new FExportMaterialProxy(
		Material,
		MP_BaseColor,
		TEXT(""),
		/*bInSynchronousCompilation=*/ true,
		/*bTangentSpaceNormal=*/ false,
		BLEND_Opaque,
		/*bAllowPixelDepthOffset=*/ false);

	// The proxy constructor calls CacheShaders -> SubmitCompileJobs. If the ShaderMapId is
	// missing any shader type that the layout includes, a checkf fires and aborts the process.
	// Reaching this point without crashing validates that the shader dependencies are consistent.
	TestTrue(TEXT("FExportMaterialProxy compiled without shader dependency assertion"), true);

	// Clean up the proxy using the same pattern as FMaterialBakingModule::CleanupMaterialProxies
	TArray<FMaterial*> ResourcesToFree;
	ResourcesToFree.Add(Proxy);
	FMaterial::DeferredDeleteArray(ResourcesToFree);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
