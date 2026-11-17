using System.Runtime.InteropServices;
using System.Windows.Interop;
namespace PotTrace.App.Services;
public sealed class HotkeyManager:IDisposable
{
    private readonly HwndSource source;
    private readonly Action show;
    public bool Registered {get;private set;}
    public HotkeyManager(Action show)
    {
        this.show=show;source=new HwndSource(new HwndSourceParameters("PotTrace Hotkey"){ParentWindow=new IntPtr(-3),WindowStyle=0,Width=0,Height=0});source.AddHook(Hook);
    }
    public bool Register(int modifiers,int key)
    {UnregisterHotKey(source.Handle,1);Registered=RegisterHotKey(source.Handle,1,(uint)modifiers|0x4000,(uint)key);return Registered;}
    private IntPtr Hook(IntPtr hwnd,int msg,IntPtr w,IntPtr l,ref bool handled)
    {if(msg==0x0312&&w==new IntPtr(1)){handled=true;show();}return IntPtr.Zero;}
    public void Dispose(){UnregisterHotKey(source.Handle,1);source.Dispose();}
    public static (int Modifiers,int Key) Parse(string text)
    {
        var parts=text.Split('+',StringSplitOptions.TrimEntries|StringSplitOptions.RemoveEmptyEntries);int modifiers=0;
        foreach(var part in parts.SkipLast(1))modifiers|=part.ToUpperInvariant() switch {"CTRL"=>2,"ALT"=>1,"SHIFT"=>4,"WIN"=>8,_=>throw new ArgumentException("快捷键修饰键支持 Ctrl、Alt、Shift、Win。")};
        if(parts.Length<2||modifiers==0||!Enum.TryParse<System.Windows.Forms.Keys>(parts[^1],true,out var key)||!((int)key is >=0x41 and <=0x5A or >=0x70 and <=0x7B))throw new ArgumentException("例如 Ctrl+Alt+H；主键支持 A–Z 或 F1–F12。须包含修饰键。");
        return (modifiers,(int)key);
    }
    public static string Format(int modifiers,int key)=>string.Join('+',new[]{(modifiers&2)!=0?"Ctrl":null,(modifiers&1)!=0?"Alt":null,(modifiers&4)!=0?"Shift":null,(modifiers&8)!=0?"Win":null,((System.Windows.Forms.Keys)key).ToString()}.Where(x=>x is not null));
    [DllImport("user32.dll",SetLastError=true)] private static extern bool RegisterHotKey(IntPtr hwnd,int id,uint modifiers,uint key);
    [DllImport("user32.dll")] private static extern bool UnregisterHotKey(IntPtr hwnd,int id);
}
