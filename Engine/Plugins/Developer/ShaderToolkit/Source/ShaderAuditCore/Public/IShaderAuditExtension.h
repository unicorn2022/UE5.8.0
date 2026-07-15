// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeatures.h"
#include "Templates/Function.h"

struct FShaderAuditSession;
class SWidget;

/**
 * A single contribution an extension makes to the session view UI.
 * An extension may return multiple contributions (e.g. a toolbar button AND a view tab).
 */
struct FShaderAuditExtensionContribution
{
	/** If non-empty, a view tab button with this label is added to the toolbar and
	 *  a corresponding slot is created in the view switcher. */
	FText ViewTabLabel;

	/** Factory for the view tab widget. Required when ViewTabLabel is set. */
	TFunction<TSharedRef<SWidget>(TSharedPtr<FShaderAuditSession>)> CreateViewWidget;

	/** Factory for a widget appended to the toolbar. May be null. */
	TFunction<TSharedRef<SWidget>(TSharedPtr<FShaderAuditSession>)> CreateToolbarWidget;
};

/**
 * Extension point for the ShaderAudit session view.
 *
 * Modules implement this interface and register via IModularFeatures to contribute
 * toolbar widgets, view tabs, and session lifecycle behavior to ShaderAudit -- without
 * any compile-time dependency from ShaderAudit to the extension module.
 *
 * Registration:
 *   IModularFeatures::Get().RegisterModularFeature(IShaderAuditExtension::FeatureName, this);
 *
 * The session view discovers all registered extensions and integrates their contributions.
 * Late registration (after the view is already constructed) is supported.
 */
class IShaderAuditExtension : public IModularFeature
{
public:
	static inline const FName FeatureName = TEXT("ShaderAuditExtension");

	virtual ~IShaderAuditExtension() = default;

	/** Unique identifier for this extension (used as key in ExtensionData maps). */
	virtual FName GetExtensionId() const = 0;

	/** Human-readable name shown in UI (e.g. tooltips). */
	virtual FText GetDisplayName() const = 0;

	/** Return the UI contributions this extension makes to each session view. */
	virtual TArray<FShaderAuditExtensionContribution> GetContributions() const = 0;

	/** Called when a session finishes loading. Use to auto-attach data, kick off background work, etc. */
	virtual void OnSessionLoaded(TSharedPtr<FShaderAuditSession> Session) {}

	/** Called when a session is about to be closed. Clean up any external state. */
	virtual void OnSessionClosed(int32 SessionId) {}
};
