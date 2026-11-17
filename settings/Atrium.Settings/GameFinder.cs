using System.Diagnostics;
using System.IO;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace Atrium.Settings;

// Installed games, for Stage Manager to leave alone (the user picks which): Steam's and Epic's
// libraries by their folders (an entry ending in a backslash: every program in it is the game's,
// also after an update renames one), and the games Windows' Game Bar has seen, by program.
internal static class GameFinder
{
    internal sealed record Game(string Title, string Entry, string Icon);

    public static List<Game> Find()
    {
        var games = new List<Game>();
        try { Steam(games); } catch { }
        try { Epic(games); } catch { }
        try { GameBar(games); } catch { }
        // A program inside a library's folder is that game already.
        var folders = games.Select(g => g.Entry).Where(IsFolder).ToList();
        return games.Where(g => IsFolder(g.Entry) || !folders.Any(f => g.Entry.StartsWith(f, StringComparison.OrdinalIgnoreCase)))
                    .DistinctBy(g => g.Entry, StringComparer.OrdinalIgnoreCase)
                    .OrderBy(g => g.Title, StringComparer.CurrentCultureIgnoreCase)
                    .ToList();
    }

    internal static bool IsFolder(string entry) => entry.EndsWith('\\');

    // As Stage Manager matches it: lower case, a prefix of its programs' paths.
    private static string FolderEntry(string folder) => Path.GetFullPath(folder).TrimEnd('\\').ToLowerInvariant() + "\\";

    private static void Steam(List<Game> games)
    {
        using var key = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam");
        if (key?.GetValue("SteamPath") is not string steam || steam.Length == 0) return;
        var libraries = new List<string> { steam };
        string vdf = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
        if (File.Exists(vdf))
            foreach (Match m in Regex.Matches(File.ReadAllText(vdf), "\"path\"\\s+\"([^\"]+)\""))
                libraries.Add(m.Groups[1].Value.Replace(@"\\", @"\"));
        foreach (string library in libraries.Select(l => Path.GetFullPath(l)).Distinct(StringComparer.OrdinalIgnoreCase))
        {
            string apps = Path.Combine(library, "steamapps");
            if (!Directory.Exists(apps)) continue;
            foreach (string manifest in Directory.EnumerateFiles(apps, "appmanifest_*.acf"))
            {
                string text = File.ReadAllText(manifest);
                string name = Field(text, "name"), dir = Field(text, "installdir");
                // Steam's own runtimes are not games.
                if (name.Length == 0 || dir.Length == 0 || name.StartsWith("Steamworks", StringComparison.OrdinalIgnoreCase) ||
                    name.Contains("Redistributable", StringComparison.OrdinalIgnoreCase))
                    continue;
                string folder = Path.Combine(apps, "common", dir);
                if (Directory.Exists(folder))
                    games.Add(new Game(name, FolderEntry(folder), MainProgram(folder)));
            }
        }
    }

    private static string Field(string vdf, string field)
    {
        var m = Regex.Match(vdf, "\"" + field + "\"\\s+\"([^\"]*)\"", RegexOptions.IgnoreCase);
        return m.Success ? m.Groups[1].Value : "";
    }

    private static void Epic(List<Game> games)
    {
        string manifests = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), @"Epic\EpicGamesLauncher\Data\Manifests");
        if (!Directory.Exists(manifests)) return;
        foreach (string item in Directory.EnumerateFiles(manifests, "*.item"))
        {
            try
            {
                if (JsonNode.Parse(File.ReadAllText(item)) is not JsonObject o) continue;
                if (o["AppCategories"] is JsonArray categories && !categories.Any(c => c?.GetValue<string>() == "games")) continue;
                string name = o["DisplayName"]?.GetValue<string>() ?? "", folder = o["InstallLocation"]?.GetValue<string>() ?? "";
                string exe = o["LaunchExecutable"]?.GetValue<string>() ?? "";
                if (name.Length == 0 || !Directory.Exists(folder)) continue;
                string launch = exe.Length > 0 ? Path.Combine(folder, exe) : "";
                games.Add(new Game(name, FolderEntry(folder), File.Exists(launch) ? launch : MainProgram(folder)));
            }
            catch { }
        }
    }

    private static void GameBar(List<Game> games)
    {
        using var children = Registry.CurrentUser.OpenSubKey(@"System\GameConfigStore\Children");
        if (children == null) return;
        string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
        foreach (string id in children.GetSubKeyNames())
        {
            using var child = children.OpenSubKey(id);
            // A packaged game is matched by its app id, not its path: left out.
            if (child?.GetValue("MatchedExeFullPath") is not string exe || !File.Exists(exe) ||
                exe.StartsWith(windows, StringComparison.OrdinalIgnoreCase) || exe.Contains(@"\WindowsApps\", StringComparison.OrdinalIgnoreCase))
                continue;
            games.Add(new Game(Title(exe), Path.GetFullPath(exe).ToLowerInvariant(), exe));
        }
    }

    private static string Title(string exe)
    {
        try
        {
            var v = FileVersionInfo.GetVersionInfo(exe);
            foreach (string? t in new[] { v.ProductName, v.FileDescription })
                if (!string.IsNullOrWhiteSpace(t)) return t.Trim();
        }
        catch { }
        return Path.GetFileNameWithoutExtension(exe);
    }

    private static readonly Regex NotTheGame = new("crash|unins|setup|redist|vc_?redist|dxsetup|report|updat|helper|install|benchmark|prereq", RegexOptions.IgnoreCase);

    // The game's own program in its folder (for its icon): the largest one, not a tool beside it.
    internal static string MainProgram(string folder)
    {
        try
        {
            var options = new EnumerationOptions { RecurseSubdirectories = true, MaxRecursionDepth = 3, IgnoreInaccessible = true };
            return Directory.EnumerateFiles(folder, "*.exe", options)
                .Where(f => !NotTheGame.IsMatch(Path.GetFileName(f)))
                .Select(f => new FileInfo(f))
                .OrderByDescending(f => f.Length)
                .FirstOrDefault()?.FullName ?? "";
        }
        catch { return ""; }
    }
}
