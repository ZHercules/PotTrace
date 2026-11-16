using PotTrace.Core;
namespace PotTrace.Core.Tests;
public class ExclusionTests
{
    [Fact] public void ExclusionMatchesOnlyDescendantsAtDirectoryBoundary()
    {
        Assert.True(ExclusionPolicy.IsExcluded(@"D:\Private\movie.avi",[@"d:\private"]));
        Assert.False(ExclusionPolicy.IsExcluded(@"D:\Private2\movie.avi",[@"D:\Private"]));
        Assert.True(ExclusionPolicy.IsExcluded(@"D:\other\..\Private\movie.avi",[@"D:\Private\"]));
    }
}
