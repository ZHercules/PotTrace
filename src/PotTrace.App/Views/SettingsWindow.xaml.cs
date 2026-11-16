using System.Windows;
using PotTrace.Core;
namespace PotTrace.App.Views;
public partial class SettingsWindow:Window
{
    private readonly App host;
    public SettingsWindow(App host){this.host=host;InitializeComponent();Mode.SelectedIndex=(int)host.Settings.Mode;Theme.SelectedIndex=host.Settings.Theme=="Light"?1:0;Exclusions.Text=string.Join(Environment.NewLine,host.Settings.ExcludedDirectories??[]);}
    private async void SaveClick(object sender,RoutedEventArgs e)
    {
        try{await host.SaveSettingsAsync(host.Settings with {Mode=(CaptureMode)Mode.SelectedIndex,Theme=Theme.SelectedIndex==1?"Light":"Dark",ExcludedDirectories=Exclusions.Text.Split(['\r','\n'],StringSplitOptions.RemoveEmptyEntries|StringSplitOptions.TrimEntries)});host.ApplyTheme();DialogResult=true;}
        catch(Exception ex){System.Windows.MessageBox.Show(this,ex.Message,"设置未保存");}
    }
}
