// KytyPS5 Launcher.exe: opens the settings window (launcher.ps1, next to this exe) the way launcher.cmd does, without a
// console window flashing up: a real program to pin, to add to Steam or EmuDeck, or to give an icon. launcher.ps1 stays
// the launcher itself, so updates and patches to it keep working; this only starts it. Arguments are passed on
// (for example -DryRun). Built by make-launcher-exe.cmd with the C# compiler every Windows 10/11 has.
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

[assembly: System.Reflection.AssemblyTitle("KytyPS5 Launcher")]
[assembly: System.Reflection.AssemblyProduct("KytyPS5")]
[assembly: System.Reflection.AssemblyVersion("1.0.0.0")]

static class KytyLauncher {
	[DllImport("user32.dll", CharSet = CharSet.Unicode)]
	static extern int MessageBoxW(IntPtr owner, string text, string caption, uint type);

	static int Fail(string text) {
		MessageBoxW(IntPtr.Zero, text, "KytyPS5 Launcher", 0x10); // MB_ICONERROR
		return 1;
	}

	static string Quote(string arg) {
		if (arg.Length > 0 && arg.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) { return arg; }
		return "\"" + arg.Replace("\"", "\\\"") + "\"";
	}

	[STAThread]
	static int Main(string[] args) {
		string folder = AppDomain.CurrentDomain.BaseDirectory;
		string script = Path.Combine(folder, "launcher.ps1");
		if (!File.Exists(script)) {
			return Fail("launcher.ps1 is not next to this program:\n" + folder + "\n\nKeep KytyPS5 Launcher.exe in the KytyPS5 folder.");
		}
		// Windows PowerShell by its full path (launcher.cmd's reason: a PATH without its folder, issue #55).
		string powershell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
			@"WindowsPowerShell\v1.0\powershell.exe");
		if (!File.Exists(powershell)) { powershell = "powershell.exe"; }

		string arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File " + Quote(script);
		foreach (string arg in args) { arguments += " " + Quote(arg); }
		var start = new ProcessStartInfo(powershell, arguments) {
			UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = folder
		};
		try {
			Process.Start(start);
		} catch (Exception error) {
			return Fail("Could not start Windows PowerShell:\n" + error.Message);
		}
		return 0;
	}
}
