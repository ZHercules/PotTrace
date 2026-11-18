uintptr probeDll = 0;
uintptr probeLog = 0;
string GetTitle() { return "PotTrace Probe"; }
string GetVersion() { return "1"; }
string GetDesc() { return "Local playback callback compatibility probe"; }
void Log(const string &in kind, const string &in path = "", int a = -1, int b = -1)
{
    if (probeDll == 0) probeDll = HostLoadLibrary(HostGetScriptFolder() + "\\PotTraceProbe.dll");
    if (probeDll == 0) return;
    if (probeLog == 0) probeLog = HostGetProcAddress(probeDll, "Probe_CopyUtf8");
    string text = "" + HostGetTickCount() + "\t" + kind + "\t" + a + "\t" + b + "\t" + path + "\n";
    HostCallProcInt(probeLog, "PI", HostString2UIntPtr(text), text.length());
}
void OnInitialize() { Log("Initialize", HostGetVersion()); }
void OnFinalize() { Log("Finalize"); }
void PlaybackOpen(const string &in path) { Log("Open", path); }
void PlaybackStart(const string &in path, int duration) { Log("Start", path, duration); }
void PlaybackTime(const string &in path, int position, int duration) { Log("Time", path, position, duration); }
void PlaybackPause(const string &in path) { Log("Pause", path); }
void PlaybackResume(const string &in path) { Log("Resume", path); }
void PlaybackComplete(const string &in path) { Log("Complete", path); }
void PlaybackClose(const string &in path) { Log("Close", path); }
