import ctypes as c
from ctypes import wintypes as w
import json, sys
sys.stdout.reconfigure(encoding='utf-8')
u = c.WinDLL('user32', use_last_error=True)
CB = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.restype = c.c_ssize_t
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetDlgCtrlID.argtypes = [w.HWND]

def info(h):
    title, cls, pid = c.create_unicode_buffer(2048), c.create_unicode_buffer(256), w.DWORD()
    u.GetWindowTextW(h, title, len(title)); u.GetClassNameW(h, cls, len(cls)); u.GetWindowThreadProcessId(h, c.byref(pid))
    return dict(hwnd=h, pid=pid.value, title=title.value, cls=cls.value, id=u.GetDlgCtrlID(h))

def windows(pid, children=False):
    rows=[]
    @CB
    def each(h,l):
        row=info(h)
        if row['pid']==pid:
            rows.append(row)
            if children:
                @CB
                def child(k,l): rows.append(info(k)); return True
                u.EnumChildWindows(h,child,0)
        return True
    u.EnumWindows(each,0)
    return rows

if __name__=='__main__':
    if sys.argv[1]=='list': print(json.dumps(windows(int(sys.argv[2]),True),ensure_ascii=False,indent=2))
    elif sys.argv[1]=='send': print(u.SendMessageW(int(sys.argv[2]),int(sys.argv[3],0),int(sys.argv[4],0),int(sys.argv[5],0)))
    elif sys.argv[1]=='close':
        for row in windows(int(sys.argv[2])):
            if row['cls']=='PotPlayer64': u.PostMessageW(row['hwnd'],16,0,0)
