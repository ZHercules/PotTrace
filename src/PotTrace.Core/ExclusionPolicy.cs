namespace PotTrace.Core;
public static class ExclusionPolicy
{
    public static bool IsExcluded(string sourcePath,IReadOnlyCollection<string> excludedDirectories)
    {
        if(!Path.IsPathFullyQualified(sourcePath))return false;
        var full=Path.GetFullPath(sourcePath);
        return excludedDirectories.Where(Path.IsPathFullyQualified).Any(dir=>
        {
            var root=Path.GetFullPath(dir);
            var prefix=Path.EndsInDirectorySeparator(root)?root:root+Path.DirectorySeparatorChar;
            return full.StartsWith(prefix,StringComparison.OrdinalIgnoreCase);
        });
    }
}
