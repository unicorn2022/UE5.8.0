// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformApplicationMisc.h"

struct FLinuxPlatformApplicationMisc : public FGenericPlatformApplicationMisc
{
	static APPLICATIONCORE_API void PreInit();
	static APPLICATIONCORE_API void Init();
	static APPLICATIONCORE_API bool InitSDL();
	static APPLICATIONCORE_API void TearDown();
	static APPLICATIONCORE_API void LoadPreInitModules();
	static APPLICATIONCORE_API void LoadStartupModules();
	static APPLICATIONCORE_API uint64 WindowStyle();
	static APPLICATIONCORE_API class FOutputDeviceConsole* CreateConsoleOutputDevice();
	static APPLICATIONCORE_API class FOutputDeviceError* GetErrorOutputDevice();
	static APPLICATIONCORE_API class FFeedbackContext* GetFeedbackContext();
	static APPLICATIONCORE_API class GenericApplication* CreateApplication();
	static APPLICATIONCORE_API bool IsThisApplicationForeground();
	static APPLICATIONCORE_API void PumpMessages(bool bFromMainLoop);
	static APPLICATIONCORE_API bool IsScreensaverEnabled();
	static APPLICATIONCORE_API bool ControlScreensaver(EScreenSaverAction Action);
	static APPLICATIONCORE_API float GetDPIScaleFactorAtPoint(float X, float Y);
	static APPLICATIONCORE_API void ClipboardCopy(const TCHAR* Str);
	static APPLICATIONCORE_API void ClipboardPaste(class FString& Dest);
	static bool FullscreenSameAsWindowedFullscreen() { return true; }
	static APPLICATIONCORE_API bool IsUsingWayland();			// Is SDL using native Wayland video driver
	static APPLICATIONCORE_API bool IsUsingX11();			    // Is SDL using X11 video driver

	// Unix specific
	static APPLICATIONCORE_API void EarlyUnixInitialization(class FString& OutCommandLine);
	static bool ShouldIncreaseProcessLimits() { return true; }

	/** Queue SDL_DestroyWindow for 3 PumpMessages ticks so the RHI thread can drain. */
	static APPLICATIONCORE_API void DeferDestroyWindow(struct SDL_Window* Window);

	/** Queue SDL_HideWindow for 3 PumpMessages ticks. Caller must set
	 *  UE.window.present_enabled=false on the window first. */
	static APPLICATIONCORE_API void DeferHideWindow(struct SDL_Window* Window);

	/** Remove a window from the deferred-hide queue. No-op if not queued. */
	static APPLICATIONCORE_API void CancelDeferredHideWindow(struct SDL_Window* Window);

	// Linux specific
	/** Informs ApplicationCore that it needs to create Vulkan-compatible windows (mutually exclusive with OpenGL) */
	static APPLICATIONCORE_API void UsingVulkan();
	/** Informs ApplicationCore that it needs to create OpenGL-compatible windows (mutually exclusive with Vulkan) */
	static APPLICATIONCORE_API void UsingOpenGL();
};

typedef FLinuxPlatformApplicationMisc FPlatformApplicationMisc;
