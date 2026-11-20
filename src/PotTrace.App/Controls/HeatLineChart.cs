using System.Globalization;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using PotTrace.App.ViewModels;
using PotTrace.Core;
using Brush=System.Windows.Media.Brush;
using Brushes=System.Windows.Media.Brushes;
using Pen=System.Windows.Media.Pen;
using Point=System.Windows.Point;

namespace PotTrace.App.Controls;

public sealed class HeatLineChart:FrameworkElement
{
    private HeatSeries series=new([]);
    private StreamGeometry? line;
    private long? selected;
    private Rect Plot=>new(12,16,Math.Max(1,ActualWidth-24),Math.Max(1,ActualHeight-42));

    public HeatLineChart()
    {
        Focusable=true;
        SizeChanged+=(_,_)=>{line=null;InvalidateVisual();};
        MouseMove+=(_,args)=>
        {
            var plot=Plot;
            var position=args.GetPosition(this);
            SetSelection(plot.Contains(position)?(long)Math.Round((position.X-plot.Left)/plot.Width*series.EndSecond):null);
        };
        MouseLeave+=(_,_)=>SetSelection(null);
        MouseLeftButtonDown+=(_,_)=>Focus();
        LostKeyboardFocus+=(_,_)=>SetSelection(null);
        KeyDown+=(_,args)=>
        {
            if(args.Key is not (Key.Left or Key.Right)||series.Points.Count==0)return;
            SetSelection(Math.Clamp((selected??0)+(args.Key==Key.Left?-1:1),0,series.EndSecond));
            args.Handled=true;
        };
    }

    public void SetHeat(IReadOnlyList<HeatInterval> intervals)
    {
        series=new(intervals);
        line=null;
        if(selected>series.EndSecond||series.Points.Count==0)selected=null;
        InvalidateVisual();
    }

    private void SetSelection(long? second)
    {
        if(selected==second)return;
        selected=second;
        InvalidateVisual();
    }

    private Point Position(Rect plot,long second,int count)=>new(
        plot.Left+plot.Width*second/Math.Max(1,series.EndSecond),
        plot.Bottom-plot.Height*count/Math.Max(1,series.Peak));

    private StreamGeometry BuildLine(Rect plot)
    {
        var geometry=new StreamGeometry();
        using(var context=geometry.Open())
        {
            var first=series.Points[0];
            var previous=Position(plot,first.Second,first.Count);
            context.BeginFigure(previous,false,false);
            for(int index=1;index<series.Points.Count;index++)
            {
                var sample=series.Points[index];
                var current=Position(plot,sample.Second,sample.Count);
                if(current.Y==previous.Y)
                    context.LineTo(current,true,false);
                else
                {
                    double middleX=(previous.X+current.X)/2;
                    context.BezierTo(new Point(middleX,previous.Y),new Point(middleX,current.Y),current,true,true);
                }
                previous=current;
            }
        }
        geometry.Freeze();
        return geometry;
    }

    protected override void OnRender(DrawingContext dc)
    {
        var panel=Theme("PanelBrush");
        var muted=Theme("MutedBrush");
        var accent=Theme("AccentBrush");
        var plot=Plot;
        dc.DrawRectangle(panel,null,new Rect(RenderSize));
        if(series.Points.Count==0)
        {
            dc.DrawText(Label("暂无有效片段",muted),new Point(plot.Left,plot.Top));
            return;
        }
        dc.DrawLine(new Pen(Theme("BorderBrush"),1),plot.BottomLeft,plot.BottomRight);
        dc.DrawGeometry(null,new Pen(accent,2),line??=BuildLine(plot));
        var endLabel=Label(Clock(series.EndSecond),muted);
        dc.DrawText(Label(Clock(0),muted),new Point(plot.Left,plot.Bottom+4));
        dc.DrawText(endLabel,new Point(plot.Right-endLabel.Width,plot.Bottom+4));
        if(selected is not {} second)return;
        int count=series.CountAt(second);
        var point=Position(plot,second,count);
        var marker=new Pen(accent,1);
        dc.DrawLine(marker,new Point(point.X,plot.Top-4),new Point(point.X,plot.Bottom+2));
        dc.DrawEllipse(accent,new Pen(panel,1.5),point,4,4);
        var value=Label($"{Clock(second)}\n{count} 次",Theme("TextBrush"));
        double width=value.Width+16,height=value.Height+8;
        double left=point.X+10+width<=ActualWidth?point.X+10:Math.Max(0,point.X-width-10);
        double top=Math.Clamp(point.Y-height/2,0,Math.Max(0,plot.Bottom-height));
        dc.DrawRoundedRectangle(panel,new Pen(Theme("BorderBrush"),1),new Rect(left,top,width,height),4,4);
        dc.DrawText(value,new Point(left+8,top+4));
    }

    private Brush Theme(string key)=>TryFindResource(key) as Brush??Brushes.Gray;
    private FormattedText Label(string value,Brush brush)=>new(value,CultureInfo.CurrentCulture,
        System.Windows.FlowDirection.LeftToRight,new Typeface("Microsoft YaHei UI"),12,brush,VisualTreeHelper.GetDpi(this).PixelsPerDip);
    private static string Clock(long second)=>$"{second/3600:00}:{second/60%60:00}:{second%60:00}";
}
