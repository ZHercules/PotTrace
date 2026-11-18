using System.Globalization;
using System.Windows;
using System.Windows.Media;
using PotTrace.Core;
using Brush=System.Windows.Media.Brush;
using Brushes=System.Windows.Media.Brushes;
using Point=System.Windows.Point;
using Rect=System.Windows.Rect;
using Color=System.Windows.Media.Color;
namespace PotTrace.App.Controls;
public sealed class TimelineControl:FrameworkElement
{
    public bool IsHeat {get;set;}
    private IReadOnlyList<SegmentRecord> segments=[];
    private IReadOnlyList<HeatInterval> heat=[];
    private readonly List<(Rect Bounds,string Text)> tips=[];
    public void SetSegments(IReadOnlyList<SegmentRecord> value){segments=value;InvalidateVisual();}
    public void SetHeat(IReadOnlyList<HeatInterval> value){heat=value;InvalidateVisual();}
    public TimelineControl(){MouseMove+=(_,e)=>{var p=e.GetPosition(this);ToolTip=tips.FirstOrDefault(x=>x.Bounds.Contains(p)).Text;};SizeChanged+=(_,_)=>InvalidateVisual();}
    protected override void OnRender(DrawingContext dc)
    {
        base.OnRender(dc);tips.Clear();double w=Math.Max(10,ActualWidth-24),h=ActualHeight;
        Brush foreground=TryFindResource("MutedBrush") as Brush??Brushes.Gray;
        dc.DrawRectangle(TryFindResource("PanelBrush") as Brush??Brushes.DarkGray,null,new Rect(0,0,ActualWidth,h));
        if(IsHeat)
        {
            if(heat.Count==0){Text(dc,"暂无有效片段",12,25,foreground);return;}
            double end=Math.Max(1,heat.Max(x=>x.EndMs));int max=heat.Max(x=>x.Count);
            foreach(var item in heat)
            {
                var rect=new Rect(12+w*item.StartMs/end,15,Math.Max(1,w*(item.EndMs-item.StartMs)/end),25);
                var color=Color.FromRgb((byte)(65+120d*item.Count/max),(byte)(115+60d*item.Count/max),185);
                dc.DrawRectangle(new SolidColorBrush(color),null,rect);tips.Add((rect,$"影片 {Media(item.StartMs)} — {Media(item.EndMs)}，覆盖 {item.Count} 次"));
            }
            Text(dc,"00:00",12,49,foreground);Text(dc,Media((long)end),Math.Max(12,w-50),49,foreground);
        }
        else
        {
            if(segments.Count==0){Text(dc,"当前会话没有超过 5 秒的播放片段",12,25,foreground);return;}
            var origin=segments.Min(x=>x.StartedUtc);double total=Math.Max(1,(segments.Max(x=>x.EndedUtc)-origin).TotalMilliseconds);
            foreach(var item in segments)
            {
                double start=Math.Clamp((item.StartedUtc-origin).TotalMilliseconds,0,total),duration=Math.Max(1,(item.EndedUtc-item.StartedUtc).TotalMilliseconds);
                var rect=new Rect(12+w*start/total,16,Math.Max(2,Math.Min(w,w*duration/total)),26);
                dc.DrawRectangle(TryFindResource("AccentBrush") as Brush??Brushes.SteelBlue,null,rect);
                tips.Add((rect,$"现实 {item.StartedUtc.LocalDateTime:HH:mm:ss.fff} — {item.EndedUtc.LocalDateTime:HH:mm:ss.fff}\n影片 {Media(item.StartMs)} — {Media(item.EndMs)}\n实际播放 {item.PlayedMs/1000d:0.000} 秒"));
            }
            Text(dc,origin.LocalDateTime.ToString("HH:mm:ss"),12,52,foreground);Text(dc,origin.AddMilliseconds(total).LocalDateTime.ToString("HH:mm:ss"),Math.Max(12,w-60),52,foreground);
        }
    }
    private void Text(DrawingContext dc,string text,double x,double y,Brush brush)=>dc.DrawText(new FormattedText(text,CultureInfo.CurrentCulture,System.Windows.FlowDirection.LeftToRight,new Typeface("Microsoft YaHei UI"),11,brush,VisualTreeHelper.GetDpi(this).PixelsPerDip),new Point(x,y));
    public static string Media(long ms){var t=TimeSpan.FromMilliseconds(ms);return $"{(int)t.TotalHours:00}:{t.Minutes:00}:{t.Seconds:00}.{t.Milliseconds:000}";}
}
