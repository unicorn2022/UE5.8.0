using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace CruncherSharp
{
	/// <summary>
	/// Helper class for reading and writing INI files
	/// </summary>
	internal static class IniFileHelper
	{
		[DllImport("kernel32", CharSet = CharSet.Unicode)]
		private static extern int GetPrivateProfileString(string section, string key, string defaultValue, StringBuilder retVal, int size, string filePath);

		[DllImport("kernel32", CharSet = CharSet.Unicode)]
		private static extern long WritePrivateProfileString(string section, string key, string value, string filePath);

		public static string Read(string section, string key, string defaultValue, string filePath)
		{
			var retVal = new StringBuilder(255);
			GetPrivateProfileString(section, key, defaultValue, retVal, 255, filePath);
			return retVal.ToString();
		}

		public static void Write(string section, string key, string value, string filePath)
		{
			WritePrivateProfileString(section, key, value, filePath);
		}

		public static bool ReadBool(string section, string key, bool defaultValue, string filePath)
		{
			string value = Read(section, key, defaultValue.ToString(), filePath);
			return bool.TryParse(value, out bool result) ? result : defaultValue;
		}

		public static void WriteBool(string section, string key, bool value, string filePath)
		{
			Write(section, key, value.ToString(), filePath);
		}
	}
}
