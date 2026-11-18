using PotTrace.App.Services;
namespace PotTrace.App.Tests;
public class HotkeyTests
{
    [Fact] public void ShortcutRoundTripsWithoutClaimingUnmodifiedKeys()
    {
        var value=HotkeyManager.Parse("Ctrl+Alt+H");Assert.Equal(3,value.Modifiers);Assert.Equal(0x48,value.Key);
        Assert.Equal("Ctrl+Alt+H",HotkeyManager.Format(value.Modifiers,value.Key));
        Assert.Throws<ArgumentException>(()=>HotkeyManager.Parse("H"));
    }
}
