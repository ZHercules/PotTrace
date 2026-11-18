string GetTitle() { return "PotTrace early identity probe"; }
string GetVersion() { return "1"; }
string GetDesc() { return "Read-only parser eligibility trace; never handles a file"; }
bool PlayitemCheck(const string &in path)
{
    uintptr library = HostLoadLibrary(HostGetExecuteFolder() + "\\Extension\\Playback\\Statistics\\PotTraceProbe.dll");
    if (library != 0) {
        uintptr log = HostGetProcAddress(library,"Probe_CopyUtf8");
        string line = "" + HostGetTickCount() + "\tEarlyCheck\t" + path + "\n";
        HostCallProcInt(log,"PI",HostString2UIntPtr(line),line.length());
    }
    return false;
}
string PlayitemParse(const string &in path, dictionary &MetaData, array<dictionary> &QualityList) { return path; }
