using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

// Tiny launcher: starts tools\iap_gui.py with pythonw.exe (no console window).
// Kept dependency-free so it can be compiled with the .NET Framework csc that
// ships with Windows.
internal static class Launcher
{
    [STAThread]
    private static void Main()
    {
        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        string script = Path.Combine(baseDir, "tools", "iap_gui.py");

        if (!File.Exists(script))
        {
            MessageBox.Show(
                "找不到 tools\\iap_gui.py\n\n请把 exe 和 tools 文件夹放在同一目录下。",
                "A/B IAP 升级工具", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        string pythonw = FindPythonw();
        if (pythonw == null)
        {
            MessageBox.Show(
                "没有找到 pythonw.exe。\n\n请先安装 Python 3（安装时勾选 Add Python to PATH），\n并执行：python -m pip install pyserial",
                "A/B IAP 升级工具", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }

        var psi = new ProcessStartInfo(pythonw)
        {
            Arguments = "\"" + script + "\"",
            WorkingDirectory = baseDir,
            UseShellExecute = false,
            CreateNoWindow = true,
        };

        try
        {
            Process.Start(psi);
        }
        catch (Exception ex)
        {
            MessageBox.Show("启动失败：" + ex.Message, "A/B IAP 升级工具",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private static string FindPythonw()
    {
        string path = Environment.GetEnvironmentVariable("PATH") ?? string.Empty;
        foreach (string dir in path.Split(';'))
        {
            if (dir.Trim().Length == 0) continue;
            try
            {
                string candidate = Path.Combine(dir.Trim(), "pythonw.exe");
                if (File.Exists(candidate)) return candidate;
            }
            catch
            {
                // ignore malformed PATH entries
            }
        }
        return null;
    }
}
