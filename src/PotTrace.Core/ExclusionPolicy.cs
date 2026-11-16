namespace PotTrace.Core;
public static class ExclusionPolicy
{
    public static bool IsExcluded(string sourcePath,IReadOnlyCollection<string> excludedDirectories)
    {
        if(!Path.IsPathFullyQualified(sourcePath))return false;
        var full=Path.GetFullPath(sourcePath);
        return excludedDirectories.Where(Path.IsPathFullyQualified).Any(dir=>full.StartsWith(Path.TrimEndingDirectorySeparator(Path.GetFullPath(dir))+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase));
    }
}
