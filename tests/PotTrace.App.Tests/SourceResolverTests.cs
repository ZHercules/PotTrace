using PotTrace.App.Services;
using PotTrace.Contracts;
namespace PotTrace.App.Tests;
public class SourceResolverTests
{
    [Fact] public void ReplacedFileGetsNewTimelineVersionButSameMovieName()
    {
        string path=Path.Combine(Path.GetTempPath(),Guid.NewGuid()+".avi");
        try
        {
            File.WriteAllText(path,"one");var resolver=new SourceResolver();
            var first=resolver.Resolve(new(){SourcePath=path,InstanceId=Guid.NewGuid(),MediaEpoch=Guid.NewGuid(),Kind=PlaybackEventKind.MediaOpened});
            File.WriteAllText(path,"replacement version");
            var second=resolver.Resolve(first with {MediaEpoch=Guid.NewGuid()});
            Assert.NotNull(first.SourceVersion);Assert.NotEqual(first.SourceVersion,second.SourceVersion);
            Assert.Equal(first.SourcePath,second.SourcePath);
        }
        finally{File.Delete(path);}
    }
}
