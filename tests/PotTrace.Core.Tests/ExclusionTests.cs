using PotTrace.Core;
namespace PotTrace.Core.Tests;
public class ExclusionTests
{
    [Fact] public void ExclusionUsesDirectoryBoundaryAndCanonicalPath()
    {
        Assert.True(ExclusionPolicy.IsExcluded(@"D:\Private\movie.avi",[@"d:\private"]));
        Assert.False(ExclusionPolicy.IsExcluded(@"D:\Private2\movie.avi",[@"D:\Private"]));
        Assert.True(ExclusionPolicy.IsExcluded(@"D:\other\..\Private\movie.avi",[@"D:\Private\"]));
    }
    [Theory]
    [InlineData(@"D:\",@"D:\Movies\private.mkv",true)]
    [InlineData(@"D:\",@"E:\Movies\private.mkv",false)]
    [InlineData(@"\\server\share\",@"\\server\share\Movies\private.mkv",true)]
    [InlineData(@"D:\Private",@"D:\Private2\movie.mkv",false)]
    public void RootsAndDirectoryBoundariesAreRespected(string excluded,string file,bool expected)
        =>Assert.Equal(expected,ExclusionPolicy.IsExcluded(file,[excluded]));
}
