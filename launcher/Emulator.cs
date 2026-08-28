using System.Diagnostics;
using System.IO;
using System.Reflection;
using Microsoft.Win32;

namespace NyxLauncher;

/// <summary>
/// Finds QEMU, unpacks the kernel, and starts the machine. Kept away from the
/// window so the UI has nothing to do but call three methods.
/// </summary>
public static class Emulator
{
    private static readonly string[] LikelyPaths =
    {
        @"C:\Program Files\qemu\qemu-system-i386.exe",
        @"C:\Program Files (x86)\qemu\qemu-system-i386.exe",
        @"C:\qemu\qemu-system-i386.exe",
    };

    /// <summary>Full path to qemu-system-i386.exe, or null if it is not installed.</summary>
    public static string? FindQemu()
    {
        foreach (var p in LikelyPaths)
            if (File.Exists(p)) return p;

        // installed somewhere unusual but on PATH
        var path = Environment.GetEnvironmentVariable("PATH") ?? "";
        foreach (var dir in path.Split(';', StringSplitOptions.RemoveEmptyEntries))
        {
            try
            {
                var candidate = Path.Combine(dir.Trim(), "qemu-system-i386.exe");
                if (File.Exists(candidate)) return candidate;
            }
            catch (ArgumentException) { /* malformed PATH entry */ }
        }

        // the installer records its location here
        try
        {
            using var key = Registry.LocalMachine.OpenSubKey(
                @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\QEMU");
            var loc = key?.GetValue("InstallLocation") as string;
            if (!string.IsNullOrEmpty(loc))
            {
                var candidate = Path.Combine(loc, "qemu-system-i386.exe");
                if (File.Exists(candidate)) return candidate;
            }
        }
        catch (Exception) { /* registry unavailable, fall through */ }

        return null;
    }

    /// <summary>Writes the embedded kernel next to the user's temp folder and returns its path.</summary>
    public static string ExtractKernel()
    {
        var dir = Path.Combine(Path.GetTempPath(), "nyx");
        Directory.CreateDirectory(dir);
        var dest = Path.Combine(dir, "nyx.elf");

        var asm = Assembly.GetExecutingAssembly();
        var name = asm.GetManifestResourceNames()
                      .FirstOrDefault(n => n.EndsWith("nyx.elf", StringComparison.OrdinalIgnoreCase))
                   ?? throw new FileNotFoundException("The kernel is missing from this build.");

        using var src = asm.GetManifestResourceStream(name)
                        ?? throw new FileNotFoundException("The kernel could not be read.");
        using var outFile = File.Create(dest);
        src.CopyTo(outFile);
        return dest;
    }

    /// <summary>Boots the kernel in its own window.</summary>
    public static Process Start(string qemu, string kernel)
    {
        var psi = new ProcessStartInfo(qemu)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        psi.ArgumentList.Add("-kernel");      psi.ArgumentList.Add(kernel);
        psi.ArgumentList.Add("-m");           psi.ArgumentList.Add("64");
        psi.ArgumentList.Add("-no-reboot");
        psi.ArgumentList.Add("-name");        psi.ArgumentList.Add("nyx");
        // no -serial stdio: that would pop a console window next to the machine
        return Process.Start(psi) ?? throw new InvalidOperationException("QEMU did not start.");
    }

    /// <summary>Runs the official winget package install, visibly, and waits.</summary>
    public static Process InstallQemu()
    {
        var psi = new ProcessStartInfo("winget")
        {
            UseShellExecute = true,     // show the console so the user sees what happens
        };
        psi.ArgumentList.Add("install");
        psi.ArgumentList.Add("--id");
        psi.ArgumentList.Add("SoftwareFreedomConservancy.QEMU");
        psi.ArgumentList.Add("--accept-source-agreements");
        psi.ArgumentList.Add("--accept-package-agreements");
        return Process.Start(psi) ?? throw new InvalidOperationException("winget did not start.");
    }
}
