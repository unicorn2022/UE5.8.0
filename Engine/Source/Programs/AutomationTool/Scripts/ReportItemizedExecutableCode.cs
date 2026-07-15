// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using UnrealBuildBase;
using UnrealBuildTool;

namespace ReportItemizedExecutableCode.Automation
{
	struct ModuleInfo
	{
		/** Module Name */
		public string Name;
		
		/** Module ShortName, null if there isn't one. */
		[JsonPropertyName("shortName")]
		public string ShortName { get; set; }

		/** Source directory for the module. Relative to project root. */
		[JsonPropertyName("moduleDirectory")]
		public string ModuleDirectory { get; set; }

		/** Intermediate directory for the module (ie. where the unity modules are put). Relative to project root. */
		[JsonPropertyName("intermediateDirectory")]
		public string IntermediateDirectory { get; set; }

		public bool IsEngineModule => ModuleDirectory.StartsWith("Engine");
		public bool IsEnginePlugin => IsEngineModule && ModuleDirectory.Contains("\\Plugins\\");
		public bool IsGameModule => !IsEngineModule;
		public bool IsGamePlugin => IsGameModule && ModuleDirectory.Contains("\\Plugins\\");
		public bool IsGameFeaturePlugin => IsGamePlugin && ModuleDirectory.Contains("\\GameFeatures\\");
	}

	struct ModuleResult
	{
		/** Resolved module name. */
		public string Name;
		/** Resolved module info if found. Will be null if we had to fallback to parsing the module name from the path. */
		public ModuleInfo? ModuleInfo;
	}

	struct ModuleInfos
	{
		/** Holds the actual itemized data (module to its bytes) */
		public Dictionary<string, uint> Modules;

		/** Holds the mappings - which sources we categorized as belonging to this module (for debugging) */
		public Dictionary<string, List<string>> ModuleNameToSources;

		/** Which sources we couldn't classify */
		public List<string> SourcesWithoutModule;

		/** Total size of all compileunits. */
		public uint Total = 0;

		/** Size of sources that we couldn't classify */
		public uint TotalUnclassified = 0;

		/** Size of engine sources */
		public uint TotalEngineSize = 0;

		/** Size of game sources */
		public uint TotalGameSize = 0;

		/** Size of all engine plugins. This total is captured in TotalEngineSize. */
		public uint TotalEnginePluginSize = 0;

		/** Size of all game plugins. This total is captured in TotalGameSize. */
		public uint TotalGamePluginSize = 0;

		/** Size of all game feature plugins. This total is captured in TotalGamePluginSize. */
		public uint TotalGameFeaturesSize = 0;

		/** Size coming from external libraries. */
		public uint TotalExternalLibs = 0;

		/** These are bytes that couldn't be attributed to a specific compile unit. */
		public uint TotalUnattributed = 0;

		public ModuleInfos()
		{
			Modules = new Dictionary<string, uint>();
			ModuleNameToSources = new Dictionary<string, List<string>>();
			SourcesWithoutModule = new List<string>();
		}
	};

	/// <summary>
	/// Generates itemized reports of executable and module sizes in a CSV format suitable for PerfReportServer.
	/// This tool has two main functions:
	/// 1. Itemize a single, primary executable using Bloaty (`-ExeToItemize`).
	/// 2. Scan a directory for Merged Modules (`-DllRoot`), creating a summary rollup and optional per-module Bloaty reports.
	/// </summary>
	/// <remarks>
	/// The tool is platform-aware and can be configured with various filters to target specific builds
	/// </remarks>
	/// <example>
	/// <code>
	/// // EXAMPLE 1: Legacy analysis of a single, monolithic Windows executable.
	/// // This shows the original functionality of the tool, without Merged Modules support.
	/// RunUAT.bat ReportItemizedExecutableCode ^
	///   -ExeToItemize={PathToStagedBinaries}/Win64/FortniteClient-Win64-Shipping.exe ^
	///   -SymbolsExe={PathToStagedBinaries}/Win64/FortniteClient-Win64-Shipping.pdb ^
	///   -ReportPath={PathToBuildArtifacts}/Reports
	/// </code>
	/// </example>
	/// <example>
	/// <code>
	/// // EXAMPLE 2: Typical usage for analyzing Merged Modules on Switch (packaged NROs only):
	/// RunUAT.bat ReportItemizedExecutableCode ^
	///   -ExeToItemize={PathToStagedBinaries}/Switch/FortniteClient-Switch-Shipping.nro ^
	///   -ReportPath={PathToBuildArtifacts}/Reports ^
	///   -DllRoot={PathToStagedBinaries}/Switch ^
	///   -Platform=Switch ^
	///   -PackagedOnly=true ^
	///   -ConfigFilter=Switch-Shipping ^
	///   -PerDllReports=true ^
	///   -DllReportsSubdir=DLL ^
	///   -SkipExeItemization=true
	/// </code>
	/// </example>
	/// <example>
	/// <code>
	/// // EXAMPLE 3: Typical usage for analyzing Merged Modules on Android (where per-DLL itemization works well):
	/// RunUAT.bat ReportItemizedExecutableCode ^
	///   -ExeToItemize={PathToStagedBinaries}\Android\libUnreal.so ^
	///   -ReportPath={PathToBuildArtifacts}/Reports ^
	///   -DllRoot={PathToStagedBinaries}/Android ^
	///   -Platform=Android ^
	///   -PerDllReports=true ^
	///   -DllReportsSubdir=DLL ^
	///   -SkipExeItemization=true
	/// </code>
	/// </example>

	[Help("Reports itemized binary size in a format consumed by PerfReportServer")]
	[ParamHelp("ExeToItemize", "Absolute path to the binary to itemize", ParamType = typeof(FileReference), Required = true)]
	[ParamHelp("SymbolsExe", "Absolute path to the symbolicated binary", ParamType = typeof(FileReference))]
	[ParamHelp("ReportPath", "Absolute path to the directory to write the reports to.", ParamType = typeof(DirectoryReference))]
	[ParamHelp("Platform", "Target platform for module scan: Auto|Switch|Win64|PS4|PS5|Android|iOS|Xbox (default: Auto)", ParamType = typeof(string), Required = true)]
	[ParamHelp("Project", "The Project name or path.", ParamType = typeof(string), Required = true)]
	[ParamHelp("Target", "The UBT Target.", ParamType = typeof(string), Required = true)]

	[ParamHelp("PrintResults", "Print itemization results to stdout. Defaults to true", ParamType = typeof(bool))]
	[ParamHelp("Verbose", "Print verbose output. Defaults to false", ParamType = typeof(bool))]

	// Merged Modules (optional flags)
	[ParamHelp("DllRoot", "Root folder to scan for merged modules (staged outputs). If omitted, merged-modules logic is skipped.", ParamType = typeof(DirectoryReference))]
	[ParamHelp("ModuleExts", "Override module extensions (comma-separated, e.g. .nro,.dll). Default chosen by Platform.", ParamType = typeof(string))]
	[ParamHelp("EagerPrefixes", "Override 'eager' name prefixes (semicolon-separated, e.g. EOS;KITT).", ParamType = typeof(string))]
	[ParamHelp("PackageMarkerSuffixes", "Override package marker suffixes (semicolon-separated, e.g. .nspd).", ParamType = typeof(string))]
	[ParamHelp("MergedModulesCsvName", "Output filename for merged-modules rollup CSV (default: MergedModules.csv).", ParamType = typeof(string))]
	[ParamHelp("PackagedOnly", "If true, only include modules contained in a package folder that matches 'PackageMarkerSuffixes' (e.g. .nspd).", ParamType = typeof(bool))]
	[ParamHelp("ConfigFilter", "If set, only include packages whose name contains this token (e.g. Switch-Shipping or Switch-Test).", ParamType = typeof(string))]
	[ParamHelp("PerDllReports", "If true, also run the existing bloaty 'Sections' and 'Itemized' reports per DLL, writing separate CSVs. (Note: bloaty must support the module format.)", ParamType = typeof(bool))]
	[ParamHelp("DllReportsSubdir", "Subdirectory under ReportPath for per-DLL CSVs (default: DLL).", ParamType = typeof(string))]
	[ParamHelp("DllReportsDOP", "Max degree of parallelism for per-DLL reports (default: Environment.ProcessorCount/2).", ParamType = typeof(int))]
	[ParamHelp("SkipExeItemization", "If true, do not run bloaty on the main executable; only run merged-modules logic (if DllRoot is set).", ParamType = typeof(bool))]
	class ReportItemizedExecutableCode : BuildCommand
	{
		// Verbose output
		private bool bVerbose = false;

		private List<ModuleInfo> Modules = new();

		private void VerboseLog(string Message)
		{
			if (bVerbose)
			{
				Console.WriteLine(Message);
			}
		}

		private string[] GetReportDestDirectories()
		{
			List<string> DestPaths = new List<string>()
			{
				CmdEnv.LogFolder
			};

			DirectoryReference ReportDir = ParseOptionalDirectoryReferenceParam("ReportPath");
			if (ReportDir != null)
			{
				DestPaths.Add(ReportDir.FullName);
			}
			return DestPaths.ToArray();
		}

		/** Attempts to assign a reasonable module name from a source file name */
		string ParseModuleName(string SourcePath)
		{
			// For context, here are the examples we might be dealing with:

			// ../../FooGame/Intermediate/Build/Platform/Arch/FooClient/Shipping/MovieSceneTracks\Module.MovieSceneTracks.2.cpp  ->   we want it to be name "MovieSceneTracks"
			// (general rule for that: last directory name)

			// but this does not work for non-unity modules (some are still encountered in unity builds), e.g.
			// ../Plugins/Runtime/Database/SQLiteCore/Source/SQLiteCore/Private\SQLiteEmbedded.c
			// So, the rule is amends - if we can find Source in the path, take the next path element after it (if any)

			// But that does not work for a path like
			// D:/foo/DevAudio/Engine/Source/ThirdParty/Vorbis/libvorbis-1.3.2/Platform/../lib\vorbisenc.c -> we want it named "Vorbis" and not "lib"
			// So, the rule added: skip "ThirdParty" when moving away from Source

			// Again a bad path where taking the directory after "source" is a bad choice:
			// D:/P4Libs/depot/3rdParty/libwebsocket/source/2.2/libwebsockets_src/lib\client-parser.c -> we want it named "libwebsocket" and not "2.2"
			// Solution: look for "3rdParty" (a well known path locally) before looking for Source

			// But this agains fails for foreign strings that don't contain any
			// D:\home\teamcity\work\sdk\Externals\curl\lib\if2ip.c
			// For this, we apply the following heurstics: if last directory name is a name like "lib" or "src", take the pre-last directory

			// so the algo becomes:
			// look for "3rdParty" in the path. Found?  Take the next path element, if any, otherwise take "3rdParty". If not found, continue
			// look for "Source" in the path. Found?  Take the next path element, if any, otherwise take "3rdParty". If not found, continue
			// Take the last directory. Is it "lib", "src" or a few other names that we don't think are a good module name?  If yes, keep walking up

			string[] PathElements = SourcePath.Split(new char[] { '\\', '/' }, StringSplitOptions.RemoveEmptyEntries);

			// look for Source for known Unreal names (both non-unity files and ThirdParty libs will be found)
			for (int IdxElem = 0; IdxElem < PathElements.Length; ++IdxElem)
			{
				string CandidateName = PathElements[IdxElem];
				if (CandidateName.Equals("3rdParty", StringComparison.InvariantCultureIgnoreCase))
				{
					if (IdxElem < PathElements.Length - 2)  // last element is the file name, don't want to return that
					{
						CandidateName = PathElements[IdxElem + 1];

						// sometimes people build 3rdParty in a 3rdParty folder, e.g. D:/perforce/3rdParty/3rdParty/libwebsocket/source/2.2/libwebsockets_src/lib\libwebsockets.c
						if (CandidateName.Equals("3rdParty", StringComparison.InvariantCultureIgnoreCase))
						{
							if (IdxElem < PathElements.Length - 3)  // last element is the file name, don't want to return that
							{
								CandidateName = PathElements[IdxElem + 2];
							}
						}
					}

					return CandidateName;
				}
				else if (CandidateName.Equals("Source", StringComparison.InvariantCultureIgnoreCase))
				{
					if (IdxElem < PathElements.Length - 2)  // last element is the file name, don't want to return that
					{
						CandidateName = PathElements[IdxElem + 1];
						if (CandidateName.Equals("ThirdParty", StringComparison.InvariantCultureIgnoreCase) && IdxElem < PathElements.Length - 3)
						{
							CandidateName = PathElements[IdxElem + 2];
						}
					}
					return CandidateName;
				}
			}

			// didn't find neither "3rdParty", nor "Source" string, try to walk up to the module name from behind
			if (PathElements.Length < 2)
			{
				// if source path is does not have any directories, well, then we cannot group it anyhow, let it be the module name
				return SourcePath;
			}

			string[] NamesThatCannotBeModuleNames = { "..", "src", "lib", "float", "core" };

			for (int IdxElem = PathElements.Length - 2; IdxElem >= 0; --IdxElem)
			{
				string ModuleCandidate = PathElements[IdxElem];

				bool SkipThisDirectory = false;
				foreach (string UndesirableModuleName in NamesThatCannotBeModuleNames)
				{
					if (ModuleCandidate.Equals(UndesirableModuleName, StringComparison.InvariantCultureIgnoreCase))
					{
						SkipThisDirectory = true;
						break;
					}
				}

				if (SkipThisDirectory)
				{
					continue;
				}

				return ModuleCandidate;
			}

			// some known badly compiled modules
			if (SourcePath.StartsWith("../..\\png"))
			{
				return "png";
			}

			// if we arrived here we couldn't find anything that looks like a module name. Return the whole path
			return SourcePath;
		}

		ModuleResult? FindModule(string SourcePath)
		{
			string NormalizedPath = SourcePath.Replace("/", "\\");
			string DirectoryPath = Path.GetDirectoryName(NormalizedPath);
			if (string.IsNullOrEmpty(DirectoryPath))
			{
				return null;
			}

			if (Modules != null)
			{
				// Look for the longest match to avoid "MyModuleCore" being matched to "MyModule".
				ModuleInfo? BestMatch = null;
				int LongestMatch = 0;
				foreach (ModuleInfo Module in Modules)
				{
					int MatchLength = 0;

					// The source path for unity files will by the intermediate dir.
					if (DirectoryPath.Contains(Module.IntermediateDirectory))
					{
						MatchLength = Module.IntermediateDirectory.Length;
					}

					// Whereas separately compiled libs will be the actual source location.
					if (DirectoryPath.Contains(Module.ModuleDirectory))
					{
						MatchLength = Module.ModuleDirectory.Length;
					}

					if (MatchLength > LongestMatch)
					{
						LongestMatch = MatchLength;
						BestMatch = Module;
					}
				}

				if (BestMatch != null)
				{
					// Use the short name if available for backwards compat to prevent stat names changing.
					return new ModuleResult()
					{
						Name = BestMatch?.ShortName ?? BestMatch?.Name,
						ModuleInfo = BestMatch
					};
				}
			}

			// Fallback to brute force method.
			// This is necessary for some third party libs where there's just no way to map it to a project module.
			string ParsedModuleName = ParseModuleName(SourcePath);
			VerboseLog($"Could not resolve module for {SourcePath}. Parsed as {ParsedModuleName}");

			if (IsValidModuleName(ParsedModuleName))
			{
				return new ModuleResult()
				{
					Name = ParsedModuleName
				};
			}
			return null;
		}

		/** Module names can be used as perf metrics, so we need to remove spaces and non-alphanumeric characters except underscores and hyphens */
		private static string SanitizeModuleName(string ModuleName)
		{
			if (ModuleName == null)
			{
				return ModuleName;
			}
			return ModuleName.Replace(' ', '_').Replace('.', '-').Replace('#', '-').Replace("[", "").Replace("]", "");
		}

		private static bool IsValidModuleName(string ModuleName)
		{
			if (string.IsNullOrEmpty(ModuleName) || ModuleName == ".." || char.IsDigit(ModuleName[0]))
			{
				//Logger.LogWarning("Module name '{0}' is poorly chosen from source '{1}' - change FindModuleName heuristics!", ModuleName, SourcePath);
				return false;
			}
			return true;
		}

		private static bool IsPathLike(string InString)
		{
			if (!string.IsNullOrWhiteSpace(InString))
			{
				return InString.IndexOfAny(Path.GetInvalidPathChars()) < 0;
			}
			return false;
		}

		/** Attempts to run bloaty on the executable and returns the results */
		protected ModuleInfos ItemizeExecutableWithBloaty(FileReference Binary, FileReference SymbolsBinary)
		{
			ModuleInfos ModInfos = new ModuleInfos();

			if (FileReference.Exists(Binary))
			{
				// If a csv was provided as the binary, treat it as bloaty output so we can skip running it.
				string ResultsOverride = null;
				if (Binary.FullName.EndsWith(".csv"))
				{
					ResultsOverride = File.ReadAllText(Binary.FullName);
				}

				string Args = string.Format("-d compileunits -n 0 --csv {0}", Binary.FullName);
				if (SymbolsBinary != null && FileReference.Exists(SymbolsBinary))
				{
					Args += string.Format(" --debug-file={0}", SymbolsBinary.FullName);
				}

				string[] LineArray = RunBloaty(Args, ResultsOverride);

				// since we told bloaty to output csv, the result will be already machine readable. Just make sure that the first line is as we expect
				const string ExpectedCSVOutput = "compileunits,vmsize,filesize";
				if (LineArray[0] != ExpectedCSVOutput)
				{
					throw new AutomationException("CSV output ({0}) does not match expectation ({1})", LineArray[0], ExpectedCSVOutput);
				}

				for (int IdxLine = 1; IdxLine < LineArray.Length; ++IdxLine)
				{
					string[] LineElems = LineArray[IdxLine].Split(',');
					if (LineElems.Length != 3)
					{
						throw new AutomationException("CSV line ({0}) does not match expectation ({1}) - comma in source file?", LineArray[IdxLine], ExpectedCSVOutput);
					}

					string SourcePath = LineElems[0];
					ModuleResult? Module = FindModule(SourcePath);
					uint Size = uint.Parse(LineElems[1]);
					ModInfos.Total += Size;

					if (Module is ModuleResult Result)
					{
						string ModuleName = SanitizeModuleName(Result.Name);
						if (ModInfos.Modules.ContainsKey(ModuleName))
						{
							ModInfos.Modules[ModuleName] += Size;
						}
						else
						{
							ModInfos.Modules.Add(ModuleName, Size);
						}

						if (ModInfos.ModuleNameToSources.ContainsKey(ModuleName))
						{
							ModInfos.ModuleNameToSources[ModuleName].Add(SourcePath);
						}
						else
						{
							ModInfos.ModuleNameToSources.Add(ModuleName, new List<string>() { SourcePath });
						}

						if (Result.ModuleInfo is ModuleInfo Info)
						{
							if (Info.IsEngineModule)
							{
								ModInfos.TotalEngineSize += Size;
							}
							else
							{
								ModInfos.TotalGameSize += Size;
							}

							if (Info.IsGamePlugin)
							{
								ModInfos.TotalGamePluginSize += Size;
								if (Info.IsGameFeaturePlugin)
								{
									ModInfos.TotalGameFeaturesSize += Size;
								}
							}
							else if (Info.IsEnginePlugin)
							{
								ModInfos.TotalEnginePluginSize += Size;
							}
						}
						else
						{
							// If it looks like a path then it's likely an externally compiled lib.
							if (IsPathLike(SourcePath))
							{
								ModInfos.TotalExternalLibs += Size;
								VerboseLog($"Classifying {SourcePath} as a external lib");
							}
							else
							{
								ModInfos.TotalUnclassified += Size;
								VerboseLog($"Could not classify {SourcePath}");
							}
						}
					}
					else
					{
						VerboseLog($"Unable to resolve module name for {SourcePath}");
						ModInfos.SourcesWithoutModule.Add(LineArray[IdxLine]);

						// If bloaty cannot associate some bytes with a specific compile unit then it will group it under the section header instead.
						// Other sections are rows like [section .rodata]
						if (SourcePath.StartsWith("["))
						{
							ModInfos.TotalUnattributed += Size;
							VerboseLog($"Classifying {SourcePath} as unattributed");
						}
						else if (IsPathLike(SourcePath))
						{
							// If it looks like a path then it's likely an externally compiled lib.
							ModInfos.TotalExternalLibs += Size;
							VerboseLog($"Classifying {SourcePath} as a external lib");
						}
						else
						{
							ModInfos.TotalUnclassified += Size;
							VerboseLog($"Could not classify {SourcePath}");
						}
					}
				}

				// add a _Total metric
				ModInfos.Modules.Add("_Total", ModInfos.Total);
				ModInfos.Modules.Add("_EngineTotal", ModInfos.TotalEngineSize);
				ModInfos.Modules.Add("_GameTotal", ModInfos.TotalGameSize);
				return ModInfos;
			}
			else
			{
				throw new AutomationException("binary to itemize is not found at '{0}'", Binary.FullName);
			}
		}

		/** Whether the binary on this platform can be itemized with bloaty - expected to be overriden in platform-specific classes */
		virtual public ModuleInfos PlatformItemizeExecutable(FileReference Binary, FileReference SymbolsBinary)
		{
			return ItemizeExecutableWithBloaty(Binary, SymbolsBinary);
		}

		public override void ExecuteBuild()
		{
			FileReference Exe = ParseRequiredFileReferenceParam("ExeToItemize");
			string[] DestDirs = GetReportDestDirectories();

			bVerbose = ParseParamBool("Verbose", false);

			Modules = BuildModuleList();
			if (Modules == null)
			{
				Console.Error.WriteLine("Failed to create module list. Falling back on source path parsing.");
			}
			else
			{
				Console.WriteLine($"Found {Modules.Count} modules.");
			}

			// --- SECTION 1: Original functionality for itemizing a single executable ---
			// Can be skipped with -SkipExeItemization=true if only Merged Module analysis is needed.

			bool SkipExe = ParseParamBool("SkipExeItemization");
			if (!SkipExe)
			{
				ExecuteItemization(Exe, DestDirs);
			}

			// --- SECTION 2: New functionality for Merged Modules analysis ---
			// This logic only runs if -DllRoot is provided on the command line.
			DirectoryReference DllRoot = ParseOptionalDirectoryReferenceParam("DllRoot");
			if (DllRoot != null)
			{
				ExecuteMergedModulesAnalysis(DllRoot, DestDirs);
			}
		}

		private List<ModuleInfo> BuildModuleList()
		{
			string ProjectNameOrPath = ParseRequiredStringParam("Project");
			FileReference ProjectPath = ParseProjectString(ProjectNameOrPath);
			if (ProjectPath == null)
			{
				Console.Error.WriteLine("Failed to parse project path.");
				return null;
			}

			string Target = ParseRequiredStringParam("Target");

			string ModuleInfoPath = Path.GetTempFileName();
			UnrealTargetPlatform Platform = UnrealTargetPlatform.Parse(ParseRequiredStringParam("Platform"));

			// Run UBT in query mode to dump out all the modules with their paths to json.
			List<string> Args = new List<string>()
			{
				$"-project=\"{ProjectPath}\"",
				$"-Target={Target}",
				$"-Platform={Platform}",
				$"-Configuration={UnrealTargetConfiguration.Test}",
				$"-mode=Query",
				$"-Query=TargetModuleReferences",
				$"-NoIntellisense",
				$"-OutputPath=\"{ModuleInfoPath}\""
			};

			try
			{
				string UBTArgs = string.Join(" ", Args);
				UnrealBuildUtils.RunUBT(CommandUtils.CmdEnv, UnrealBuild.UnrealBuildToolDll, UBTArgs);
			}
			catch (AutomationException Ex)
			{
				Console.Error.WriteLine($"Error dumping modules from UBT: {Ex}");
				return null;
			}
			
			if (!Path.Exists(ModuleInfoPath))
			{
				Console.Error.WriteLine($"No module json file found at {ModuleInfoPath}");
				return null;
			}

			List<ModuleInfo> Modules = [];

			try
			{
				string JsonText = File.ReadAllText(ModuleInfoPath);
				var ModuleMap = JsonSerializer.Deserialize<Dictionary<string, Dictionary<string, ModuleInfo>>>(JsonText);

				DirectoryReference RootDirectory = UnrealBuildBase.Unreal.RootDirectory;

				Modules = ModuleMap["modules"]
					.Select(Kvp => new ModuleInfo()
					{
						Name = Kvp.Key,
						ShortName = Kvp.Value.ShortName,
						ModuleDirectory = new DirectoryReference(Kvp.Value.ModuleDirectory).MakeRelativeTo(RootDirectory),
						IntermediateDirectory = new DirectoryReference(Kvp.Value.IntermediateDirectory).MakeRelativeTo(RootDirectory)
					})
					.ToList();
			}
			catch (Exception Ex)
			{
				Console.Error.WriteLine($"Error parsing module json: {Ex}");
			}
			finally
			{
				File.Delete(ModuleInfoPath);
			}
			return Modules;
		}

		private void ExecuteItemization(FileReference ExecutableFileToItemize, string[] DestDirs)
		{
			FileReference ExecutableFileWithSymbols = ParseOptionalFileReferenceParam("SymbolsExe");
			System.Console.WriteLine("Executing itemization of executable: {0}", ExecutableFileToItemize);
			if (ExecutableFileWithSymbols != null)
			{
				System.Console.WriteLine("Using executable for symbolication: {0}", ExecutableFileWithSymbols);
			}

			ModuleInfos Info = PlatformItemizeExecutable(ExecutableFileToItemize, ExecutableFileWithSymbols);
			if (Info.Modules.Count == 0)
			{
				Console.Error.WriteLine("Unable to itemize binary '{0}'", ExecutableFileToItemize.ToString());
				return;
			}

			try
			{
				string[] DestPaths = DestDirs.Select(Dir => Path.Combine(Dir, ExecutableFileToItemize.GetFileNameWithoutExtension() + "_ItemizedModules.csv")).ToArray();
				WriteItemizedReport(Info, DestPaths);
			}
			catch (Exception ex)
			{
				Console.WriteLine($"Failed to generate module itemization report: {ex.Message}");
			}

			try
			{
				string[] DestPaths = DestDirs.Select(Dir => Path.Combine(Dir, ExecutableFileToItemize.GetFileNameWithoutExtension() + "_Totals.csv")).ToArray();
				WriteTotalsReport(Info, DestPaths);
			}
			catch (Exception ex)
			{
				Console.WriteLine($"Failed to generate totals report: {ex.Message}");
			}

			try
			{
				string[] DestPaths = DestDirs.Select(Dir => Path.Combine(Dir, ExecutableFileToItemize.GetFileNameWithoutExtension() + "_Sections.csv")).ToArray();
				WriteSectionsReport(ExecutableFileToItemize, DestPaths);
			}
			catch (Exception ex)
			{
				Console.WriteLine($"Failed to generate sections report: {ex.Message}");
			}
		}

		private void WriteItemizedReport(FileReference ExecutableFileToItemize, string[] DestPaths)
		{
			FileReference ExecutableFileWithSymbols = ParseOptionalFileReferenceParam("SymbolsExe");

			ModuleInfos Info = PlatformItemizeExecutable(ExecutableFileToItemize, ExecutableFileWithSymbols);
			if (Info.Modules.Count == 0)
			{
				throw new AutomationException("Unable to itemize binary '{0}'", ExecutableFileToItemize.ToString());
			}

			WriteItemizedReport(Info, DestPaths);
		}

		private void WriteItemizedReport(ModuleInfos Info, string[] DestPaths)
		{
			bool bPrintResults = ParseParamBool("PrintResults", true);
			if (bPrintResults)
			{
				System.Console.WriteLine("\n\n\n\nExec itemization, here's how we arrived at module names:");
				foreach (KeyValuePair<string, List<string>> ModuleMap in Info.ModuleNameToSources)
				{
					uint SizeForThisModule = Info.Modules[ModuleMap.Key];

					System.Console.WriteLine("{0} (takes {1:F2} MB):", ModuleMap.Key, SizeForThisModule / (1024.0 * 1024.0));
					foreach (string Source in ModuleMap.Value)
					{
						System.Console.WriteLine("\t{0}", Source);
					}
				}

				System.Console.WriteLine("\n\n\n\nAdditionally, {0} source files of {1} bytes ({2:F2} MB in total) failed to be categorized:",
					Info.SourcesWithoutModule.Count, Info.TotalUnclassified, Info.TotalUnclassified / (1024.0 * 1024.0));
				if (Info.SourcesWithoutModule.Count > 0)
				{
					foreach (string Source in Info.SourcesWithoutModule)
					{
						System.Console.WriteLine("\t{0}", Source);
					}
				}
				else
				{
					System.Console.WriteLine("None!");
				}
			}

			// sort modules alphabetically and print out
			List<KeyValuePair<string, uint>> ModInfos = Info.Modules.ToList();

			if (bPrintResults)
			{
				System.Console.WriteLine("\n\n\n\n-------------------------------------------------------------------------------------------------------------------------------------------------------------------");
				System.Console.WriteLine("Exec itemization, modules sorted alphabetically by name");
				System.Console.WriteLine("-------------------------------------------------------------------------------------------------------------------------------------------------------------------");
				System.Console.WriteLine("Module, Megabytes (float), Bytes (uint)");
			}
			ModInfos.SortBy(ModInfo => ModInfo.Key);

			// Create a new string builder for CSV output
			StringBuilder Builder = new StringBuilder("Module,Megabytes,Bytes\n");

			foreach (KeyValuePair<string, uint> ModInfo in ModInfos)
			{
				if (bPrintResults)
				{
					System.Console.WriteLine("{0}, {1:F2}, {2}", ModInfo.Key, ModInfo.Value / (1024.0 * 1024.0), ModInfo.Value);
				}
				Builder.AppendLine(string.Format("{0},{1:F2},{2}", ModInfo.Key, ModInfo.Value / (1024.0 * 1024.0), ModInfo.Value));
			}

			ModInfos.SortBy(ModInfo => ModInfo.Value);
			ModInfos.Reverse();

			if (bPrintResults)
			{
				System.Console.WriteLine("\n\n\n\n-------------------------------------------------------------------------------------------------------------------------------------------------------------------");
				System.Console.WriteLine("Exec itemization, modules sorted by their size");
				System.Console.WriteLine("-------------------------------------------------------------------------------------------------------------------------------------------------------------------");
				System.Console.WriteLine("Module, Megabytes (float), Bytes (uint)");
				foreach (KeyValuePair<string, uint> ModInfo in ModInfos)
				{
					System.Console.WriteLine("{0}, {1:F2}, {2}", ModInfo.Key, ModInfo.Value / (1024.0 * 1024.0), ModInfo.Value);
				}
			}

			foreach (string DestPath in DestPaths)
			{
				System.Console.WriteLine($"Writing itemized module information to: {DestPath}");

				string DirPath = Path.GetDirectoryName(DestPath);
				Directory.CreateDirectory(DirPath);

				File.WriteAllText(DestPath, Builder.ToString());
			}
		}

		private void WriteTotalsReport(ModuleInfos Info, string[] DestPaths)
		{
			StringBuilder Builder = new StringBuilder("Name,Megabytes,Bytes\n");
			// Grand total
			Builder.AppendLine($"Total,{Info.Total / (1024.0 * 1024.0):F2},{Info.Total}");
			// Sections making up total
			Builder.AppendLine($"Engine,{Info.TotalEngineSize / (1024.0 * 1024.0):F2},{Info.TotalEngineSize}");
			Builder.AppendLine($"Game,{Info.TotalGameSize / (1024.0 * 1024.0):F2},{Info.TotalGameSize}");
			Builder.AppendLine($"Unattributed,{Info.TotalUnattributed / (1024.0 * 1024.0):F2},{Info.TotalUnattributed}");
			Builder.AppendLine($"ExternalLibs,{Info.TotalExternalLibs / (1024.0 * 1024.0):F2},{Info.TotalExternalLibs}");
			Builder.AppendLine($"Unclassified,{Info.TotalUnclassified / (1024.0 * 1024.0):F2},{Info.TotalUnclassified}");

			// Engine breakdown
			uint EngineBase = Info.TotalEngineSize - Info.TotalEnginePluginSize;
			Builder.AppendLine($"Engine/Base,{EngineBase / (1024.0 * 1024.0):F2},{EngineBase}");
			Builder.AppendLine($"Engine/Plugins,{Info.TotalEnginePluginSize / (1024.0 * 1024.0):F2},{Info.TotalEnginePluginSize}");

			// Game breakdown
			uint GameBase = Info.TotalGameSize - Info.TotalGamePluginSize;
			uint GamePlugins = Info.TotalGamePluginSize - Info.TotalGameFeaturesSize;
			Builder.AppendLine($"Game/Base,{GameBase / (1024.0 * 1024.0):F2},{GameBase}");
			Builder.AppendLine($"Game/Plugins,{GamePlugins / (1024.0 * 1024.0):F2},{GamePlugins}");
			Builder.AppendLine($"Game/GameFeatures,{Info.TotalGameFeaturesSize / (1024.0 * 1024.0):F2},{Info.TotalGameFeaturesSize}");

			foreach (string DestPath in DestPaths)
			{
				System.Console.WriteLine($"Writing totals information to: {DestPath}");

				string DirPath = Path.GetDirectoryName(DestPath);
				Directory.CreateDirectory(DirPath);

				File.WriteAllText(DestPath, Builder.ToString());
			}
		}

		private void WriteSectionsReport(FileReference ExecutableFile, string[] DestPaths)
		{
			string Args = string.Format("-s vm --csv {0}", ExecutableFile.FullName);
			string[] Lines = RunBloaty(Args);

			string ExpectedColumns = "sections,vmsize,filesize";
			if (Lines[0] != ExpectedColumns)
			{
				throw new AutomationException("CSV output ({0}) does not match expectation ({1})", Lines[0], ExpectedColumns);
			}

			uint TotalVMSize = 0;
			StringBuilder Builder = new StringBuilder("Section,Megabytes,Bytes\n");
			foreach (string Line in Lines.Skip(1))
			{
				string[] LineValues = Line.Split(",");
				string SectionName = SanitizeModuleName(LineValues[0]).Replace("-", "");

				uint VMSize = uint.Parse(LineValues[1]);
				TotalVMSize += VMSize;

				Builder.AppendLine(string.Format("{0},{1:F2},{2}", SectionName, VMSize / (1024.0 * 1024.0), VMSize));
			}

			Builder.AppendLine(string.Format("{0},{1:F2},{2}", "Total", TotalVMSize / (1024.0 * 1024.0), TotalVMSize));

			foreach (string DestPath in DestPaths)
			{
				System.Console.WriteLine($"Writing sections report to: {DestPath}");

				string DirPath = Path.GetDirectoryName(DestPath);
				Directory.CreateDirectory(DirPath);

				File.WriteAllText(DestPath, Builder.ToString());
			}
		}

		/** Runs bloaty with the given args and returns an array of the output. */
		protected static string[] RunBloaty(string Args, string ResultsOverride = null)
		{
			string BloatyExePath = Unreal.RootDirectory.ToString() + @"\Engine\Extras\ThirdPartyNotUE\Bloaty\bloaty.exe";

			if (File.Exists(BloatyExePath))
			{
				int ExitCode = 0;

				string Results = null;
				if (ResultsOverride != null)
				{
					Results = ResultsOverride;
				}
				else
				{
					Results = UnrealBuildTool.Utils.RunLocalProcessAndReturnStdOut(BloatyExePath, Args, out ExitCode);
				}

				if (ExitCode == 0)
				{
					// Get the results from Bloaty as an array of lines
					string[] LineArray = Results.Split(new char[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);

					return LineArray;
				}
				else
				{
					throw new AutomationException("bloaty failed with exit code {0} and output '{1}'", ExitCode, Results);
				}
			}
			else
			{
				throw new AutomationException("bloaty.exe is not found at '{0}'", BloatyExePath);
			}
		}

		private void ExecuteMergedModulesAnalysis(DirectoryReference DllRoot, string[] DestDirs)
		{
			string PlatformArg = ParseParamValue("Platform") ?? "Auto";
			string ExtsArg = ParseParamValue("ModuleExts");
			string EagerArg = ParseParamValue("EagerPrefixes");
			string PkgArg = ParseParamValue("PackageMarkerSuffixes");
			string MmCsv = ParseParamValue("MergedModulesCsvName") ?? "MergedModules.csv";

			bool PackagedOnly = ParseParamBool("PackagedOnly");
			string ConfigFilter = ParseParamValue("ConfigFilter");

			bool PerDll = ParseParamBool("PerDllReports");
			string DllSubdir = ParseParamValue("DllReportsSubdir") ?? "DLL";
			int Dop = 0;
			int.TryParse(ParseParamValue("DllReportsDOP") ?? "", out Dop);

			// Create a configuration object based on the specified platform and any CLI overrides.
			// This determines which file extensions to look for, what defines an "eager" module, etc.
			var Cfg = MergedModulePlatformConfig.Create(PlatformArg, ExtsArg, EagerArg, PkgArg);
			Console.WriteLine($"Platform={Cfg.Platform}, Exts=[{string.Join(",", Cfg.ModuleExtensions)}], Eager=[{string.Join(";", Cfg.EagerPrefixes)}], PackageMarkers=[{string.Join(";", Cfg.PackageMarkerSuffixes)}], PackagedOnly={PackagedOnly}, ConfigFilter='{ConfigFilter}'");

			// 1) Scan the DllRoot directory to find all relevant module files.
			var Modules = MergedModuleScanner.EnumerateModules(DllRoot, Cfg, PackagedOnly, ConfigFilter);

			// 2) Write the main Merged Modules summary report to each destination directory.
			foreach (string DestDir in DestDirs)
			{
				string OutPath = Path.Combine(DestDir, MmCsv);
				try
				{
					MergedModuleScanner.WriteMergedModulesRollupCsv(OutPath, Modules);
					Console.WriteLine($"Wrote Merged Modules rollup report: {OutPath}");
				}
				catch (Exception ex)
				{
					Console.WriteLine($"Failed to write Merged Modules rollup report: {OutPath}: {ex.Message}");
				}
			}

			// 3) Optional: If requested, run detailed Bloaty reports on each individual module.
			// This is performed in parallel to improve performance.
			if (PerDll && Modules.Count > 0)
			{
				if (Dop <= 0)
				{
					Dop = Math.Max(1, Environment.ProcessorCount / 2);
				}
				Console.WriteLine($"Per-DLL reports enabled. Modules={Modules.Count}, MaxDOP={Dop}");

				var Options = new System.Threading.Tasks.ParallelOptions { MaxDegreeOfParallelism = Dop };


				// This process is "best-effort". If Bloaty fails for one module (e.g., unsupported format),
				// it creates placeholder files and continues, ensuring the entire automation job does not fail.
				System.Threading.Tasks.Parallel.ForEach(Modules, Options, Mod =>
				{
					foreach (string Root in DestDirs)
					{
						string DllDir = Path.Combine(Root, DllSubdir, Mod.Name); // mod.Name is the module's filename without the extension (e.g., "EOSSDK-Switch-Shipping")
						Directory.CreateDirectory(DllDir); // ensure dir exists even if bloaty fails

						string Itemized = Path.Combine(DllDir, $"{Mod.Name}_ItemizedModules.csv");
						string Sections = Path.Combine(DllDir, $"{Mod.Name}_Sections.csv");

						try
						{
							// Attempt to generate both itemized and sections reports for this module.
							WriteItemizedReport(new FileReference(Mod.Path), new[] { Itemized });
							WriteSectionsReport(new FileReference(Mod.Path), new[] { Sections });
							Console.WriteLine($"Wrote per-DLL bloaty for {Mod.Name}");
						}
						catch (Exception ex)
						{
							// If Bloaty fails, create a marker file with the error message.
							string Marker = Path.Combine(DllDir, $"{Mod.Name}_BloatySkipped.txt");
							File.WriteAllText(Marker, ex.Message);

							// Also create header-only CSVs so downstream tools like PRS find a consistent file layout.
							if (!File.Exists(Itemized))
							{
								File.WriteAllText(Itemized, "Module,Megabytes,Bytes\n");
							}
							if (!File.Exists(Sections))
							{
								File.WriteAllText(Sections, "Section,Megabytes,Bytes\n");
							}
							Console.WriteLine($"Per-DLL bloaty skipped for {Mod.Name}: {ex.Message}");
						}
					}
				});
			}
		}
	}

	// ===== Platform config for merged-modules scan =====
	// Represents the target platform for a Merged Modules scan.
	internal enum MergedModulePlatform
	{
		Auto, Switch, Win64, PS4, PS5, Android, iOS, Xbox
	}

	// Holds all configuration for a Merged Modules scan, including platform-specific defaults
	// that can be overridden by command-line arguments.
	internal sealed class MergedModulePlatformConfig
	{
		public MergedModulePlatform Platform { get; private set; }
		public string[] ModuleExtensions { get; private set; }      // e.g. .nro, .dll, .so
		public string[] EagerPrefixes { get; private set; }         // e.g. EOS, KITT
		public string[] PackageMarkerSuffixes { get; private set; } // e.g. .nspd (Switch)

		private MergedModulePlatformConfig() { }

		// Creates a new configuration instance, establishing defaults based on the platform and then applying any command-line overrides.
		public static MergedModulePlatformConfig Create(string PlatformArg, string ExtsArg, string EagerArg, string PkgArg)
		{
			var Cfg = new MergedModulePlatformConfig();
			Cfg.Platform = ParsePlatform(PlatformArg);

			// Defaults per platform
			switch (Cfg.Platform)
			{
				case MergedModulePlatform.Switch:
					Cfg.ModuleExtensions = new[] { ".nro" };
					Cfg.EagerPrefixes = new[] { "EOS", "KITT" };
					Cfg.PackageMarkerSuffixes = new[] { ".nspd" };
					break;

				case MergedModulePlatform.Win64:
					Cfg.ModuleExtensions = new[] { ".dll" };
					Cfg.EagerPrefixes = new[] { "EOS", "KITT" };
					Cfg.PackageMarkerSuffixes = Array.Empty<string>();
					break;

				case MergedModulePlatform.Xbox:
					Cfg.ModuleExtensions = new[] { ".dll" }; // treat like Win64 by default
					Cfg.EagerPrefixes = new[] { "EOS", "KITT" };
					Cfg.PackageMarkerSuffixes = Array.Empty<string>();
					break;

				case MergedModulePlatform.PS4:
				case MergedModulePlatform.PS5:
					Cfg.ModuleExtensions = new[] { ".sprx", ".prx" };
					Cfg.EagerPrefixes = new[] { "EOS", "KITT" };
					Cfg.PackageMarkerSuffixes = Array.Empty<string>();
					break;

				case MergedModulePlatform.Android:
					Cfg.ModuleExtensions = new[] { ".so" };
					Cfg.EagerPrefixes = new[] { "EOS", "KITT" };
					Cfg.PackageMarkerSuffixes = Array.Empty<string>();
					break;

				case MergedModulePlatform.iOS:
					Cfg.ModuleExtensions = new[] { ".dylib" };
					Cfg.EagerPrefixes = new[] { "EOS", "KITT" };
					Cfg.PackageMarkerSuffixes = Array.Empty<string>();
					break;

				case MergedModulePlatform.Auto:
				default:
					Cfg.ModuleExtensions = Array.Empty<string>(); // auto-detect later
					Cfg.EagerPrefixes = new[] { "EOS", "KITT" };
					Cfg.PackageMarkerSuffixes = new[] { ".nspd" }; // helpful for Switch
					break;
			}

			// Overrides from CLI
			if (!string.IsNullOrWhiteSpace(ExtsArg))
			{
				Cfg.ModuleExtensions = ExtsArg.Split(',').Select(s => s.Trim()).Where(s => s.StartsWith(".")).ToArray();
			}

			if (!string.IsNullOrWhiteSpace(EagerArg))
			{
				Cfg.EagerPrefixes = EagerArg.Split(';').Select(s => s.Trim()).Where(s => s.Length > 0).ToArray();
			}

			if (!string.IsNullOrWhiteSpace(PkgArg))
			{
				Cfg.PackageMarkerSuffixes = PkgArg.Split(';').Select(s => s.Trim()).Where(s => s.Length > 0).ToArray();
			}

			return Cfg;
		}

		public static MergedModulePlatform ParsePlatform(string S)
		{
			if (string.IsNullOrWhiteSpace(S))
			{
				return MergedModulePlatform.Auto;
			}
			S = S.Trim().ToLowerInvariant();
			return S switch
			{
				"switch" => MergedModulePlatform.Switch,
				"win64" or "windows" => MergedModulePlatform.Win64,
				"ps4" => MergedModulePlatform.PS4,
				"ps5" => MergedModulePlatform.PS5,
				"android" => MergedModulePlatform.Android,
				"ios" => MergedModulePlatform.iOS,
				"xbox" or "xsx" or "xboxone" or "xboxseries" => MergedModulePlatform.Xbox,
				_ => MergedModulePlatform.Auto,
			};
		}
	}

	// ===== Scanner/writer for merged-modules rollup  =====
	// Provides functionality to scan a directory for platform-specific modules (e.g., .dll, .nro),
	// gather metadata about them, and write a summary "rollup" CSV report.
	internal static class MergedModuleScanner
	{
		internal sealed class Row
		{
			public string Name = "";
			public long Bytes;
			public double MB;
			public string Package = "";
			public string Bucket = "demand"; // or "eager"
			public string Path = "";
		}

		// Scans the given root directory for module files based on the platform configuration.
		// <param name="root">The directory to scan recursively.</param>
		// <param name="cfg">The platform configuration determining which files to find.</param>
		// <param name="packagedOnly">If true, only includes modules found within a recognized package folder.</param>
		// <param name="configFilter">If set, only includes modules from packages whose names contain this string.</param>
		// <returns>A list of Row objects, one for each module found.</returns>
		public static List<Row> EnumerateModules(DirectoryReference Root, MergedModulePlatformConfig Cfg, bool PackagedOnly, string ConfigFilter)
		{
			var Exts = (Cfg.ModuleExtensions.Length > 0) ? Cfg.ModuleExtensions : AutoDetectExtensions(Root);
			if (Exts.Length == 0)
			{
				throw new InvalidOperationException($"Could not auto-detect module extensions under: {Root.FullName}. Provide -ModuleExts or -Platform.");
			}

			var Rows = new List<Row>();
			var EagerSet = new HashSet<string>(Cfg.EagerPrefixes.Select(p => p.ToUpperInvariant()));

			foreach (string Ext in Exts)
			{
				foreach (string File in Directory.EnumerateFiles(Root.FullName, "*" + Ext, SearchOption.AllDirectories))
				{
					var Fi = new FileInfo(File);
					string Package = TryFindPackage(Fi.Directory, Cfg.PackageMarkerSuffixes);

					// PackagedOnly: require a package match if markers are defined
					if (PackagedOnly && Cfg.PackageMarkerSuffixes.Length > 0 && string.IsNullOrEmpty(Package))
					{
						continue;
					}

					// ConfigFilter: include only packages whose name contains the token
					if (!string.IsNullOrEmpty(ConfigFilter) &&
						(string.IsNullOrEmpty(Package) || !Package.Contains(ConfigFilter, StringComparison.InvariantCultureIgnoreCase)))
					{
						continue;
					}
					string Name = Path.GetFileNameWithoutExtension(Fi.Name);
					long Bytes = Fi.Length;
					string Bucket = IsEager(Name, EagerSet) ? "eager" : "demand";

					Rows.Add(new Row
					{
						Name = Name,
						Bytes = Bytes,
						MB = Bytes / (1024.0 * 1024.0),
						Package = Package,
						Bucket = Bucket,
						Path = Fi.FullName
					});
				}
			}

			return Rows.OrderByDescending(R => R.Bytes).ToList();
		}

		// Writes the provided list of module rows to a CSV file.
		public static void WriteMergedModulesRollupCsv(string OutCsvPath, List<Row> Rows)
		{
			long Total = Rows.Sum(R => R.Bytes);

			var Sb = new StringBuilder("Name,Bytes,Megabytes,Package,Bucket,Path\n");
			foreach (var R in Rows)
			{
				Sb.AppendLine($"{R.Name},{R.Bytes},{R.MB:F2},{R.Package},{R.Bucket},{R.Path}");
			}
			Sb.AppendLine($"_MM_Total,{Total},{(Total / (1024.0 * 1024.0)):F2},,,");

			var Dir = Path.GetDirectoryName(OutCsvPath);
			if (!string.IsNullOrEmpty(Dir))
			{
				Directory.CreateDirectory(Dir);
			}
			File.WriteAllText(OutCsvPath, Sb.ToString());
		}

		// Writer calls Enumerate+Write to preserve API if you call it elsewhere
		public static void WriteMergedModulesRollupCsv(DirectoryReference Root, string OutCsvPath, MergedModulePlatformConfig Cfg)
		{
			var Rows = EnumerateModules(Root, Cfg, PackagedOnly: false, ConfigFilter: null);
			WriteMergedModulesRollupCsv(OutCsvPath, Rows);
		}

		// Attempts to automatically detect the correct module extension by searching for known types.
		private static string[] AutoDetectExtensions(DirectoryReference Root)
		{
			var Candidates = new[] { ".nro", ".dll", ".sprx", ".prx", ".so", ".dylib" };
			foreach (var Ext in Candidates)
			{
				if (Directory.EnumerateFiles(Root.FullName, "*" + Ext, SearchOption.AllDirectories).Any())
				{
					return new[] { Ext };
				}
			}
			return Array.Empty<string>();
		}

		// Checks if a module name matches any of the "eager-loaded" prefixes.
		private static bool IsEager(string Name, HashSet<string> EagerPrefixesUpper)
		{
			string Upper = Name.ToUpperInvariant();
			foreach (string P in EagerPrefixesUpper)
			{
				if (Upper.StartsWith(P)) { return true; }
			}

			return false;
			
		}

		// Walks up the directory tree to find a parent folder whose name indicates it is a package.
		private static string TryFindPackage(DirectoryInfo Start, string[] Suffixes)
		{
			if (Start == null || Suffixes.Length == 0) { return ""; }
			for (DirectoryInfo D = Start; D != null; D = D.Parent)
			{
				foreach (string Sfx in Suffixes)
				{
					if (D.Name.EndsWith(Sfx, StringComparison.InvariantCultureIgnoreCase))
					{
						return D.Name;
					}
				}
			}
			return "";
		}
	}


}
