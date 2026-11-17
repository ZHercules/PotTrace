uintptr bridge = 0;
uintptr enqueue = 0;
string GetTitle() { return "PotTrace"; }
string GetVersion() { return "0.1.0"; }
string GetDesc() { return "Local playback history and statistics"; }
void OnInitialize()
{
    bridge = HostLoadLibrary(HostGetScriptFolder() + "\\PotTrace.Bridge.dll");
    if (bridge == 0) return;
    uintptr init = HostGetProcAddress(bridge, "PotTrace_Initialize");
    enqueue = HostGetProcAddress(bridge, "PotTrace_Enqueue");
    string folder = HostGetScriptFolder();
    if (init != 0) HostCallProcInt(init, "PI", HostString2UIntPtr(folder), folder.length());
    Send("Ready", "");
}
void Send(const string &in kind, const string &in path, int duration = -1, int position = -1)
{
    if (enqueue == 0) return;
    string text = kind + "\t" + duration + "\t" + position + "\t" + path;
    HostCallProcInt(enqueue, "PI", HostString2UIntPtr(text), text.length());
}
void OnFinalize() { if (bridge != 0) { uintptr stop = HostGetProcAddress(bridge,"PotTrace_Shutdown"); if (stop != 0) HostCallProcInt(stop, ""); } }
void PlaybackOpen(const string &in path) { Send("Open",path); }
void PlaybackStart(const string &in path,int duration) { Send("Start",path,duration); }
void PlaybackTime(const string &in path,int duration,int position) { Send("Time",path,duration,position); }
void PlaybackPause(const string &in path) { Send("Pause",path); }
void PlaybackResume(const string &in path) { Send("Resume",path); }
void PlaybackComplete(const string &in path) { Send("Complete",path); }
void PlaybackClose(const string &in path) { Send("Close",path); }
