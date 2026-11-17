namespace PotTrace.App.Hosting;
public sealed class HostLifetimeCoordinator
{
    private readonly TimeProvider clock;
    private readonly HashSet<Guid> players=[];
    private DateTimeOffset idleSince;
    private bool visible,exitRequested;
    private readonly object gate=new();
    public HostLifetimeCoordinator(TimeProvider? clock=null)
    { this.clock=clock??TimeProvider.System;idleSince=this.clock.GetUtcNow(); }
    public void OnPlayerConnected(Guid instanceId) { lock(gate)players.Add(instanceId); }
    public void OnPlayerDisconnected(Guid instanceId)
    { lock(gate)if(players.Remove(instanceId)&&players.Count==0)idleSince=clock.GetUtcNow(); }
    public void SetWindowVisible(bool visible) { lock(gate)this.visible=visible; }
    public void RequestUserExit() { lock(gate)exitRequested=true; }
    public bool ShouldExit { get {lock(gate)return exitRequested||(!visible&&players.Count==0&&clock.GetUtcNow()-idleSince>=TimeSpan.FromSeconds(3));} }
    public int PlayerCount { get {lock(gate)return players.Count;} }
}
