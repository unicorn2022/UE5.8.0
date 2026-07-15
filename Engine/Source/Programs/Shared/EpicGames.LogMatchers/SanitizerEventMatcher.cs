// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Issues.Handlers;
using Microsoft.Extensions.Logging;
using System.Collections.Generic;
using System.Text.RegularExpressions;

#pragma warning disable MA0016

namespace EpicGames.LogMatchers
{
	/// <summary>
	/// Matcher for LLVM Sanitizer reports
	/// 
	/// </summary>
	public class SanitizerEventMatcher : ILogEventMatcher
	{
		/// <summary>
		/// Regex pattern that matches Report Start
		/// </summary>
		// e.g.
		// ASAN:		 =================================================================
		// TSAN:		 ==================
		// RaceDetector: =========================================
		public static readonly Regex ReportStartPattern = new(@"(^=+$)", RegexOptions.Multiline | RegexOptions.ExplicitCapture);

		/// <summary>
		/// Regex pattern that matches Report End
		/// </summary>
		// e.g.
		// ASAN:		 ==30562==ABORTING
		// TSAN:		 ==================
		// RaceDetector: =========================================
		public static readonly Regex ReportEndPattern = new(@"(End of Address Sanitizer report)|(^==\d+==(\s*\w+)?$)|(^=+$)", RegexOptions.Multiline | RegexOptions.ExplicitCapture);

		/// <summary>
		/// Regex pattern that matches Report Level
		/// </summary>
		// e.g.
		// ASAN:		 ==30562==ERROR: AddressSanitizer: heap-use-after-free on address 0x617002aa8418 at pc 0x7f98a08bd090 bp 0x7ffc30203af0 sp 0x7ffc30203ae8
		// TSAN:		 WARNING: ThreadSanitizer: data race (pid=6049)
		// RaceDetector: WARNING: RaceDetector: data race detected
		// Note, non-LLVM formats capture a SummaryReason group instead of Summary since we can't be sure if a separate summary line will exist
		public static readonly Regex ReportLevelPattern = new(
			@"^[\s\t]*(==\d+(?:\[0x\w+\])?==\s*)?" +
			@"(?<ReportLevel>WARNING|ERROR):\s+" +
			@"(" +
				@"((?<SanitizerName>\w+)Sanitizer:\s*(?<Summary>.+))" + // LLVM report format
				@"|" +
				@"((?<SanitizerName>\w+):\s*(?<SummaryReason>.+))" +   // Non-LLVM report format
			@")$", RegexOptions.Multiline | RegexOptions.ExplicitCapture);

		/// <summary>
		/// Regex pattern that matches Stack Trace
		/// </summary>
		// e.g. Note line number, column number and BuildId are optional
		// ASAN: #0 0x7f98a08bd08f in UObjectBase::GetFName() const /mnt/somepath/file.h:90:10
		// TSAN: #0 MyType<Type, OtherType>::Function(unsigned long*&) const /mnt/somepath/file.h:90:10 (BinaryName+0x2bc102e0) (BuildId: 8b17597a9f5444a0)
		public static readonly Regex StackTracePattern = new(
			@"^[\s\t]+\#[\d]+\s+(0x[0-9a-fA-F]+\s+in\s+)?" + // The optional capture group is to handle ASAN's slightly different stack trace syntax from TSAN
			@"(?<Symbol>.+?)\s" +
			@"(?<SourceFile>(\/|\w:).+?)" +
			@"(:(?<Line>\d+?))?" +
			@"(:(?<Column>\d+?))?" +
			@"(\s+\(.+?\+(?<Address>0x[0-9a-fA-F]+?)\))?" +
			@"(\s+?\(BuildId:\s+?(?<BuildId>.+?)\))?$", RegexOptions.Multiline | RegexOptions.ExplicitCapture);

		/// <summary>
		/// Regex pattern that matches Stack Trace used by RaceDetector
		/// </summary>
		// e.g. This ordering is used to support VisualStudio logline hyperlinks
		// RaceDetector: D:\build\++Fortnite\Sync\Engine\Source\Runtime\AssetRegistry\Private\AssetDataGatherer.cpp (1648):     [Inline Frame] UE::AssetDataGather::Private::FAssetDataDiscovery::IsSynchronous() 0x00007FFD6F6801B7",
		public static readonly Regex RaceDetectorStackTracePattern = new(
			@"^(?<SourceFile>(\/|\w:)[^\(]+)" +
			@"(\s\((?<Line>\d+)\))?:\s+" +
			@"(?<Symbol>.+)\s" +
			@"(?<Address>0x[\w]+)$", RegexOptions.Multiline | RegexOptions.ExplicitCapture);

		/// <summary>
		/// Regex pattern that matches Summary
		/// </summary>
		// e.g.
		// ASAN: SUMMARY: AddressSanitizer: heap-use-after-free /some/path.cpp:169:33 in UObjectBase::GetFName() const
		// TSAN: SUMMARY: ThreadSanitizer: data race /some/path.cpp:169:33 in MyType::Foo()
		// RaceDetector: Doesn't provide a summary line so we must infer one from the whole report
		public static readonly Regex SummaryPattern = new(
			@"^[\s]*SUMMARY:\s+(?<SanitizerName>\w+)Sanitizer:\s*" +
			@"(?<SummaryReason>[\w\s\-\(\)]+)\s" +
			@"\(?(?<SourceFile>.+?)" +
			@"(\+(?<Address>0[xX][0-9a-fA-F]+?)\))?" +
			@"(:(?<Line>\d+?))?" +
			@"(:(?<Column>\d+?))?" +
			@"(\s*\(BuildId:\s+?(?<BuildId>.+?)\))?" +
			@"(\s+(in|at)\s+(\((?<Symbol>.+)\)|(?<Symbol>.+)))?$", RegexOptions.Multiline | RegexOptions.ExplicitCapture);

		/// <summary>
		/// Convert SanitizerName to EventId
		/// </summary>
		/// <param name="sanitizerName"></param>
		/// <returns></returns>
		public static EventId ConvertSanitizerNameToEventId(string sanitizerName)
		{
			switch (sanitizerName)
			{
				case "Thread": return KnownLogEvents.Sanitizer_Thread;
				case "Address": return KnownLogEvents.Sanitizer_Address;
				case "RaceDetector": return KnownLogEvents.Sanitizer_RaceDetector;
			}

			return KnownLogEvents.Sanitizer;
		}

		/// <summary>
		/// Convert ReportLevel string to LogLevel enum
		/// </summary>
		/// <param name="reportLevel"></param>
		/// <returns></returns>
		public static LogLevel ConvertReportLevel(string reportLevel)
		{
			switch (reportLevel)
			{
				case "WARNING": return LogLevel.Warning;
				case "ERROR": return LogLevel.Error;
			}
			return LogLevel.Error;
		}

		/// <summary>
		/// Add Value to Properties using (Regex) Group match and update Message with the corresponding key instead of value from Group.
		/// </summary>
		/// <param name="value"></param>
		/// <param name="group"></param>
		/// <param name="properties"></param>
		/// <param name="message"></param>
		/// <param name="offset">Optional argument to offset Group index from Message if Message changed since Group was initialy matched</param>
		private static void AddPropertyAndReplace(LogValue value, Group group, Dictionary<string, object> properties, ref string message, int offset = 0)
		{
			string key = value.Type.ToString();
			value.Text = group.Value;
			properties.Add(key, value);
			int index = group.Index + offset;
			message = $"{message.Substring(0, index)}{{{key}}}{message.Substring(index + group.Length)}";
		}

#pragma warning disable CA1045 // Do not pass types by reference
		/// <summary>
		/// Add Sanitizer Summary information to input Key/Value Pair Properties and update Message with Key markers
		/// </summary>
		/// <param name="message"></param>
		/// <param name="properties"></param>
		/// <returns></returns>
		public static bool AddSanitizerSummaryProperties(ref string message, Dictionary<string, object> properties)
		{
			Match match = SummaryPattern.Match(message);
			if (match.Success)
			{
				int initialLength = message.Length;
				AddPropertyAndReplace(SanitizerIssueHandler.SanitizerName, match.Groups["SanitizerName"], properties, ref message);
				AddPropertyAndReplace(SanitizerIssueHandler.SummaryReason, match.Groups["SummaryReason"], properties, ref message, message.Length - initialLength);
				AddPropertyAndReplace(SanitizerIssueHandler.SummarySourceFile, match.Groups["SourceFile"], properties, ref message, message.Length - initialLength);

				return true;
			}

			return false;
		}
#pragma warning restore CA1045

		/// <inheritdoc/> 
		public LogEventMatch? Match(ILogCursor cursor)
		{
			bool bIsRaceDetectorTrace = false;
			bool bFoundFirstRaceDetectorStackTrace = false;

			Match? match;
			if (cursor.TryMatch(ReportStartPattern, out _))
			{
				EventId sanitizerId = KnownLogEvents.Sanitizer;
				LogLevel reportLevel = LogLevel.Information;

				LogEventBuilder builder = new(cursor);
				builder.MoveNext();

				while (builder.Current.CurrentLine != null)
				{
					if (bIsRaceDetectorTrace && builder.Current.TryMatch(RaceDetectorStackTracePattern, out match))
					{
						if (!bFoundFirstRaceDetectorStackTrace)
						{
							bFoundFirstRaceDetectorStackTrace = true;
							builder.Annotate("SummarySourceFile", match.Groups["SourceFile"], SanitizerIssueHandler.SummarySourceFile);
						}

						do
						{
							builder.AnnotateSymbol(match!.Groups["Symbol"]);
							builder.AnnotateSourceFile(match.Groups["SourceFile"], "");
							builder.TryAnnotate(match.Groups["Line"], LogEventMarkup.LineNumber);
							builder.TryAnnotate(match.Groups["Address"], "");

							builder.MoveNext();
						} while (builder.Current.TryMatch(RaceDetectorStackTracePattern, out match));
					}
					else if (!bIsRaceDetectorTrace && builder.Current.TryMatch(StackTracePattern, out match))
					{
						do
						{
							builder.AnnotateSymbol(match!.Groups["Symbol"]);
							builder.AnnotateSourceFile(match.Groups["SourceFile"], "");
							builder.TryAnnotate(match.Groups["Line"], LogEventMarkup.LineNumber);
							builder.TryAnnotate(match.Groups["Column"], LogEventMarkup.ColumnNumber);
							builder.TryAnnotate(match.Groups["Address"], "");
							builder.TryAnnotate(match.Groups["BuildId"], "");

							builder.MoveNext();
						} while (builder.Current.TryMatch(StackTracePattern, out match));
					}
					else
					{
						if (builder.Current.TryMatch(ReportLevelPattern, out match))
						{
							sanitizerId = ConvertSanitizerNameToEventId(match.Groups["SanitizerName"].Value);
							reportLevel = ConvertReportLevel(match.Groups["ReportLevel"].Value);
							bIsRaceDetectorTrace = sanitizerId == KnownLogEvents.Sanitizer_RaceDetector;

							// These annotations are used by IssueHandler
							Group summaryReason = match.Groups["SummaryReason"];
							if (summaryReason.Success)
							{
								// SummaryReason is only set on ReportLevel lines for non-LLVM sanitizers
								builder.Annotate(summaryReason, SanitizerIssueHandler.SummaryReason);
							}
							builder.Annotate(match.Groups["SanitizerName"], SanitizerIssueHandler.SanitizerName);
						}

						if (builder.Current.TryMatch(SummaryPattern, out match))
						{
							// Used by IssueHandler
							builder.Annotate(match.Groups["SummaryReason"], SanitizerIssueHandler.SummaryReason);
							builder.Annotate("SummarySourceFile", match.Groups["SourceFile"], SanitizerIssueHandler.SummarySourceFile);

							// Annotate the source file again so that we may get UGS link resolution
							builder.AnnotateSourceFile(match.Groups["SourceFile"], "");
							builder.TryAnnotate(match.Groups["Line"], LogEventMarkup.LineNumber);
							builder.TryAnnotate(match.Groups["Column"], LogEventMarkup.ColumnNumber);
							builder.AnnotateSymbol(match.Groups["Symbol"]);
						}
						else if (builder.Current.IsMatch(ReportEndPattern))
						{
							return builder.ToMatch(LogEventPriority.AboveNormal, reportLevel, sanitizerId);
						}

						builder.MoveNext();
					}
				}
			}

			return null;
		}
	}
}
