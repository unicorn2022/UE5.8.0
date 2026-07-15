// Copyright Epic Games, Inc. All Rights Reserved.

#include "MSGamingRuntimeModule.h"
#include "GDKRuntimeModule.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_EDITOR
#include "Containers/Ticker.h"
#include "Editor.h"
#endif

#if WITH_GRDK
#include "Microsoft/AllowMicrosoftPlatformTypes.h"
#include <XPackage.h>
#include <XGameRuntimeFeature.h>
#include "Microsoft/HideMicrosoftPlatformTypes.h"
#endif

class FMSGamingRuntimeModule : public IMSGamingRuntimeModule
{
public:

	static inline const TCHAR* ConfigSection = TEXT("/Script/MSGamingSupport.MSGamingSettings");


	virtual void StartupModule() override
	{
#if WITH_GRDK && WITH_EDITOR
		// editor requires the GDK to be intitialized on startup
		EnsureInitialized();
		FEditorDelegates::PreBeginPIE.AddRaw(this, &FMSGamingRuntimeModule::OnBeginPIE);
		FEditorDelegates::EndPIE.AddRaw(this, &FMSGamingRuntimeModule::OnEndPIE);
#elif WITH_GRDK
		// outside of the editor it can be deferred until the first use
		bool bLazyInitialize = true;
		GConfig->GetBool(ConfigSection, TEXT("bLazyInitialize"), bLazyInitialize, GEngineIni);
		if (!bLazyInitialize)
		{
			EnsureInitialized();
		}
#else
		// otherwise there's no GRDK support compiled in this build
		UE_LOGF(LogInit, Warning, "This title was not built with GDK support");
		bIsInitialized = true;
#endif // WITH_GRDK

	}


	virtual void ShutdownModule() override
	{
		bIsAvailable = false;

#if WITH_GRDK && WITH_EDITOR
		FEditorDelegates::PreBeginPIE.RemoveAll(this);
		FEditorDelegates::EndPIE.RemoveAll(this);
#endif
	}


	virtual bool IsAvailable() const override
	{
#if WITH_GRDK
		EnsureInitialized();
#endif // WITH_GRDK

		return bIsAvailable;
	}


private:

#if WITH_GRDK && WITH_EDITOR
	void OnBeginPIE(const bool)
	{
		bool bRestartRuntimeForPIE = true;
		if (GConfig->GetBool(ConfigSection, TEXT("bRestartRuntimeForPIE"), bRestartRuntimeForPIE, GEngineIni ) && bRestartRuntimeForPIE)
		{
			if (bIsAvailable)
			{
				UE_LOGF(LogGDK, Log, "Shutting down previous GDK environment for PIE");
				IGDKRuntimeModule::Get().Internal_TeardownForPIE();
			}

			UE_LOGF(LogGDK, Log, "Initializing GDK environment for PIE");
			IGDKRuntimeModule::Get().Internal_InitForPIE();
			bIsAvailable = IGDKRuntimeModule::Get().IsAvailable();
			bIsInitialized = true;
		}
	}

	void OnEndPIE(const bool)
	{
		bool bRestartRuntimeForPIE = true;
		if (GConfig->GetBool(ConfigSection, TEXT("bRestartRuntimeForPIE"), bRestartRuntimeForPIE, GEngineIni ) && bRestartRuntimeForPIE)
		{
			ExecuteOnGameThread(UE_SOURCE_LOCATION, [this]()
			{
				UE_LOGF(LogGDK, Log, "Shutting down GDK environment for PIE");
				IGDKRuntimeModule::Get().Internal_TeardownForPIE();
				bIsAvailable = false;
			});
		}
	}
#endif //WITH_GRDK && WITH_EDITOR

#if WITH_GRDK
	void EnsureInitialized() const
	{
		if (!bIsInitialized)
		{
			FModuleManager::Get().LoadModuleChecked(TEXT("GDKRuntime"));
			bIsAvailable = IGDKRuntimeModule::Get().IsAvailable();

			bIsInitialized = true;
		}
	}
#endif // WITH_GRDK


	mutable bool bIsInitialized = false;
	mutable bool bIsAvailable = false;
};

IMPLEMENT_MODULE(FMSGamingRuntimeModule, MSGamingRuntime);
