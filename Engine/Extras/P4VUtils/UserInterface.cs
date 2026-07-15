// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.IO;
using Microsoft.Extensions.Logging;
using System.Linq;
using System.Collections.Generic;
using System.Threading.Tasks;
using System.Threading.Channels;



#if IS_WINDOWS
using System.Windows.Forms;
#endif


namespace P4VUtils
{
	/// <summary>Cross-platform UI helpers for dialogs, clipboard, and process launching</summary>
	public static class UserInterface
	{
#if IS_WINDOWS
		[System.Runtime.InteropServices.DllImport("user32.dll")]
		private static extern bool SetProcessDPIAware();

		/// <summary>Configures DPI awareness and visual styles for the application</summary>
		public static void SetupVisuals()
		{
			// make the form look good on modern displays!
			SetProcessDPIAware();

			Application.EnableVisualStyles();
			Application.SetCompatibleTextRenderingDefault(false);
		}
#else
		/// <summary>Configures DPI awareness and visual styles for the application</summary>
		public static void SetupVisuals() { }
#endif

		static UserInterface()
		{
			SetupVisuals();
		}


		#region Dialog Boxes

		/// <summary>Dialog button identifiers</summary>
		public enum Button
		{
			/// <summary>Yes button</summary>
			Yes,
			/// <summary>No button</summary>
			No,
			/// <summary>OK button</summary>
			OK,
			/// <summary>Cancel button</summary>
			Cancel,
		}

		/// <summary>Button set for Yes/No/Cancel dialogs</summary>
		public static readonly Button[] YesNoCancel = { Button.Yes, Button.No, Button.Cancel };
		/// <summary>Button set for Yes/No dialogs</summary>
		public static readonly Button[] YesNo = { Button.Yes, Button.No };
		/// <summary>Button set for simple OK dialogs</summary>
		public static readonly Button[] OK = { Button.OK };
		/// <summary>Button set for OK/Cancel dialogs</summary>
		public static readonly Button[] OKCancel = { Button.OK, Button.Cancel };

#if IS_WINDOWS
		private static Dictionary<Button[], MessageBoxButtons> ButtonsToWindows = new () {
			{ YesNoCancel, MessageBoxButtons.YesNoCancel },
			{ YesNo, MessageBoxButtons.YesNo },
			{ OK, MessageBoxButtons.OK },
			{ OKCancel, MessageBoxButtons.OKCancel },
		};

		private static Dictionary<DialogResult, Button> WindowsToButton = new () {
			{ DialogResult.Yes, Button.Yes },
			{ DialogResult.No, Button.No },
			{ DialogResult.OK, Button.OK },
			{ DialogResult.Cancel, Button.Cancel },
		};
#endif

		/// <summary>Shows an informational dialog with a single OK button</summary>
		/// <param name="Message">Message to display</param>
		/// <param name="Title">Dialog title</param>
		/// <param name="Logger">Logger for fallback output on non-GUI platforms</param>
		public static void ShowSimpleDialog(string Message, String Title, ILogger Logger)
		{
			ShowDialog(Message, Title, OK, Button.OK, Logger);
		}

		/// <summary>Shows a dialog with a configurable set of buttons and returns the button the user clicked</summary>
		/// <param name="Message">Message to display</param>
		/// <param name="Title">Dialog title</param>
		/// <param name="Buttons">Buttons to present</param>
		/// <param name="DefaultButton">Button returned when no GUI is available</param>
		/// <param name="Logger">Logger for fallback output on non-GUI platforms</param>
		/// <returns>The button the user clicked</returns>
		public static Button ShowDialog(string Message, string Title, Button[] Buttons, Button DefaultButton, ILogger Logger)
		{
			Button Response = Button.OK;

			if (OperatingSystem.IsWindows())
			{
#if IS_WINDOWS
				DialogResult Result = MessageBox.Show(Message, Title, ButtonsToWindows[Buttons], MessageBoxIcon.Information);
				Response = WindowsToButton[Result];
#endif
			}
			else if (OperatingSystem.IsMacOS())
			{
				string ButtonString = string.Join(", ", Buttons.Select(x => $"\\\"{x}\\\""));
				int DefaultIndex = Buttons.ToList().IndexOf(DefaultButton) + 1;


				string Output = RunProcessAsync("/bin/bash",
					$"-c \" osascript -e 'display dialog \\\"{Message}\\\" buttons {{{ButtonString}}} default button {DefaultIndex} with icon caution'\"").Result;

				Output = Output.Replace("button returned:", "", StringComparison.OrdinalIgnoreCase);
				Response = Enum.Parse<Button>(Output);
			}
			else if (OperatingSystem.IsLinux())
			{
				// Try zenity for GUI dialogs, fall back to kdialog, then log-only
				string? ZenityPath = null;
				string? KDialogPath = null;
				try
				{
					ZenityPath = RunProcessAsync("/usr/bin/env", "bash -c \"which zenity 2>/dev/null\"").Result;
					if (string.IsNullOrEmpty(ZenityPath)) { ZenityPath = null; }
				}
				catch { }
				if (ZenityPath == null)
				{
					try
					{
						KDialogPath = RunProcessAsync("/usr/bin/env", "bash -c \"which kdialog 2>/dev/null\"").Result;
						if (string.IsNullOrEmpty(KDialogPath)) { KDialogPath = null; }
					}
					catch { }
				}

				if (ZenityPath != null)
				{
					string EscapedMessage = ShellEscape(Message);
					string EscapedTitle = ShellEscape(Title);

					if (Buttons.Length == 1 && Buttons[0] == Button.OK)
					{
						// Simple info dialog
						RunProcessWithExitCodeAsync("/usr/bin/env", $"bash -c \"zenity --info --title=\\\"{EscapedTitle}\\\" --text=\\\"{EscapedMessage}\\\" --width=400\"").Wait();
						Response = Button.OK;
					}
					else if (Buttons.SequenceEqual(YesNo))
					{
						// zenity: exit 0 = OK button, exit 1 = Cancel button
						var (_, ExitCode) = RunProcessWithExitCodeAsync("/usr/bin/env", $"bash -c \"zenity --question --title=\\\"{EscapedTitle}\\\" --text=\\\"{EscapedMessage}\\\" --ok-label=Yes --cancel-label=No --width=400\"").Result;
						Response = ExitCode == 0 ? Button.Yes : Button.No;
					}
					else if (Buttons.SequenceEqual(OKCancel))
					{
						var (_, ExitCode) = RunProcessWithExitCodeAsync("/usr/bin/env", $"bash -c \"zenity --question --title=\\\"{EscapedTitle}\\\" --text=\\\"{EscapedMessage}\\\" --ok-label=OK --cancel-label=Cancel --width=400\"").Result;
						Response = ExitCode == 0 ? Button.OK : Button.Cancel;
					}
					else if (Buttons.SequenceEqual(YesNoCancel))
					{
						// zenity --extra-button prints button label to stdout and exits with code 1
						// OK button (Yes): exit 0, no stdout
						// Cancel button (No): exit 1, no stdout
						// Extra button (Cancel): exit 1, prints "Cancel" to stdout
						var (Output, ExitCode) = RunProcessWithExitCodeAsync("/usr/bin/env", $"bash -c \"zenity --question --title=\\\"{EscapedTitle}\\\" --text=\\\"{EscapedMessage}\\\" --ok-label=Yes --cancel-label=No --extra-button=Cancel --width=400\"").Result;
						if (ExitCode == 0)
						{
							Response = Button.Yes;
						}
						else if (Output == "Cancel")
						{
							Response = Button.Cancel;
						}
						else
						{
							Response = Button.No;
						}
					}
				}
				else if (KDialogPath != null)
				{
					string EscapedMessage = ShellEscape(Message);
					string EscapedTitle = ShellEscape(Title);

					if (Buttons.Length == 1 && Buttons[0] == Button.OK)
					{
						RunProcessWithExitCodeAsync("/usr/bin/env", $"bash -c \"kdialog --title \\\"{EscapedTitle}\\\" --msgbox \\\"{EscapedMessage}\\\"\"").Wait();
						Response = Button.OK;
					}
					else if (Buttons.SequenceEqual(YesNo))
					{
						// kdialog --yesno: exit 0 = Yes, exit 1 = No
						var (_, ExitCode) = RunProcessWithExitCodeAsync("/usr/bin/env", $"bash -c \"kdialog --title \\\"{EscapedTitle}\\\" --yesno \\\"{EscapedMessage}\\\"\"").Result;
						Response = ExitCode == 0 ? Button.Yes : Button.No;
					}
					else if (Buttons.SequenceEqual(OKCancel))
					{
						var (_, ExitCode) = RunProcessWithExitCodeAsync("/usr/bin/env", $"bash -c \"kdialog --title \\\"{EscapedTitle}\\\" --yesno \\\"{EscapedMessage}\\\" --yes-label OK --no-label Cancel\"").Result;
						Response = ExitCode == 0 ? Button.OK : Button.Cancel;
					}
					else if (Buttons.SequenceEqual(YesNoCancel))
					{
						// kdialog --yesnocancel: exit 0 = Yes, exit 1 = No, exit 2 = Cancel
						var (_, ExitCode) = RunProcessWithExitCodeAsync("/usr/bin/env", $"bash -c \"kdialog --title \\\"{EscapedTitle}\\\" --yesnocancel \\\"{EscapedMessage}\\\"\"").Result;
						if (ExitCode == 0)
						{
							Response = Button.Yes;
						}
						else if (ExitCode == 2)
						{
							Response = Button.Cancel;
						}
						else
						{
							Response = Button.No;
						}
					}
				}
				else
				{
					Logger.LogWarning("{Message}", Message);
					if (Buttons.Length > 1)
					{
						Logger.LogWarning("Note: This dialog was meant to have buttons {Buttons}, returning {Default}. Install zenity or kdialog for GUI dialogs.", string.Join(", ", Buttons), DefaultButton);
					}
					Response = DefaultButton;
				}
			}
			else
			{
				Logger.LogWarning("{Message}", Message);
				if (Buttons.Length > 1)
				{
					Logger.LogWarning("Note: This message oringally was meant for a dialog with buttons {Buttons}, returning {Default}", string.Join(", ", Buttons), DefaultButton);
				}
				Response = DefaultButton;
			}

			return Response;
		}

		#endregion


		#region Clipboard

		/// <summary>Copies the given text to the system clipboard</summary>
		/// <param name="Text">Text to copy</param>
		/// <param name="Logger">Logger for error output when clipboard access fails</param>
		public static async Task CopyTextToClipboard(string Text, ILogger Logger)
		{
			if (OperatingSystem.IsWindows())
			{
#if IS_WINDOWS
				// Jump through some hoops to make async & windows com happy and force STA on the clipboard call
				var tcs = new TaskCompletionSource<int>();

				var SetClipThread = new System.Threading.Thread(() => 
					{ 
						System.Windows.Forms.Clipboard.SetText(Text);
						tcs.SetResult(0);
					}
				);

				SetClipThread.SetApartmentState(System.Threading.ApartmentState.STA);
				SetClipThread.Start();
				SetClipThread.Join();

				await tcs.Task;
#endif
			}
			else if (OperatingSystem.IsMacOS())
			{
				await UserInterface.RunProcessAsync("/bin/bash", $" -c \"osascript -e 'set the clipboard to \\\"{Text}\\\"'\"");
			}
			else if (OperatingSystem.IsLinux())
			{
				// Try xclip, xsel, then wl-copy (Wayland) in order of preference
				string[] ClipboardCommands = new[]
				{
					"xclip -selection clipboard",
					"xsel --clipboard --input",
					"wl-copy"
				};

				bool Copied = false;
				foreach (string ClipCmd in ClipboardCommands)
				{
					string[] Parts = ClipCmd.Split(' ', 2);
					string Executable = Parts[0];

					try
					{
						// Check if the tool exists
						string WhichOutput = await RunProcessAsync("/usr/bin/env", $"bash -c \"which {Executable} 2>/dev/null\"");
						if (string.IsNullOrEmpty(WhichOutput))
						{
							continue;
						}

						int ExitCode = await RunProcessWithStdinAsync("/usr/bin/env", $"bash -c \"{ClipCmd}\"", Text);
						if (ExitCode == 0)
						{
							Copied = true;
							break;
						}
					}
					catch (Exception)
					{
						continue;
					}
				}

				if (!Copied)
				{
					Logger.LogError("Unable to copy to clipboard on Linux. Install xclip, xsel, or wl-copy.");
				}
			}
			else
			{
				Logger.LogError("Unable to copy to clipboard on this platform");
			}
		}

		#endregion


		#region Utils

		/// <summary>Launches a process and returns its stdout as a trimmed string</summary>
		/// <param name="Executable">Path to the executable</param>
		/// <param name="Params">Command-line arguments</param>
		/// <returns>Trimmed stdout output</returns>
		public static async Task<string> RunProcessAsync(string Executable, string Params)
		{
			ProcessStartInfo SI = new ProcessStartInfo(Executable, Params);
			SI.RedirectStandardOutput = true;
			using (Process Proc = Process.Start(SI)!)
			{
				StreamReader Reader = Proc.StandardOutput;
				string Output = (await Reader.ReadToEndAsync()).Trim();

				return Output;
			}
		}

		/// <summary>Launches a process and returns its stdout and exit code</summary>
		/// <param name="Executable">Path to the executable</param>
		/// <param name="Params">Command-line arguments</param>
		/// <returns>Trimmed stdout and the process exit code</returns>
		public static async Task<(string Output, int ExitCode)> RunProcessWithExitCodeAsync(string Executable, string Params)
		{
			ProcessStartInfo SI = new ProcessStartInfo(Executable, Params);
			SI.RedirectStandardOutput = true;
			SI.RedirectStandardError = true;
			using (Process Proc = Process.Start(SI)!)
			{
				string Output = (await Proc.StandardOutput.ReadToEndAsync()).Trim();
				await Proc.WaitForExitAsync();
				return (Output, Proc.ExitCode);
			}
		}

		/// <summary>Launches a process, writes text to its stdin, and returns the exit code</summary>
		/// <param name="Executable">Path to the executable</param>
		/// <param name="Params">Command-line arguments</param>
		/// <param name="StdinText">Text to write to the process stdin</param>
		/// <returns>The process exit code</returns>
		public static async Task<int> RunProcessWithStdinAsync(string Executable, string Params, string StdinText)
		{
			ProcessStartInfo SI = new ProcessStartInfo(Executable, Params);
			SI.RedirectStandardInput = true;
			SI.RedirectStandardOutput = true;
			SI.RedirectStandardError = true;
			using (Process Proc = Process.Start(SI)!)
			{
				await Proc.StandardInput.WriteAsync(StdinText);
				Proc.StandardInput.Close();
				await Proc.WaitForExitAsync();
				return Proc.ExitCode;
			}
		}

		// Escape a string for safe embedding in shell double-quotes
		private static string ShellEscape(string Input)
		{
			return Input
				.Replace("\\", "\\\\", StringComparison.Ordinal)
				.Replace("\"", "\\\"", StringComparison.Ordinal)
				.Replace("$", "\\$", StringComparison.Ordinal)
				.Replace("`", "\\`", StringComparison.Ordinal)
				.Replace("\n", "\\n", StringComparison.Ordinal)
				.Replace("\r", "", StringComparison.Ordinal);
		}

		#endregion
	}
}

