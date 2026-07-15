using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Xml.Linq;

namespace CruncherSharp
{
	public class LoadPDBTask
	{
		public string FileName { get; set; }
		public string Filter { get; set; }
		public bool SecondPDB { get; set; }
		public bool MatchCase { get; set; }
		public bool WholeExpression { get; set; }
		public bool UseRegularExpression { get; set; }
		public bool UseProgressBar { get; set; }
	}

	public class LoadCSVTask
	{
		public string ClassName { get; set; }
		public ulong Count { get; set; }
	}

	public abstract class SymbolAnalyzer
	{
		public Dictionary<string, SymbolInfo> Symbols { get; }
		public SortedSet<string> RootNamespaces { get; }
		public SortedSet<string> Namespaces { get; }
		public string LastError { get; private set; }
		public string FileName { get; set; }

		public Dictionary<string, uint> ConfigAlignment { get; set; }
		public Dictionary<string, uint> ConfigEnum { get; set; }

		private List<uint> _MemPools;
		public List<uint> MemPools
		{
			get => _MemPools;
			set
			{
				_MemPools = value;
				foreach (var symbol in Symbols.Values)
				{
					symbol.SetMemPools(_MemPools);
				}
			}
		}

		public SymbolAnalyzer()
		{
			Symbols = new Dictionary<string, SymbolInfo>();
			ConfigAlignment = new Dictionary<string, uint>();
			ConfigEnum = new Dictionary<string, uint>();
			RootNamespaces = new SortedSet<string>();
			Namespaces = new SortedSet<string>();
			_MemPools = new List<uint>();
			LastError = string.Empty;
		}

		public void Reset()
		{
			RootNamespaces.Clear();
			Namespaces.Clear();
			Symbols.Clear();
			LastError = string.Empty;
			FileName = null;
		}

		public virtual void OpenAsync(string filename)
		{
			FileName = filename;
		}

		public bool LoadPdb(object sender, DoWorkEventArgs e)
		{
			try
			{
				var task = e.Argument as LoadPDBTask;
				FileName = task.FileName;
				if (!LoadPdb(sender, task))
				{
					e.Cancel = true;
					return false;
				}

				return true;
			}
			catch (System.Runtime.InteropServices.COMException exception)
			{
				LastError = exception.ToString();
				return false;
			}
		}

		public abstract bool LoadPdb(object sender, LoadPDBTask task);


		protected void RunAnalysis()
		{
			foreach (var symbol in Symbols.Values)
			{
				symbol.UpdateBaseClass(this);
			}

			foreach (var symbol in Symbols.Values)
			{
				symbol.ComputeTotalPadding();
			}

			foreach (var Name in ConfigAlignment.Keys)
			{
				if (Symbols.TryGetValue(Name, out SymbolInfo symbol))
				{
					ConfigAlignment.TryGetValue(Name, out var Alignment);
					symbol.MinAlignment = Alignment;
				}
			}

			foreach (var symbol in Symbols.Values)
			{
				symbol.ComputeMinAlignment();
				symbol.ComputePotentialSaving(this);
			}
		}
		public bool HasSymbolInfo(string name)
		{
			return Symbols.ContainsKey(name);
		}

		public bool LoadCSV(object sender, DoWorkEventArgs e)
		{
			try
			{
				var task = e.Argument as List<LoadCSVTask>;
				if (!LoadCSV(sender, task))
				{
					e.Cancel = true;
					return false;
				}
				return true;
			}
			catch (System.Runtime.InteropServices.COMException exception)
			{
				LastError = exception.ToString();
				return false;
			}
		}

		public abstract bool LoadCSV(object sender, List<LoadCSVTask> tasks);

		public SymbolInfo FindSymbolInfo(string name, bool loadMissingSymbol = false)
		{
			if (!HasSymbolInfo(name) && name.Length > 0)
			{
				if (!loadMissingSymbol)
					return null;
				var task = new LoadPDBTask
				{
					FileName = FileName,
					SecondPDB = false,
					Filter = name,
					MatchCase = true,
					WholeExpression = true,
					UseRegularExpression = false,
					UseProgressBar = false
				};

				if (!LoadPdb(null, task))
					return null;
			}

			Symbols.TryGetValue(name, out var symbolInfo);

			// Ensure functions are loaded for this symbol (lazy loading)
			if (symbolInfo != null && loadMissingSymbol)
			{
				EnsureFunctionsLoadedForSymbol(symbolInfo);
			}

			return symbolInfo;
		}

		/// <summary>
		/// Virtual method to ensure functions are loaded for a symbol (used for lazy loading)
		/// </summary>
		protected virtual void EnsureFunctionsLoadedForSymbol(SymbolInfo symbolInfo)
		{
			// Default implementation does nothing - override in derived classes for lazy loading
		}

		/// <summary>
		/// Loads functions for a single symbol without cascading to base/derived classes.
		/// Override in derived classes to provide a thread-safe flat implementation.
		/// </summary>
		protected virtual void LoadFunctionsForSymbolOnly(SymbolInfo symbolInfo)
		{
			EnsureFunctionsLoadedForSymbol(symbolInfo);
		}

		/// <summary>
		/// Loads functions for all symbols in parallel, then runs CheckOverride and CheckMasking on all of them.
		/// </summary>
		public bool LoadAllFunctions(object sender, DoWorkEventArgs e)
		{
			var worker = sender as BackgroundWorker;
			var symbols = Symbols.Values.ToList();
			int completed = 0;
			int cancelledFlag = 0;

			Parallel.ForEach(symbols, symbol =>
			{
				if (Volatile.Read(ref cancelledFlag) != 0)
					return;
				if (worker?.CancellationPending == true)
				{
					Volatile.Write(ref cancelledFlag, 1);
					return;
				}

				LoadFunctionsForSymbolOnly(symbol);

				int current = Interlocked.Increment(ref completed);
				if (current % 500 == 0)
					worker?.ReportProgress(current * 75 / symbols.Count, $"Loading functions: {current}/{symbols.Count}");
			});

			if (Volatile.Read(ref cancelledFlag) != 0 || worker?.CancellationPending == true)
			{
				e.Cancel = true;
				return false;
			}

			worker?.ReportProgress(80, "Running function analysis...");
			foreach (var symbol in symbols)
			{
				symbol.CheckOverride();
				symbol.CheckMasking();
			}
			worker?.ReportProgress(100, $"Functions loaded for {symbols.Count} symbols");
			return true;
		}
	}
}
