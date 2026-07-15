// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AES.h"

/**
 * Unified config helpers for IoStoreDependencyViewer
 * Centralizes path resolution for zen.exe, OidcToken.exe, and related config
 */
namespace IoStoreConfig
{
	/**
	 * Find zen.exe path with fallback search:
	 * 1. Config setting (Engine.ini [IoStoreDependencyViewer] ZenExePath)
	 * 2. Application directory
	 * 3. Project directory (if -projectdir= specified)
	 * @param OutPath - Resolved path (may not exist - use ValidateZenExe to check)
	 * @return true if a path was resolved (not validated for existence)
	 */
	bool FindZenExePath(FString& OutPath);

	/**
	 * Find OidcToken.exe path with fallback search:
	 * 1. Config setting (Engine.ini [IoStoreDependencyViewer] OidcTokenExePath)
	 * 2. ../DotNET/OidcToken/win-x64/OidcToken.exe (relative to app directory)
	 * 3. Project directory (if -projectdir= specified)
	 * 4. Same directory as zen.exe (if provided)
	 * @param OutPath - Resolved path (may not exist)
	 * @param ZenExePath - Optional zen.exe path to check alongside (for fallback #4)
	 * @return true if a path was resolved
	 */
	bool FindOidcTokenExePath(FString& OutPath, const FString& ZenExePath = FString());

	/**
	 * Validate that zen.exe exists and optionally show error dialog if missing
	 * @param ZenExePath - Path to validate
	 * @param bShowDialog - Show error dialog to user (default true)
	 * @return true if zen.exe exists
	 */
	bool ValidateZenExe(const FString& ZenExePath, bool bShowDialog = true);

	/**
	 * Validate that OidcToken.exe exists (warning only - not fatal)
	 * @param OidcExePath - Path to validate
	 * @param bShowWarning - Show warning if missing (default true)
	 * @return true if OidcToken.exe exists
	 */
	bool ValidateOidcTokenExe(const FString& OidcExePath, bool bShowWarning = true);

	/**
	 * Escapes a string for safe use as a command-line argument value on Windows.
	 * Implements proper Windows/CRT CommandLineToArgvW escaping rules:
	 * - Sequences of backslashes before quotes must be doubled, then the quote escaped
	 * - Trailing backslashes (before closing quote) must be doubled
	 * - Other backslashes are literal
	 * This prevents argument injection via embedded quotes.
	 * @param Argument - The string to escape
	 * @return Escaped string safe for use in command-line arguments
	 */
	FString EscapeCommandLineArgument(const FString& Argument);

	/**
	 * Try to load crypto.json from configured search paths.
	 * Reads path templates from Engine.ini [IoStoreDependencyViewer] +CryptoJsonSearchPath entries.
	 * Templates are expanded using provided variables (e.g., <buildname>, <branchname>, <cl>, <platform>).
	 * Example template: "\\epicgames.net\Root\Builds\Fortnite\++Fortnite+<branchname>-CL-<cl>\<platform>\Metadata\crypto.json"
	 *
	 * @param TemplateVars - Map of variable names to values for template substitution (e.g., "buildname" -> "Fortnite-Main-CL-123")
	 * @param DownloadDirectory - Local directory to copy crypto.json into (under <Platform>/Metadata/)
	 * @return true if crypto.json was found and copied successfully
	 */
	bool TryLoadCryptoJsonFromSearchPaths(const TMap<FString, FString>& TemplateVars, const FString& DownloadDirectory);

	/**
	 * Parse branch name from a cloud build name.
	 * Examples: "Fortnite-Main-CL-123" → "Main", "Fortnite-Release-40.20-CL-456" → "Release-40.20"
	 * @param BuildName - Cloud build name
	 * @return Branch name, or full BuildName if parsing fails
	 */
	FString ParseBranchFromBuildName(const FString& BuildName);

	/**
	 * Parse changelist number from a cloud build name.
	 * Examples: "Fortnite-Main-CL-12345" → "12345", "Fortnite-Release-40.20-CL-456789" → "456789"
	 * @param BuildName - Cloud build name
	 * @return Changelist number string, or empty string if not found
	 */
	FString ParseChangelistFromBuildName(const FString& BuildName);

	/**
	 * Load encryption keys from crypto.json files in a directory.
	 * Searches all platform subdirectories (e.g., Windows/Metadata/crypto.json) and merges keys.
	 * @param DirectoryPath - Root directory containing platform subdirectories
	 * @param OutKeys - Map of encryption keys (Guid -> AES key)
	 * @return true if any crypto.json files were found and loaded
	 */
	bool LoadEncryptionKeysFromDirectory(const FString& DirectoryPath, TMap<FGuid, FAES::FAESKey>& OutKeys);
}
