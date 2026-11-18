using System.Windows;
using PotTrace.Core;
namespace PotTrace.App.Views;
public partial class SettingsWindow:Window
{
    private readonly App host;
    public SettingsWindow(App host){this.host=host;InitializeComponent();Mode.SelectedIndex=(int)host.Settings.Mode;Theme.SelectedIndex=host.Settings.Theme=="Light"?1:0;Exclusions.Text=string.Join(Environment.NewLine,host.Settings.ExcludedDirectories??[]);Shortcut.Text=Services.HotkeyManager.Format(host.Settings.HotKeyModifiers,host.Settings.HotKey);ShortcutStatus.Text=host.HotkeyRegistered?"快捷键已注册":"快捷键被占用时仍可从托盘打开统计";}
    private async void SaveClick(object sender,RoutedEventArgs e)
    {
        try{var key=Services.HotkeyManager.Parse(Shortcut.Text);await host.SaveSettingsAsync(host.Settings with {Mode=(CaptureMode)Mode.SelectedIndex,Theme=Theme.SelectedIndex==1?"Light":"Dark",HotKeyModifiers=key.Modifiers,HotKey=key.Key,ExcludedDirectories=Exclusions.Text.Split(['\r','\n'],StringSplitOptions.RemoveEmptyEntries|StringSplitOptions.TrimEntries)});host.ApplyTheme();DialogResult=true;}
        catch(Exception ex){System.Windows.MessageBox.Show(this,ex.Message,"设置未保存");}
    }
}
