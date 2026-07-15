using System;
using System.Collections.Generic;
using System.Linq;
using System.ComponentModel;
using System.Text.RegularExpressions;
using System.Net.Mime;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using System.Runtime.InteropServices;


using PDBReader;

namespace CruncherSharp
{
	internal class SymbolAnalyzerRawPDB : SymbolAnalyzer
	{
		PDB pdbFile = null;

		public override void OpenAsync(string filename)
		{
			pdbFile = new PDB(filename);
		}

		public override bool LoadPdb(object sender, LoadPDBTask task)
		{
			if (task.SecondPDB || pdbFile == null)
			{
				pdbFile = new PDB(task.FileName);
			}

			if (!LoadSymbols(sender, task))
			{
				return false;
			}
			RunAnalysis();
			return true;
		}

		private bool LoadSymbols(object sender, LoadPDBTask task)
		{
			var worker = sender as BackgroundWorker;
			worker?.ReportProgress(0, "Finding symbols");

			IEnumerable<PDBReader.TypeBasicInfo> allSymbols;
			if (task.Filter.Length > 0)
			{
				if (task.UseRegularExpression)
				{
					Regex r = new Regex(task.WholeExpression ? $"^{task.Filter}$" : task.Filter, RegexOptions.Compiled);
					allSymbols = pdbFile.TypeBasicInfos.Where(x => r.IsMatch(x.Name));
				}
				else if (task.WholeExpression)
				{
					var sym = pdbFile.FindType(task.Filter, task.MatchCase);
					if (sym.HasValue)
					{
						allSymbols = new PDBReader.TypeBasicInfo[] { sym.Value };
					}
					else
					{
						allSymbols = new PDBReader.TypeBasicInfo[0];
					}
				}
				else
				{
					var Comparison = task.MatchCase ? StringComparison.InvariantCulture : StringComparison.InvariantCultureIgnoreCase;
					allSymbols = pdbFile.TypeBasicInfos.Where(x => x.Name.IndexOf(task.Filter, Comparison) != -1);
				}
			}
			else
			{
				allSymbols = pdbFile.TypeBasicInfos;
			}

			if (allSymbols == null)
			{
				return false;
			}

			worker?.ReportProgress(0, "Processing symbols");
			int addedSymbolsCount = 0;
			if (task.SecondPDB)
			{
				allSymbols.Reverse().AsParallel().ForAll(symBasicInfo =>
				{
					SymbolInfo info = FindSymbolInfo(symBasicInfo.Name);
					if (info != null)
					{
						info.NewSize = symBasicInfo.Size;
						if (MemPools != null)
						{
							info.SetNewMemPools(MemPools);
						}
						// Thread-safe increment
						Interlocked.Increment(ref addedSymbolsCount);
					}
				});
			}
			else
			{
				ConcurrentDictionary<string, SymbolInfo> results = new ConcurrentDictionary<string, SymbolInfo>();
				int loadedSymbolsCount = 0;

				allSymbols.AsParallel().ForAll(symBasicInfo =>
				{
					if (symBasicInfo.Size == 0 || HasSymbolInfo(symBasicInfo.Name))
					{
						return;
					}

					if (task.Filter.Length > 0)
					{
						LoadSymbolsRecursive(symBasicInfo, results);
					}
					else
					{
						// Loading all symbols so don't recurse
						var symbolInfo = new SymbolInfo(symBasicInfo.Name, symBasicInfo.GetType().Name, symBasicInfo.Size, MemPools);
						if (results.TryAdd(symbolInfo.Name, symbolInfo))
						{
							var symFullInfo = pdbFile.GetFullTypeInfo(symBasicInfo, loadFunctions: false);
							ProcessDataMembers(symbolInfo, pdbFile, symFullInfo);
							symbolInfo.SortAndCalculate();
						}
					}

					// Report progress during parallel loading phase - show count without known total
					int currentLoaded = Interlocked.Increment(ref loadedSymbolsCount);
					// Report every 500 symbols to reduce overhead (multiple threads may report simultaneously, which is fine)
					if (currentLoaded % 500 == 0)
					{
						worker?.ReportProgress(0, String.Format("Loading symbols: {0}", currentLoaded));
					}
				});

				var allSymbolsCount = results.Count;
				worker?.ReportProgress(50, String.Format("Organizing {0} symbols", allSymbolsCount));

				int progress = 0;
				foreach (KeyValuePair<string, SymbolInfo> pair in results)
				{
					Symbols.Add(pair.Key, pair.Value);
					Interlocked.Increment(ref addedSymbolsCount);

					// Batch progress updates every 1% to reduce overhead (50-99%)
					var percentProgress = allSymbolsCount > 0 ? (int)Math.Round((double)(addedSymbolsCount) / allSymbolsCount) : 0;
					if (percentProgress > progress)
					{
						progress = Math.Min(percentProgress, 99);
						worker?.ReportProgress(progress, String.Format("Adding symbol {0} of {1}", addedSymbolsCount, allSymbolsCount));
					}

					// Optimized namespace extraction - find :: positions once
					var symbolName = pair.Key;
					var firstColonPos = symbolName.IndexOf("::", StringComparison.Ordinal);
					if (firstColonPos > 0 && symbolName.IndexOf('<') == -1)
					{
						// Extract root namespace (before first ::)
						RootNamespaces.Add(symbolName.Substring(0, firstColonPos));

						// Extract full namespace path (before last ::)
						var lastColonPos = symbolName.LastIndexOf("::", StringComparison.Ordinal);
						if (lastColonPos > 0)
						{
							Namespaces.Add(symbolName.Substring(0, lastColonPos));
						}
					}
				}
			}

			worker?.ReportProgress(100, String.Format("{0} symbols added", addedSymbolsCount));

			// Keep PDB file open for lazy function loading
			// if (task.Filter.Length == 0)
			// {
			// 	// Everything was loaded, close PDB
			// 	pdbFile.Dispose();
			// 	pdbFile = null;
			// }

			return true;
		}

		private void LoadSymbolsRecursive(TypeBasicInfo newType, ConcurrentDictionary<string, SymbolInfo> results)
		{
			if (Symbols.ContainsKey(newType.Name) || results.ContainsKey(newType.Name))
			{
				return;
			}
			if (newType.Size == 0)
			{
				return;
			}

			var symbolInfo = new SymbolInfo(newType.Name, newType.Name, newType.Size, MemPools);
			if (results.TryAdd(symbolInfo.Name, symbolInfo))
			{
				// Skip function loading for performance - load only data members
				var symFullInfo = pdbFile.GetFullTypeInfo(newType, loadFunctions: false);
				ProcessDataMembers(symbolInfo, pdbFile, symFullInfo);
				symbolInfo.SortAndCalculate();
				foreach (var member in symbolInfo.Members)
				{
					if (member.Category == SymbolMemberInfo.MemberCategory.UDT || member.Category == SymbolMemberInfo.MemberCategory.Base)
					{
						var info = pdbFile.FindType(member.TypeName, true);
						if (info.HasValue)
						{
							LoadSymbolsRecursive(info.Value, results);
						}
					}
				}
			}
		}

		public override bool LoadCSV(object sender, List<LoadCSVTask> tasks)
		{
			// if pdbFile is null it means that it is fully loaded, just update count
			if (pdbFile != null)
			{
				var worker = sender as BackgroundWorker;

				var allSymbolsCount = (worker != null) ? tasks.Count : 0;
				worker?.ReportProgress(0, "Adding symbols");

				ConcurrentDictionary<string, SymbolInfo> bag = new ConcurrentDictionary<string, SymbolInfo>();
				tasks.AsParallel().ForAll(task =>
				{
					if (worker != null && worker.CancellationPending)
					{
						return;
					}

					var symbol = pdbFile.FindType(task.ClassName, true);
					if (symbol.HasValue)
					{
						LoadSymbolsRecursive(symbol.Value, bag);
					}
				});

				ulong addedSymbolsCount = 0;
				foreach (KeyValuePair<string, SymbolInfo> pair in bag)
				{
					if (!HasSymbolInfo(pair.Key))
					{
						Symbols.Add(pair.Key, pair.Value);
						++addedSymbolsCount;
						if (pair.Key.Contains("::") && !pair.Key.Contains("<"))
						{
							RootNamespaces.Add(pair.Key.Substring(0, pair.Key.IndexOf("::")));
							Namespaces.Add(pair.Key.Substring(0, pair.Key.LastIndexOf("::")));
						}
					}
				}
				worker?.ReportProgress(100, String.Format("{0} symbols added", addedSymbolsCount));
			}

			foreach (LoadCSVTask task in tasks)
			{
				if (Symbols.TryGetValue(task.ClassName, out var symbolInfo))
				{
					symbolInfo.IsImportedFromCSV = true;
					symbolInfo.TotalCount = symbolInfo.NumInstances = task.Count;
				}
			}

			RunAnalysis();
			return true;
		}

		private void ProcessDataMembers(SymbolInfo outSymbolInfo, PDB pdbFile, PDBReader.TypeFullInfo typeFullInfo)
		{
			foreach (var dataMember in typeFullInfo.Members)
			{
				var typeSymbol = pdbFile.GetBasicTypeInfo(dataMember.TypeIndex);

				var category = SymbolMemberInfo.MemberCategory.Member;
				var memberName = dataMember.Name;
				var typeName = typeSymbol.Name;

				if (dataMember.IsVtable)
				{
					category = SymbolMemberInfo.MemberCategory.VTable;
					memberName = string.Empty;
					typeName = string.Empty;
				}
				else if (dataMember.IsBaseClass)
				{
					category = SymbolMemberInfo.MemberCategory.Base;
				}
				// TODO: Fix this to fix expanding struct fields?
				else if (typeSymbol.IsUDT)
				{
					category = SymbolMemberInfo.MemberCategory.UDT;
				}
				else if (typeSymbol.IsPointer)
				{
					category = SymbolMemberInfo.MemberCategory.Pointer;
				}

				var info = new SymbolMemberInfo(
					category,
					memberName,
					typeName,
					typeSymbol.Size,
					dataMember.BitSize,
					dataMember.Offset,
					dataMember.BitPosition
				);
				info.BitField = dataMember.IsBitfield;

				outSymbolInfo.Members.Add(info);
			}
		}


		public void ProcessFunctions(SymbolInfo outSymbolInfo, PDB pdbFile, PDBReader.TypeFullInfo typeFullInfo)
		{
			// Track added functions to filter duplicates (compiler can emit multiple variants of same function)
			var addedFunctions = new HashSet<string>();

			foreach (var functionMember in typeFullInfo.Functions)
			{
				var info = new SymbolFunctionInfo();

				// Determine function category
				if (functionMember.IsStatic)
				{
					info.Category = SymbolFunctionInfo.FunctionCategory.StaticFunction;
				}
				else if (functionMember.Name.StartsWith("~"))
				{
					info.Category = SymbolFunctionInfo.FunctionCategory.Destructor;
				}
				else
				{
					bool nameMatches = functionMember.Name == typeFullInfo.Name;

					if (nameMatches)
					{
						info.Category = SymbolFunctionInfo.FunctionCategory.Constructor;
					}
					else
					{
						info.Category = SymbolFunctionInfo.FunctionCategory.Function;
					}

				}

				// Set function properties
				info.Virtual = functionMember.IsVirtual;
				info.IsOverride = functionMember.IsVirtual && !functionMember.IsIntro;
				info.IsPure = functionMember.IsPure;
				info.IsConst = functionMember.IsConst;

				// Build function signature
				info.Name = functionMember.Name;
				if (info.IsPure)
				{
					outSymbolInfo.IsAbstract = true;
				}

				// Extract function signature (return type and parameters) directly from functionMember
				if (info.Category == SymbolFunctionInfo.FunctionCategory.Constructor)
				{
					info.ReturnType = string.Empty;
				}
				else if (!string.IsNullOrEmpty(functionMember.ReturnType))
				{
					info.ReturnType = functionMember.ReturnType;
				}

				if (!string.IsNullOrEmpty(functionMember.Parameters))
				{
					info.Parameters = functionMember.Parameters;
				}
				else
				{
					info.Parameters = string.Empty;
				}

				// Create unique signature for deduplication
				// Include name, parameters, const qualifier, and virtual/static to distinguish overloads
				string signature = $"{info.Name}({info.Parameters}){(info.IsConst ? " const" : "")}{(info.Virtual ? " virtual" : "")}{(info.Category == SymbolFunctionInfo.FunctionCategory.StaticFunction ? " static" : "")}";

				// Skip duplicates (compiler generates multiple destructor variants with same visible signature)
				if (addedFunctions.Add(signature))
				{
					outSymbolInfo.AddFunction(info);
				}

			}
		}

		/// <summary>
		/// Override to implement lazy loading of functions
		/// </summary>
		protected override void EnsureFunctionsLoadedForSymbol(SymbolInfo symbolInfo)
		{
			if (pdbFile == null)
			{
				// PDB was closed, reopen it
				pdbFile = new PDB(FileName);
			}

			EnsureFunctionsLoaded(symbolInfo);
			symbolInfo.CheckOverride();
			symbolInfo.CheckMasking();
		}

		/// <summary>
		/// Ensures functions are loaded for the given symbol and all related symbols (base and derived classes)
		/// </summary>
		private void EnsureFunctionsLoaded(SymbolInfo symbolInfo, bool loadDerivedClass = true)
		{
			if (symbolInfo == null)
				return; // Already loaded


			// Load functions for this symbol
			LoadFunctionsForSymbol(symbolInfo);

			// Load functions for all base classes recursively
			foreach (var member in symbolInfo.Members)
			{
				if (member.Category == SymbolMemberInfo.MemberCategory.Base && member.TypeInfo != null)
				{
					EnsureFunctionsLoaded(member.TypeInfo, false);
				}
			}

			// Load functions for all derived classes
			if (symbolInfo.DerivedClasses != null && loadDerivedClass)
			{
				foreach (var derivedClass in symbolInfo.DerivedClasses)
				{
					EnsureFunctionsLoaded(derivedClass);
				}
			}
		}

		private void LoadFunctionsForSymbol(SymbolInfo symbolInfo)
		{
			// Fast path: already loaded
			if (symbolInfo.Functions != null)
				return;

			// Double-checked lock: pdbFile reads are thread-safe (read-only data),
			// but Functions assignment must be atomic to avoid duplicate work.
			lock (symbolInfo)
			{
				if (symbolInfo.Functions != null)
					return; // Loaded by another thread while we waited

				symbolInfo.Functions = new List<SymbolFunctionInfo>();
				var typeInfo = pdbFile.FindType(symbolInfo.Name, true);
				if (typeInfo.HasValue)
				{
					var symFullInfo = pdbFile.GetFullTypeInfo(typeInfo.Value);
					ProcessFunctions(symbolInfo, pdbFile, symFullInfo);
					symbolInfo.SortFunctions();
				}
			}
		}

		/// <summary>
		/// Thread-safe flat load: loads functions for this symbol only, no cascade.
		/// Used by the parallel bulk loader in LoadAllFunctions.
		/// </summary>
		protected override void LoadFunctionsForSymbolOnly(SymbolInfo symbolInfo)
		{
			LoadFunctionsForSymbol(symbolInfo);
		}
	}
}
