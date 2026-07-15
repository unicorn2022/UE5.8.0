// Copyright Epic Games, Inc. All Rights Reserved.

namespace EpicGames.OIDC;

/// <summary>
/// Configuration for a <see cref="HttpOidcAuthHandler"/>.
/// </summary>
public sealed record HttpOidcAuthHandlerConfig(
	string AllowInteractiveLoginHeaderKey,
	bool AllowInteractiveLogin = true,
	string AuthenticationScheme = "Bearer",
	int MaxAttempts = 3
);
