using System.IO;
using PotTrace.App.Settings;

namespace PotTrace.App.Tests;
public sealed class CapturePolicyTests
{
    [Fact]
    public async Task MissingPolicyDoesNotEnableCapture()
    {
        var path=Path.Combine(Path.GetTempPath(),Guid.NewGuid()+".json");
        var policy=await new CapturePolicyStore(path).LoadAsync(default);
        Assert.False(policy.Enabled);
    }
    [Fact]
    public async Task AtomicPolicyPreservesPauseAndIncrementsRevision()
    {
        var directory=Path.Combine(Path.GetTempPath(),"PotTrace-policy-"+Guid.NewGuid());
        try
        {
            var store=new CapturePolicyStore(Path.Combine(directory,"capture-policy.json"));
            await store.SaveAsync(new(true,false,1),default);
            await store.SaveAsync(new(true,true,2),default);
            Assert.Equal(new CapturePolicy(true,true,2),await store.LoadAsync(default));
            await Assert.ThrowsAsync<InvalidOperationException>(()=>store.SaveAsync(new(true,false,1),default));
            Assert.Empty(Directory.GetFiles(directory,"*.tmp"));
        }
        finally {if(Directory.Exists(directory))Directory.Delete(directory,true);}
    }
    [Fact]
    public async Task MalformedPolicyFailsClosed()
    {
        var path=Path.Combine(Path.GetTempPath(),Guid.NewGuid()+".json");
        try {await File.WriteAllTextAsync(path,"{\"Enabled\":true}");Assert.False((await new CapturePolicyStore(path).LoadAsync(default)).Enabled);}
        finally {File.Delete(path);}
    }
    [Fact]
    public async Task ConcurrentUiUpdatesAndReadersDoNotLosePolicy()
    {
        var directory=Path.Combine(Path.GetTempPath(),"PotTrace-policy-"+Guid.NewGuid());
        try
        {
            var store=new CapturePolicyStore(Path.Combine(directory,"capture-policy.json"));
            await Task.WhenAll(Enumerable.Range(0,30).Select(i=>store.UpdateAsync(true,i%2==0,default)));
            Assert.Equal(30,(await store.LoadAsync(default)).Revision);
            var reads=Task.Run(async ()=>{for(int i=0;i<100;++i)Assert.True((await store.LoadAsync(default)).Enabled);});
            for(int i=0;i<30;++i)await store.UpdateAsync(true,false,default);
            await reads;Assert.Equal(60,(await store.LoadAsync(default)).Revision);
        }
        finally {if(Directory.Exists(directory))Directory.Delete(directory,true);}
    }
    [Fact]
    public async Task ExplicitCollectorBackendCanBeEnabledFromMissingPolicy()
    {
        var directory=Path.Combine(Path.GetTempPath(),"PotTrace-policy-"+Guid.NewGuid());
        var path=Path.Combine(directory,"capture-policy.json");
        try
        {
            Assert.Null(CapturePolicyStore.ForBackend(path,false));
            var store=CapturePolicyStore.ForBackend(path,true);Assert.NotNull(store);
            Assert.False((await store.LoadAsync(default)).Enabled);
            await store.UpdateAsync(true,false,default);
            Assert.Equal(new CapturePolicy(true,false,1),await store.LoadAsync(default));
        }
        finally {if(Directory.Exists(directory))Directory.Delete(directory,true);}
    }
}
