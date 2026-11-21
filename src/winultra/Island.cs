// WinUltra: the island. A thin glowing line at the top centre of the main screen that grows into
// a pill for music, timers, charging, volume and notifications. Hover it (or press Ctrl+Alt+Space)
// to open Media, Controls, Stats, Clipboard, Shelf and Timer. Started by WinUltra.cs.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Management;
using System.Media;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Microsoft.Win32;
using MediaManager = Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager;
using MediaSession = Windows.Media.Control.GlobalSystemMediaTransportControlsSession;
using PlaybackStatus = Windows.Media.Control.GlobalSystemMediaTransportControlsSessionPlaybackStatus;
using Radio = Windows.Devices.Radios.Radio;
using RadioKind = Windows.Devices.Radios.RadioKind;
using RadioState = Windows.Devices.Radios.RadioState;

// ---------------------------------------------------------------- Win32

static class Native
{
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern short GetKeyState(int vk);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool EnumDisplaySettings(string device, int mode, ref DEVMODE dm);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct DEVMODE
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
        public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
        public short dmLogPixels;
        public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
    }
#if !FRIENDS
    [DllImport("user32.dll")] public static extern bool RegisterRawInputDevices(RAWINPUTDEVICE[] devices, uint count, uint size);
    [DllImport("user32.dll")] public static extern uint GetRawInputData(IntPtr raw, uint command, IntPtr data, ref uint size, uint headerSize);
    [DllImport("user32.dll")] static extern uint SendInput(uint count, INPUT[] inputs, int size);
    [StructLayout(LayoutKind.Sequential)] public struct RAWINPUTDEVICE { public ushort UsagePage, Usage; public uint Flags; public IntPtr Target; }
    [StructLayout(LayoutKind.Explicit, Size = 40)]
    struct INPUT { [FieldOffset(0)] public int Type; [FieldOffset(8)] public ushort Vk; [FieldOffset(10)] public ushort Scan; [FieldOffset(12)] public uint Flags; [FieldOffset(16)] public uint Time; [FieldOffset(24)] public IntPtr Extra; }

    // Presses Ctrl+V in whatever window has the keyboard.
    public static void SendPaste()
    {
        var keys = new INPUT[4];
        keys[0] = new INPUT { Type = 1, Vk = 0x11 };
        keys[1] = new INPUT { Type = 1, Vk = 0x56 };
        keys[2] = new INPUT { Type = 1, Vk = 0x56, Flags = 2 };
        keys[3] = new INPUT { Type = 1, Vk = 0x11, Flags = 2 };
        SendInput(4, keys, 40);
    }
#endif
    [DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref LASTINPUTINFO info);
    [StructLayout(LayoutKind.Sequential)] public struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool GetMonitorInfo(IntPtr m, ref MONITORINFO mi);
    [DllImport("shell32.dll")] public static extern int SHQueryUserNotificationState(out int state);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int index);
    [DllImport("user32.dll")] public static extern int SetWindowLong(IntPtr h, int index, int value);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool RegisterHotKey(IntPtr h, int id, uint mods, uint vk);
    [DllImport("user32.dll")] public static extern bool UnregisterHotKey(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern bool AddClipboardFormatListener(IntPtr h);
    [DllImport("user32.dll")] public static extern bool RemoveClipboardFormatListener(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern uint RegisterClipboardFormat(string name);
    [DllImport("user32.dll")] public static extern bool IsClipboardFormatAvailable(uint format);
    [DllImport("user32.dll")] public static extern bool OpenClipboard(IntPtr h);
    [DllImport("user32.dll")] public static extern bool CloseClipboard();
    [DllImport("user32.dll")] public static extern IntPtr GetClipboardData(uint format);
    [DllImport("kernel32.dll")] public static extern IntPtr GlobalLock(IntPtr h);
    [DllImport("kernel32.dll")] public static extern bool GlobalUnlock(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern uint RegisterWindowMessage(string s);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
    [DllImport("kernel32.dll")] public static extern bool GetSystemPowerStatus(out SYSTEM_POWER_STATUS s);
    [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentProcess();
    [DllImport("psapi.dll")] public static extern bool EmptyWorkingSet(IntPtr process);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SHGetFileInfo(string path, uint attr, ref SHFILEINFO info, uint size, uint flags);
    [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr h);

    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)]
    public struct MONITORINFO { public int cbSize; public RECT rcMonitor; public RECT rcWork; public uint dwFlags; }
    [StructLayout(LayoutKind.Sequential)]
    public struct SYSTEM_POWER_STATUS { public byte ACLineStatus, BatteryFlag, BatteryLifePercent, SystemStatusFlag; public int BatteryLifeTime, BatteryFullLifeTime; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct SHFILEINFO
    {
        public IntPtr hIcon; public int iIcon; public uint dwAttributes;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string szDisplayName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 80)] public string szTypeName;
    }

    public static void TrimMemory()
    {
        GC.Collect();
        GC.WaitForPendingFinalizers();
        EmptyWorkingSet(GetCurrentProcess());
    }

    // 48 px shell icons ("extra large" image list), so files on the shelf stay sharp at 125%.
    [DllImport("shell32.dll", EntryPoint = "#727")] static extern int SHGetImageList(int list, ref Guid iid, out IntPtr imageList);
    [DllImport("comctl32.dll")] static extern IntPtr ImageList_GetIcon(IntPtr imageList, int index, int flags);

    public static IntPtr LargeIcon(string path)
    {
        var info = new SHFILEINFO();
        if (SHGetFileInfo(path, 0, ref info, (uint)Marshal.SizeOf(info), 0x4000) == IntPtr.Zero) return IntPtr.Zero;    // SHGFI_SYSICONINDEX
        var iid = new Guid("46EB5926-582E-4017-9FDF-E8998DAA0950");
        IntPtr list;
        if (SHGetImageList(2, ref iid, out list) != 0 || list == IntPtr.Zero) return IntPtr.Zero;    // SHIL_EXTRALARGE
        return ImageList_GetIcon(list, info.iIcon, 1);    // ILD_TRANSPARENT
    }
}

// ---------------------------------------------------------------- Frosted glass backdrop

// Windows' own blur falls back to a flat colour for an always-on-top window that never takes focus,
// so the island makes its own glass: just before it grows, it copies the strip of screen behind it,
// shrinks it, blurs and saturates it, and paints that under a dark tint. It stays in memory only.
static class Backdrop
{
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleBitmap(IntPtr dc, int w, int h);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc, IntPtr o);
    [DllImport("gdi32.dll")] static extern bool BitBlt(IntPtr dst, int x, int y, int w, int h, IntPtr src, int sx, int sy, int rop);
    [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr o);
    [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr dc);

    public static BitmapSource Capture(int x, int y, int w, int h)
    {
        IntPtr screen = GetDC(IntPtr.Zero), mem = CreateCompatibleDC(screen), bmp = CreateCompatibleBitmap(screen, w, h);
        IntPtr old = SelectObject(mem, bmp);
        BitmapSource src;
        try
        {
            BitBlt(mem, 0, 0, w, h, screen, x, y, 0x00CC0020);    // SRCCOPY
            SelectObject(mem, old);
            src = Imaging.CreateBitmapSourceFromHBitmap(bmp, IntPtr.Zero, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions());
        }
        finally { DeleteObject(bmp); DeleteDC(mem); ReleaseDC(IntPtr.Zero, screen); }

        int sw = Math.Max(8, w / 8), sh = Math.Max(8, h / 8);
        var small = new FormatConvertedBitmap(new TransformedBitmap(src, new ScaleTransform((double)sw / w, (double)sh / h)), PixelFormats.Bgra32, null, 0);
        sw = small.PixelWidth;
        sh = small.PixelHeight;
        var px = new byte[sw * sh * 4];
        small.CopyPixels(px, sw * 4, 0);
        for (int pass = 0; pass < 3; pass++) BoxBlur(px, sw, sh, 2);
        for (int i = 0; i < px.Length; i += 4)
        {
            double b = px[i], g = px[i + 1], r = px[i + 2], l = 0.299 * r + 0.587 * g + 0.114 * b;
            px[i] = Clamp(l + (b - l) * 1.4);
            px[i + 1] = Clamp(l + (g - l) * 1.4);
            px[i + 2] = Clamp(l + (r - l) * 1.4);
            px[i + 3] = 255;
        }
        var result = BitmapSource.Create(sw, sh, 96, 96, PixelFormats.Bgra32, null, px, sw * 4);
        result.Freeze();
        return result;
    }

    static byte Clamp(double v) { return (byte)(v < 0 ? 0 : v > 255 ? 255 : v); }

    static void BoxBlur(byte[] p, int w, int h, int r)
    {
        var t = new byte[p.Length];
        for (int pass = 0; pass < 2; pass++)
        {
            byte[] from = pass == 0 ? p : t, to = pass == 0 ? t : p;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    for (int c = 0; c < 3; c++)
                    {
                        int sum = 0;
                        for (int k = -r; k <= r; k++)
                        {
                            int xx = pass == 0 ? Math.Min(w - 1, Math.Max(0, x + k)) : x;
                            int yy = pass == 0 ? y : Math.Min(h - 1, Math.Max(0, y + k));
                            sum += from[(yy * w + xx) * 4 + c];
                        }
                        to[(y * w + x) * 4 + c] = (byte)(sum / (2 * r + 1));
                    }
        }
    }
}

// ---------------------------------------------------------------- Camera and microphone in use

// Windows notes in the registry when an app starts and stops using the camera or microphone
// (LastUsedTimeStop is 0 while it is in use). The island waits on registry change events, so it
// costs nothing until something changes.
class PrivacyWatch
{
    [DllImport("advapi32.dll")] static extern int RegNotifyChangeKeyValue(IntPtr key, bool subtree, uint filter, IntPtr ev, bool async);
    const string Root = @"Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\";
    readonly List<RegistryKey> keys = new List<RegistryKey>();
    readonly List<AutoResetEvent> events = new List<AutoResetEvent>();
    public event Action Changed;

    public void Start()
    {
        foreach (var cap in new[] { "webcam", "microphone" })
        {
            var key = Registry.CurrentUser.OpenSubKey(Root + cap);
            if (key == null) continue;
            var ev = new AutoResetEvent(false);
            keys.Add(key);
            events.Add(ev);
            Arm(key, ev);
            ThreadPool.RegisterWaitForSingleObject(ev, delegate
            {
                Arm(key, ev);
                var h = Changed;
                if (h != null) h();
            }, null, -1, false);
        }
        StartCameraMonitor();
    }

    // Names, values and subkeys anywhere below; REG_NOTIFY_THREAD_AGNOSTIC so any thread can re-arm it.
    static void Arm(RegistryKey key, AutoResetEvent ev)
    {
        RegNotifyChangeKeyValue(key.Handle.DangerousGetHandle(), true, 0x1 | 0x4 | 0x10000000, ev.SafeWaitHandle.DangerousGetHandle(), true);
    }

    // ---- live detection (Windows 11 25H2 stopped updating the registry records reliably)
    [DllImport("mfplat.dll")] static extern int MFStartup(int version, int flags);
    [DllImport("mfsensorgroup.dll")] static extern int MFCreateSensorActivityMonitor(IMFSensorActivitiesReportCallback callback, out IMFSensorActivityMonitor monitor);
    static readonly object liveLock = new object();
    static List<uint> cameraPids = new List<uint>();
    static readonly uint ownPid = (uint)System.Diagnostics.Process.GetCurrentProcess().Id;
    SensorReportCallback sensorCallback;
    IMFSensorActivityMonitor sensorMonitor;

    public bool CameraMonitorRunning { get { return sensorMonitor != null; } }

    void StartCameraMonitor()
    {
        try
        {
            MFStartup(0x00020070, 1);    // MF_VERSION, MFSTARTUP_LITE
            sensorCallback = new SensorReportCallback(OnSensorReport);
            if (MFCreateSensorActivityMonitor(sensorCallback, out sensorMonitor) == 0) sensorMonitor.Start();
        }
        catch (Exception ex) { Store.Log("camera monitor: " + ex.Message); }
    }

    // Each report lists the cameras and which processes are streaming from them right now.
    void OnSensorReport(IMFSensorActivitiesReport report)
    {
        var pids = new List<uint>();
        uint n;
        if (report.GetCount(out n) != 0) return;
        for (uint i = 0; i < n; i++)
        {
            IMFSensorActivityReport device;
            if (report.GetActivityReport(i, out device) != 0 || device == null) continue;
            uint count;
            if (device.GetProcessCount(out count) == 0)
                for (uint j = 0; j < count; j++)
                {
                    IMFSensorProcessActivity act;
                    if (device.GetProcessActivity(j, out act) != 0 || act == null) continue;
                    uint pid; int streaming;
                    if (act.GetProcessId(out pid) == 0 && act.GetStreamingState(out streaming) == 0 && streaming != 0 && pid != ownPid && !pids.Contains(pid)) pids.Add(pid);
                    Marshal.ReleaseComObject(act);
                }
            Marshal.ReleaseComObject(device);
        }
        lock (liveLock) cameraPids = pids;
        var h = Changed;
        if (h != null) h();
    }

    public static List<string> LiveCamera()
    {
        List<uint> pids;
        lock (liveLock) pids = new List<uint>(cameraPids);
        return pids.Select(ProcessLabel).Where(x => x != null).Distinct().ToList();
    }

    // Microphone: any recording (capture) audio session that is active right now, the same thing the volume mixer sees.
    public static List<string> LiveMicrophone()
    {
        var list = new List<string>();
        try
        {
            var en = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
            IntPtr pc;
            if (en.EnumAudioEndpoints(1, 1, out pc) != 0 || pc == IntPtr.Zero) return list;    // eCapture, DEVICE_STATE_ACTIVE
            var devices = (IMMDeviceCollection)Marshal.GetObjectForIUnknown(pc);
            Marshal.Release(pc);
            uint n;
            devices.GetCount(out n);
            var iid = typeof(IAudioSessionManager2).GUID;
            for (uint i = 0; i < n; i++)
            {
                IMMDevice dev;
                object o;
                if (devices.Item(i, out dev) != 0 || dev.Activate(ref iid, 23, IntPtr.Zero, out o) != 0) continue;
                IAudioSessionEnumerator sessions;
                if (((IAudioSessionManager2)o).GetSessionEnumerator(out sessions) == 0)
                {
                    int count;
                    sessions.GetCount(out count);
                    for (int k = 0; k < count; k++)
                    {
                        object so;
                        if (sessions.GetSession(k, out so) != 0) continue;
                        var ctl = so as IAudioSessionControl2;
                        int state; uint pid;
                        if (ctl != null && ctl.GetState(out state) == 0 && state == 1 && ctl.GetProcessId(out pid) == 0 && pid != 0 && pid != ownPid)
                        {
                            var name = ProcessLabel(pid);
                            if (name != null && !list.Contains(name)) list.Add(name);
                        }
                        Marshal.ReleaseComObject(so);
                    }
                    Marshal.ReleaseComObject(sessions);
                }
                Marshal.ReleaseComObject(o);
                Marshal.ReleaseComObject(dev);
            }
            Marshal.ReleaseComObject(devices);
            Marshal.ReleaseComObject(en);
        }
        catch (Exception) { }
        return list;
    }

    static string ProcessLabel(uint pid)
    {
        try
        {
            var name = System.Diagnostics.Process.GetProcessById((int)pid).ProcessName;
            return name.Length > 0 ? char.ToUpper(name[0]) + name.Substring(1) : null;
        }
        catch (Exception) { return null; }
    }
    public static List<string> InUse(string capability)
    {
        var list = new List<string>();
        try
        {
            using (var key = Registry.CurrentUser.OpenSubKey(Root + capability))
            {
                if (key == null) return list;
                Scan(key, list, false);
                using (var np = key.OpenSubKey("NonPackaged")) if (np != null) Scan(np, list, true);
            }
        }
        catch (Exception) { }
        return list;
    }

    static void Scan(RegistryKey key, List<string> list, bool desktopApp)
    {
        foreach (var name in key.GetSubKeyNames())
        {
            if (name == "NonPackaged") continue;
            using (var app = key.OpenSubKey(name))
            {
                if (app == null) continue;
                object start = app.GetValue("LastUsedTimeStart"), stop = app.GetValue("LastUsedTimeStop");
                if (start is long && (long)start != 0 && stop is long && (long)stop == 0) list.Add(Pretty(name, desktopApp));
            }
        }
    }

    // "C:#Program Files#Google#Chrome#Application#chrome.exe" -> "Chrome"; "Microsoft.WindowsCamera_8wekyb3d8bbwe" -> "WindowsCamera".
    static string Pretty(string name, bool desktopApp)
    {
        string s = name;
        if (desktopApp)
        {
            s = s.Substring(s.LastIndexOf('#') + 1);
            if (s.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) s = s.Substring(0, s.Length - 4);
        }
        else
        {
            int u = s.IndexOf('_');
            if (u > 0) s = s.Substring(0, u);
            s = s.Substring(s.LastIndexOf('.') + 1);
        }
        return s.Length > 0 ? char.ToUpper(s[0]) + s.Substring(1) : name;
    }
}

// ---------------------------------------------------------------- Core Audio (volume)

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom { }

[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator
{
    [PreserveSig] int EnumAudioEndpoints(int flow, int mask, out IntPtr devices);
    [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out IMMDevice device);
    [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IMMDevice device);
    [PreserveSig] int RegisterEndpointNotificationCallback(IMMNotificationClient client);
    [PreserveSig] int UnregisterEndpointNotificationCallback(IMMNotificationClient client);
}

[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice
{
    [PreserveSig] int Activate(ref Guid iid, int clsCtx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
}
[ComImport, Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceCollection
{
    [PreserveSig] int GetCount(out uint count);
    [PreserveSig] int Item(uint index, out IMMDevice device);
}

[ComImport, Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionManager2
{
    [PreserveSig] int GetAudioSessionControl(IntPtr a, uint b, out IntPtr c);
    [PreserveSig] int GetSimpleAudioVolume(IntPtr a, uint b, out IntPtr c);
    [PreserveSig] int GetSessionEnumerator(out IAudioSessionEnumerator sessions);
}

[ComImport, Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionEnumerator
{
    [PreserveSig] int GetCount(out int count);
    [PreserveSig] int GetSession(int index, [MarshalAs(UnmanagedType.IUnknown)] out object session);
}

[ComImport, Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionControl2
{
    [PreserveSig] int GetState(out int state);    // 1 = active (recording right now)
    [PreserveSig] int GetDisplayName(out IntPtr name);
    [PreserveSig] int SetDisplayName(IntPtr name, IntPtr ctx);
    [PreserveSig] int GetIconPath(out IntPtr path);
    [PreserveSig] int SetIconPath(IntPtr path, IntPtr ctx);
    [PreserveSig] int GetGroupingParam(out Guid g);
    [PreserveSig] int SetGroupingParam(ref Guid g, IntPtr ctx);
    [PreserveSig] int RegisterAudioSessionNotification(IntPtr n);
    [PreserveSig] int UnregisterAudioSessionNotification(IntPtr n);
    [PreserveSig] int GetSessionIdentifier(out IntPtr id);
    [PreserveSig] int GetSessionInstanceIdentifier(out IntPtr id);
    [PreserveSig] int GetProcessId(out uint pid);
}

// Media Foundation's sensor activity monitor: Windows calls back whenever an app starts or stops streaming
// from a camera. It only reports activity; it never opens the camera itself.
[ComImport, Guid("D0CEF145-B3F4-4340-A2E5-7A5080CA05CB"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMFSensorActivityMonitor
{
    [PreserveSig] int Start();
    [PreserveSig] int Stop();
}

[ComImport, Guid("DE5072EE-DBE3-46DC-8A87-B6F631194751"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMFSensorActivitiesReportCallback
{
    [PreserveSig] int OnActivitiesReport(IMFSensorActivitiesReport report);
}

[ComImport, Guid("683F7A5E-4A19-43CD-B1A9-DBF4AB3F7777"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMFSensorActivitiesReport
{
    [PreserveSig] int GetCount(out uint count);
    [PreserveSig] int GetActivityReport(uint index, out IMFSensorActivityReport report);
    [PreserveSig] int GetActivityReportByDeviceName([MarshalAs(UnmanagedType.LPWStr)] string name, out IMFSensorActivityReport report);
}

[ComImport, Guid("3E8C4BE1-A8C2-4528-90DE-2851BDE5FEAD"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMFSensorActivityReport
{
    [PreserveSig] int GetFriendlyName([MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder name, uint size, out uint written);
    [PreserveSig] int GetSymbolicLink([MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder link, uint size, out uint written);
    [PreserveSig] int GetProcessCount(out uint count);
    [PreserveSig] int GetProcessActivity(uint index, out IMFSensorProcessActivity activity);
}

[ComImport, Guid("39DC7F4A-B141-4719-813C-A7F46162A2B8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMFSensorProcessActivity
{
    [PreserveSig] int GetProcessId(out uint pid);
    [PreserveSig] int GetStreamingState(out int streaming);
    [PreserveSig] int GetStreamingMode(out int mode);
    [PreserveSig] int GetReportTime(out long fileTime);
}

[ComVisible(true), ClassInterface(ClassInterfaceType.None)]
class SensorReportCallback : IMFSensorActivitiesReportCallback
{
    readonly Action<IMFSensorActivitiesReport> onReport;
    public SensorReportCallback(Action<IMFSensorActivitiesReport> onReport) { this.onReport = onReport; }
    public int OnActivitiesReport(IMFSensorActivitiesReport report)
    {
        try { onReport(report); } catch (Exception) { }
        return 0;
    }
}

[ComImport, Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioEndpointVolume
{
    [PreserveSig] int RegisterControlChangeNotify(IAudioEndpointVolumeCallback notify);
    [PreserveSig] int UnregisterControlChangeNotify(IAudioEndpointVolumeCallback notify);
    [PreserveSig] int GetChannelCount(out uint count);
    [PreserveSig] int SetMasterVolumeLevel(float db, ref Guid ctx);
    [PreserveSig] int SetMasterVolumeLevelScalar(float level, ref Guid ctx);
    [PreserveSig] int GetMasterVolumeLevel(out float db);
    [PreserveSig] int GetMasterVolumeLevelScalar(out float level);
    [PreserveSig] int SetChannelVolumeLevel(uint ch, float db, ref Guid ctx);
    [PreserveSig] int SetChannelVolumeLevelScalar(uint ch, float level, ref Guid ctx);
    [PreserveSig] int GetChannelVolumeLevel(uint ch, out float db);
    [PreserveSig] int GetChannelVolumeLevelScalar(uint ch, out float level);
    [PreserveSig] int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid ctx);
    [PreserveSig] int GetMute([MarshalAs(UnmanagedType.Bool)] out bool mute);
}

[ComImport, Guid("657804FA-D6AD-4496-8A60-352752AF4F89"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioEndpointVolumeCallback
{
    [PreserveSig] int OnNotify(IntPtr data);
}

[ComImport, Guid("7991EEC9-7E89-4D85-8390-6C703CEC60C0"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMNotificationClient
{
    [PreserveSig] int OnDeviceStateChanged([MarshalAs(UnmanagedType.LPWStr)] string id, int state);
    [PreserveSig] int OnDeviceAdded([MarshalAs(UnmanagedType.LPWStr)] string id);
    [PreserveSig] int OnDeviceRemoved([MarshalAs(UnmanagedType.LPWStr)] string id);
    [PreserveSig] int OnDefaultDeviceChanged(int flow, int role, [MarshalAs(UnmanagedType.LPWStr)] string id);
    [PreserveSig] int OnPropertyValueChanged([MarshalAs(UnmanagedType.LPWStr)] string id, IntPtr key);
}

// Master volume of the default speakers. Changed fires on any thread: (level 0..1, muted, changedByAnotherApp).
class Volume : IAudioEndpointVolumeCallback, IMMNotificationClient
{
    public static Guid Context = Guid.NewGuid();
    IMMDeviceEnumerator enumerator;
    IAudioEndpointVolume endpoint;
    public event Action<float, bool, bool> Changed;
    public event Action DefaultDeviceChanged;

    public bool Init()
    {
        try
        {
            if (enumerator == null)
            {
                enumerator = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
                enumerator.RegisterEndpointNotificationCallback(this);
            }
            Release();
            IMMDevice device;
            if (enumerator.GetDefaultAudioEndpoint(0, 1, out device) != 0 || device == null) return false;    // eRender, eMultimedia
            Guid iid = typeof(IAudioEndpointVolume).GUID;
            object o;
            if (device.Activate(ref iid, 23, IntPtr.Zero, out o) != 0) return false;                         // CLSCTX_ALL
            endpoint = (IAudioEndpointVolume)o;
            endpoint.RegisterControlChangeNotify(this);
            Marshal.ReleaseComObject(device);
            return true;
        }
        catch (Exception) { endpoint = null; return false; }
    }

    void Release()
    {
        if (endpoint == null) return;
        try { endpoint.UnregisterControlChangeNotify(this); Marshal.ReleaseComObject(endpoint); } catch (Exception) { }
        endpoint = null;
    }

    public bool Available { get { return endpoint != null; } }

    public float Level
    {
        get { float v = 0; if (endpoint != null) endpoint.GetMasterVolumeLevelScalar(out v); return v; }
        set { if (endpoint != null) endpoint.SetMasterVolumeLevelScalar(Math.Max(0f, Math.Min(1f, value)), ref Context); }
    }

    public bool Muted
    {
        get { bool m = false; if (endpoint != null) endpoint.GetMute(out m); return m; }
        set { if (endpoint != null) endpoint.SetMute(value, ref Context); }
    }

    public int OnNotify(IntPtr data)
    {
        var context = (Guid)Marshal.PtrToStructure(data, typeof(Guid));
        bool muted = Marshal.ReadInt32(data, 16) != 0;
        float level = BitConverter.ToSingle(BitConverter.GetBytes(Marshal.ReadInt32(data, 20)), 0);
        var h = Changed;
        if (h != null) h(level, muted, context != Context);
        return 0;
    }

    public int OnDeviceStateChanged(string id, int state) { return 0; }
    public int OnDeviceAdded(string id) { return 0; }
    public int OnDeviceRemoved(string id) { return 0; }
    public int OnPropertyValueChanged(string id, IntPtr key) { return 0; }
    public int OnDefaultDeviceChanged(int flow, int role, string id)
    {
        if (flow == 0 && role == 1) { var h = DefaultDeviceChanged; if (h != null) h(); }
        return 0;
    }
}

// ---------------------------------------------------------------- Brightness (WMI)

static class Brightness
{
    static readonly object gate = new object();
    static int pending = -1;
    static bool busy;

    public static int Get()
    {
        try
        {
            using (var s = new ManagementObjectSearcher("root\\WMI", "SELECT CurrentBrightness FROM WmiMonitorBrightness"))
            using (var all = s.Get())
                foreach (ManagementObject mo in all)
                    using (mo) return Convert.ToInt32(mo["CurrentBrightness"]);
        }
        catch (Exception) { }
        return -1;
    }

    public static void Set(int level)
    {
        level = Math.Max(0, Math.Min(100, level));
        using (var mc = new ManagementClass("root\\WMI", "WmiMonitorBrightnessMethods", null))
        using (var all = mc.GetInstances())
            foreach (ManagementObject mo in all)
                using (mo) mo.InvokeMethod("WmiSetBrightness", new object[] { (uint)1, (byte)level });
    }

    // Slider drags call this many times a second; only the latest value is sent.
    public static void SetLater(int level)
    {
        lock (gate)
        {
            pending = level;
            if (busy) return;
            busy = true;
        }
        ThreadPool.QueueUserWorkItem(delegate
        {
            while (true)
            {
                int v;
                lock (gate)
                {
                    v = pending;
                    pending = -1;
                    if (v < 0) { busy = false; return; }
                }
                try { Set(v); } catch (Exception) { }
            }
        });
    }
}

// ---------------------------------------------------------------- Night light (undocumented CloudStore blob)

static class NightLight
{
    const string Key = @"Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\DefaultAccount\Current\default$windows.data.bluelightreduction.bluelightreductionstate\windows.data.bluelightreduction.bluelightreductionstate";

    static byte[] Read()
    {
        try
        {
            using (var k = Registry.CurrentUser.OpenSubKey(Key))
                return k == null ? null : k.GetValue("Data") as byte[];
        }
        catch (Exception) { return null; }
    }

    // The layout this code understands: "CB" header, a timestamp at byte 10, the inner record's
    // length at byte 18, the inner "CB" header at 19..22, and "02 01" at 23 when night light is on.
    static bool Valid(byte[] d)
    {
        return d != null && d.Length > 26 && d[0] == 0x43 && d[1] == 0x42 && d[15] == 0x2A && d[16] == 0x2B && d[17] == 0x0E
            && d[19] == 0x43 && d[20] == 0x42 && d[21] == 0x01 && d[22] == 0x00 && d.Length == d[18] + 22;
    }

    // 1 on, 0 off, -1 unknown.
    public static int State()
    {
        var d = Read();
        if (!Valid(d)) return -1;
        return d[23] == 0x02 && d[24] == 0x01 ? 1 : 0;
    }

    public static bool Set(bool on)
    {
        var d = Read();
        if (!Valid(d)) return false;
        bool now = d[23] == 0x02 && d[24] == 0x01;
        if (now == on) return true;

        var b = new List<byte>(d);
        if (on) { b.InsertRange(23, new byte[] { 0x02, 0x01 }); b[18] = (byte)(b[18] + 2); }
        else { b.RemoveRange(23, 2); b[18] = (byte)(b[18] - 2); }

        // Bump the timestamp so Windows notices the change.
        int end = 10;
        while (end < 15 && (b[end] & 0x80) != 0) end++;
        int oldLength = end - 10 + 1;
        var stamp = new List<byte>();
        ulong secs = (ulong)(DateTime.UtcNow - new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc)).TotalSeconds;
        do { byte x = (byte)(secs & 0x7F); secs >>= 7; if (secs != 0) x |= 0x80; stamp.Add(x); } while (secs != 0);
        if (stamp.Count == oldLength) for (int i = 0; i < oldLength; i++) b[10 + i] = stamp[i];

        try
        {
            using (var k = Registry.CurrentUser.OpenSubKey(Key, true))
            {
                if (k == null) return false;
                k.SetValue("Data", b.ToArray(), RegistryValueKind.Binary);
            }
            return true;
        }
        catch (Exception) { return false; }
    }

    public static void Restore(byte[] original)
    {
        using (var k = Registry.CurrentUser.OpenSubKey(Key, true)) k.SetValue("Data", original, RegistryValueKind.Binary);
    }

    public static byte[] Snapshot() { return Read(); }
}

// ---------------------------------------------------------------- CPU / GPU usage and GPU temperature

class Sensors
{
    [DllImport("pdh.dll", CharSet = CharSet.Unicode)] static extern int PdhOpenQuery(string source, IntPtr user, out IntPtr query);
    [DllImport("pdh.dll", CharSet = CharSet.Unicode)] static extern int PdhAddEnglishCounter(IntPtr query, string path, IntPtr user, out IntPtr counter);
    [DllImport("pdh.dll")] static extern int PdhCollectQueryData(IntPtr query);
    [DllImport("pdh.dll")] static extern int PdhGetFormattedCounterValue(IntPtr counter, uint format, IntPtr type, out FmtValue value);
    [DllImport("pdh.dll", CharSet = CharSet.Unicode)] static extern int PdhGetFormattedCounterArray(IntPtr counter, uint format, ref uint size, ref uint count, IntPtr buffer);
    [DllImport("nvml.dll")] static extern int nvmlInit_v2();
    [DllImport("nvml.dll")] static extern int nvmlDeviceGetHandleByIndex_v2(uint index, out IntPtr device);
    [DllImport("nvml.dll")] static extern int nvmlDeviceGetTemperature(IntPtr device, int sensor, out uint temp);

    [StructLayout(LayoutKind.Explicit)]
    struct FmtValue { [FieldOffset(0)] public uint Status; [FieldOffset(8)] public double Value; }

    const uint PDH_FMT_DOUBLE = 0x200;
    IntPtr query, gpuCounter, cpuCounter, downCounter, upCounter;
    bool ready, nvmlTried, nvmlOk;
    IntPtr nvDevice;
    IntPtr buffer;
    uint bufferSize;
    readonly Dictionary<string, double> engines = new Dictionary<string, double>();

    public void Init()
    {
        if (ready) return;
        if (PdhOpenQuery(null, IntPtr.Zero, out query) != 0) return;
        PdhAddEnglishCounter(query, @"\GPU Engine(*)\Utilization Percentage", IntPtr.Zero, out gpuCounter);
        if (PdhAddEnglishCounter(query, @"\Processor Information(_Total)\% Processor Utility", IntPtr.Zero, out cpuCounter) != 0)
            PdhAddEnglishCounter(query, @"\Processor(_Total)\% Processor Time", IntPtr.Zero, out cpuCounter);
        PdhAddEnglishCounter(query, @"\Network Interface(*)\Bytes Received/sec", IntPtr.Zero, out downCounter);
        PdhAddEnglishCounter(query, @"\Network Interface(*)\Bytes Sent/sec", IntPtr.Zero, out upCounter);
        PdhCollectQueryData(query);
        ready = true;
    }

    // cpu and gpu in percent, temp in degrees C, down and up in bytes a second; -1 means not available.
    public void Sample(out double cpu, out double gpu, out int temp, out double down, out double up)
    {
        cpu = -1; gpu = -1; temp = -1; down = -1; up = -1;
        if (!ready) Init();
        if (ready && PdhCollectQueryData(query) == 0)
        {
            FmtValue v;
            if (cpuCounter != IntPtr.Zero && PdhGetFormattedCounterValue(cpuCounter, PDH_FMT_DOUBLE, IntPtr.Zero, out v) == 0)
                cpu = Math.Max(0, Math.Min(100, v.Value));
            if (gpuCounter != IntPtr.Zero) gpu = ReadGpu();
            if (downCounter != IntPtr.Zero) down = SumAll(downCounter);
            if (upCounter != IntPtr.Zero) up = SumAll(upCounter);
        }
        temp = GpuTemp();
    }

    // Adds up every instance of a counter (all network adapters).
    double SumAll(IntPtr counter)
    {
        uint size = 0, count = 0;
        if ((uint)PdhGetFormattedCounterArray(counter, PDH_FMT_DOUBLE, ref size, ref count, IntPtr.Zero) != 0x800007D2) return -1;
        IntPtr buf = Marshal.AllocHGlobal((int)size);
        try
        {
            if (PdhGetFormattedCounterArray(counter, PDH_FMT_DOUBLE, ref size, ref count, buf) != 0) return -1;
            double sum = 0;
            for (int i = 0; i < count; i++)
            {
                IntPtr item = buf + i * 24;
                if (Marshal.ReadInt32(item, 8) == 0) sum += BitConverter.Int64BitsToDouble(Marshal.ReadInt64(item, 16));
            }
            return sum;
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    double ReadGpu()
    {
        uint size = bufferSize, count = 0;
        int r = PdhGetFormattedCounterArray(gpuCounter, PDH_FMT_DOUBLE, ref size, ref count, buffer);
        if ((uint)r == 0x800007D2)    // PDH_MORE_DATA
        {
            if (buffer != IntPtr.Zero) Marshal.FreeHGlobal(buffer);
            bufferSize = size + 4096;
            buffer = Marshal.AllocHGlobal((int)bufferSize);
            size = bufferSize;
            r = PdhGetFormattedCounterArray(gpuCounter, PDH_FMT_DOUBLE, ref size, ref count, buffer);
        }
        if (r != 0) return -1;

        // Like Task Manager: add up each engine type per adapter, then take the busiest.
        engines.Clear();
        for (int i = 0; i < count; i++)
        {
            IntPtr item = buffer + i * 24;
            if (Marshal.ReadInt32(item, 8) != 0) continue;
            string name = Marshal.PtrToStringUni(Marshal.ReadIntPtr(item));
            double value = BitConverter.Int64BitsToDouble(Marshal.ReadInt64(item, 16));
            int luid = name.IndexOf("luid_", StringComparison.Ordinal);
            int phys = name.IndexOf("_phys", StringComparison.Ordinal);
            int type = name.IndexOf("engtype_", StringComparison.Ordinal);
            if (luid < 0 || phys < luid || type < 0) continue;
            string key = name.Substring(luid, phys - luid) + name.Substring(type);
            double sum;
            engines.TryGetValue(key, out sum);
            engines[key] = sum + value;
        }
        double best = 0;
        foreach (var v in engines.Values) if (v > best) best = v;
        return Math.Min(100, best);
    }

    int GpuTemp()
    {
        try
        {
            if (!nvmlTried)
            {
                nvmlTried = true;
                nvmlOk = nvmlInit_v2() == 0 && nvmlDeviceGetHandleByIndex_v2(0, out nvDevice) == 0;
            }
            uint t;
            if (nvmlOk && nvmlDeviceGetTemperature(nvDevice, 0, out t) == 0) return (int)t;
        }
        catch (Exception) { nvmlOk = false; }
        return -1;
    }
}

// ---------------------------------------------------------------- Saved data

public class SavedClip { public string Kind { get; set; } public string Text { get; set; } public string File { get; set; } }
public class SavedShelfItem { public string Path { get; set; } public bool Pinned { get; set; } }

class Clip
{
    public bool IsImage;
    public string Text;
    public byte[] Png;
    public ImageSource Thumb;
    public string Hash;
    public bool Pinned;
    public string File;
}

class ShelfItem
{
    public string Path;
    public bool Pinned;
    public ImageSource Icon;
}

static class Store
{
    public static readonly string Dir = WinUltra.App.DataDir;
    public static string ClipsFile { get { return System.IO.Path.Combine(Dir, "clipboard-pins.json"); } }
    public static string ShelfFile { get { return System.IO.Path.Combine(Dir, "shelf.json"); } }
    public static string ImageDir { get { return System.IO.Path.Combine(Dir, "clip-images"); } }

    public static T Load<T>(string file) where T : class
    {
        try { if (File.Exists(file)) return new JavaScriptSerializer().Deserialize<T>(File.ReadAllText(file, Encoding.UTF8)); }
        catch (Exception) { }
        return null;
    }

    public static void Save(string file, object value)
    {
        try
        {
            Directory.CreateDirectory(Dir);
            string tmp = file + ".tmp";
            File.WriteAllText(tmp, new JavaScriptSerializer { MaxJsonLength = int.MaxValue }.Serialize(value), Encoding.UTF8);
            if (File.Exists(file)) File.Delete(file);
            File.Move(tmp, file);
        }
        catch (Exception) { }
    }

    public static void Log(string text)
    {
        try { Directory.CreateDirectory(Dir); File.AppendAllText(System.IO.Path.Combine(Dir, "error.log"), DateTime.Now + "  " + text + Environment.NewLine); }
        catch (Exception) { }
    }
}

// ---------------------------------------------------------------- Small UI pieces

static class UI
{
    public static readonly FontFamily TextFont = new FontFamily("Segoe UI Variable Text, Segoe UI");
    public static readonly FontFamily IconFont = new FontFamily("Segoe Fluent Icons, Segoe MDL2 Assets");
    public static readonly Brush Fg = B("#F5F5F7"), Dim = B("#9A9AA6"), Card = B("#14FFFFFF"), Rim = B("#1CFFFFFF"), Panel_ = B("#2A2A33"),
        Selected = B("#30FFFFFF"), Blue = B("#3D7BFF"), Orange = B("#FF9F0A"), Green = B("#30D158"), Red = B("#FF453A"), Purple = B("#9F7BFF");

    static void Spring(ScaleTransform s, double to, double ms)
    {
        var a = new DoubleAnimation(to, TimeSpan.FromMilliseconds(ms)) { EasingFunction = global::Spring.Soft };
        Timeline.SetDesiredFrameRate(a, Island.Fps);
        s.BeginAnimation(ScaleTransform.ScaleXProperty, a);
        s.BeginAnimation(ScaleTransform.ScaleYProperty, a);
    }

    // A bright edge that catches the light at the top-left and fades round to the bottom-right.
    public static readonly Brush Specular = MakeSpecular();

    static Brush MakeSpecular()
    {
        var b = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 1) };
        b.GradientStops.Add(new GradientStop(Color.FromArgb(0xB0, 255, 255, 255), 0));
        b.GradientStops.Add(new GradientStop(Color.FromArgb(0x1A, 255, 255, 255), 0.5));
        b.GradientStops.Add(new GradientStop(Color.FromArgb(0x50, 255, 255, 255), 1));
        b.Freeze();
        return b;
    }

    // A tinted glass fill: a lighter top fading to the colour itself.
    public static Brush GlassFill(Color c, byte alpha)
    {
        var top = Color.FromArgb(alpha, (byte)(c.R + (255 - c.R) * 0.3), (byte)(c.G + (255 - c.G) * 0.3), (byte)(c.B + (255 - c.B) * 0.3));
        var bottom = Color.FromArgb((byte)(alpha * 0.8), c.R, c.G, c.B);
        var b = new LinearGradientBrush(top, bottom, 90);
        b.Freeze();
        return b;
    }

    // A round glass badge for an icon: tinted fill, bright rim and a soft shine across the top.
    // Icons go into the Grid that is its Child.
    public static Border GlassBadge(double size, double shine)
    {
        var shineBrush = new LinearGradientBrush(Color.FromArgb(0x66, 255, 255, 255), Color.FromArgb(0, 255, 255, 255), 90);
        shineBrush.Freeze();
        var g = new Grid();
        g.Children.Add(new System.Windows.Shapes.Ellipse { Height = shine, Margin = new Thickness(size * 0.12, 1.5, size * 0.12, 0), VerticalAlignment = VerticalAlignment.Top, Fill = shineBrush, IsHitTestVisible = false });
        return new Border
        {
            Width = size, Height = size, CornerRadius = new CornerRadius(size / 2),
            Background = GlassFill(Color.FromRgb(0x9A, 0x9A, 0xAA), 0x3C), BorderBrush = Specular, BorderThickness = new Thickness(1), Child = g,
        };
    }

    public static Brush B(string hex)
    {
        var b = (SolidColorBrush)new BrushConverter().ConvertFromString(hex);
        b.Freeze();
        return b;
    }

    public static TextBlock Text(string s, double size, Brush fg, FontWeight weight)
    {
        return new TextBlock { Text = s, FontSize = size, Foreground = fg, FontWeight = weight, FontFamily = TextFont, TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center };
    }

    public static TextBlock Icon(string glyph, double size, Brush fg)
    {
        return new IconBlock { Text = glyph, FontSize = size, Foreground = fg, FontFamily = IconFont, VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Center };
    }

    // An icon whose drawn shape, not its line box, is centred. The icon font sits its glyphs high in
    // the line, so a plainly centred icon looked higher than the words next to it.
    public class IconBlock : TextBlock
    {
    static readonly Dictionary<string, double> offsets = new Dictionary<string, double>();
    static readonly Typeface face = new Typeface(UI.IconFont, FontStyles.Normal, FontWeights.Normal, FontStretches.Normal);
    readonly TranslateTransform shift = new TranslateTransform();

    static IconBlock()
    {
        PropertyChangedCallback recentre = (d, e) => { var b = (IconBlock)d; b.shift.Y = Offset(b.Text, b.FontSize); };
        TextProperty.OverrideMetadata(typeof(IconBlock), new FrameworkPropertyMetadata(string.Empty, recentre));
        FontSizeProperty.OverrideMetadata(typeof(IconBlock), new FrameworkPropertyMetadata(SystemFonts.MessageFontSize, FrameworkPropertyMetadataOptions.Inherits | FrameworkPropertyMetadataOptions.AffectsMeasure | FrameworkPropertyMetadataOptions.AffectsRender, recentre));
    }

    public IconBlock() { RenderTransform = shift; }

    static double Offset(string glyph, double size)
    {
        if (string.IsNullOrEmpty(glyph)) return 0;
        string key = glyph + "|" + size;
        double y;
        if (offsets.TryGetValue(key, out y)) return y;
        try
        {
#pragma warning disable 618
            var text = new FormattedText(glyph, System.Globalization.CultureInfo.InvariantCulture, FlowDirection.LeftToRight, face, size, Brushes.White);
#pragma warning restore 618
            var box = text.BuildGeometry(new Point(0, 0)).Bounds;
            y = box.IsEmpty ? 0 : text.Height / 2 - (box.Top + box.Height / 2);
        }
        catch (Exception) { y = 0; }
        offsets[key] = y;
        return y;
    }
    }

    // A clickable rounded box. It lightens on hover and stops the click reaching whatever is behind it.
    public static Border Button(UIElement child, double width, double height, double radius, Brush background, Action click)
    {
        var b = new Border { Child = child, CornerRadius = new CornerRadius(radius), Background = background ?? Brushes.Transparent, Cursor = Cursors.Hand };
        if (width > 0) b.Width = width;
        if (height > 0) b.Height = height;
        bool pressed = false;
        var scale = new ScaleTransform(1, 1);
        b.RenderTransform = scale;
        b.RenderTransformOrigin = new Point(0.5, 0.5);
        b.MouseEnter += delegate { b.Opacity = 0.8; };
        b.MouseLeave += delegate { b.Opacity = 1; if (pressed) Spring(scale, 1, 420); pressed = false; };
        b.MouseLeftButtonDown += delegate(object s, MouseButtonEventArgs e) { pressed = true; e.Handled = true; Spring(scale, 0.94, 260); };
        b.MouseLeftButtonUp += delegate(object s, MouseButtonEventArgs e)
        {
            e.Handled = true;
            if (!pressed) return;
            pressed = false;
            Spring(scale, 1, 480);
            if (click != null) click();
        };
        return b;
    }

    public static Border Panel(UIElement child, Thickness padding)
    {
        return new Border { Child = child, Background = Card, BorderBrush = Rim, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(20), Padding = padding };
    }

    public static Grid Columns(params GridLength[] widths)
    {
        var g = new Grid();
        foreach (var w in widths) g.ColumnDefinitions.Add(new ColumnDefinition { Width = w });
        return g;
    }

    public static T At<T>(T e, int column) where T : UIElement { Grid.SetColumn(e, column); return e; }
    public static T Row<T>(T e, int row) where T : UIElement { Grid.SetRow(e, row); return e; }
    public static readonly GridLength Auto = GridLength.Auto, Star = new GridLength(1, GridUnitType.Star);
    public static GridLength Px(double v) { return new GridLength(v); }
}

// An iOS-style fat slider: a rounded track whose white fill follows the mouse.
class BarSlider : Grid
{
    readonly Border fill = new Border { Background = new LinearGradientBrush(Color.FromRgb(0xFF, 0xFF, 0xFF), Color.FromRgb(0xD8, 0xDA, 0xE6), 90), HorizontalAlignment = HorizontalAlignment.Left, CornerRadius = new CornerRadius(16) };
    public readonly TextBlock Glyph;
    double value;
    public event Action<double> Changed;

    public BarSlider(string glyph, Action iconClick)
    {
        Height = 42;
        Cursor = Cursors.Hand;
        Children.Add(new Border { Background = UI.Card, BorderBrush = UI.Rim, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(16) });
        Children.Add(fill);
        Glyph = UI.Icon(glyph, 16, UI.B("#8E8E93"));
        Glyph.HorizontalAlignment = HorizontalAlignment.Left;
        Glyph.Margin = new Thickness(14, 0, 0, 0);
        Glyph.Background = Brushes.Transparent;
        if (iconClick != null) Glyph.MouseLeftButtonDown += delegate(object s, MouseButtonEventArgs e) { e.Handled = true; iconClick(); };
        Children.Add(Glyph);
        SizeChanged += delegate { Redraw(); };
        MouseLeftButtonDown += delegate(object s, MouseButtonEventArgs e) { CaptureMouse(); FromMouse(e); e.Handled = true; };
        MouseMove += delegate(object s, MouseEventArgs e) { if (IsMouseCaptured) FromMouse(e); };
        MouseLeftButtonUp += delegate { ReleaseMouseCapture(); };
        MouseWheel += delegate(object s, MouseWheelEventArgs e) { Set(value + (e.Delta > 0 ? 0.02 : -0.02)); e.Handled = true; };
    }

    public bool Dragging { get { return IsMouseCaptured; } }
    public double Value { get { return value; } set { this.value = Math.Max(0, Math.Min(1, value)); Redraw(); } }

    void FromMouse(MouseEventArgs e) { Set(e.GetPosition(this).X / Math.Max(1, ActualWidth)); }

    void Set(double v)
    {
        Value = v;
        var h = Changed;
        if (h != null) h(value);
    }

    void Redraw() { fill.Width = ActualWidth * value; }
}

// The iOS Control Center slider: a tall frosted pill whose white fill rises from the bottom.
// Dragging follows the finger exactly; changes made elsewhere glide in on a spring.
class VerticalBarSlider : Grid
{
    readonly Border fill = new Border { VerticalAlignment = VerticalAlignment.Bottom, Height = 0 };
    readonly Border shape = new Border();
    public readonly TextBlock Glyph;
    readonly Brush glyphLight = UI.B("#E6FFFFFF"), glyphDark = UI.B("#5A5A66");
    readonly RectangleGeometry clipShape = new RectangleGeometry();
    double value;
    public event Action<double> Changed;

    public VerticalBarSlider(string glyph, Action iconClick)
    {
        Cursor = Cursors.Hand;
        var fillBrush = new LinearGradientBrush(Color.FromRgb(0xFF, 0xFF, 0xFF), Color.FromRgb(0xE2, 0xE4, 0xEE), 90);
        fillBrush.Freeze();
        fill.Background = fillBrush;
        shape.Background = UI.Card;
        Children.Add(shape);
        Children.Add(fill);
        // Inset so the slider's rounded clip doesn't shave the rim off along the curves.
        Children.Add(new Border { BorderBrush = UI.Specular, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(23.25), Margin = new Thickness(0.75), IsHitTestVisible = false, Opacity = 0.7 });
        Glyph = UI.Icon(glyph, 18, glyphLight);
        Glyph.VerticalAlignment = VerticalAlignment.Bottom;
        Glyph.Margin = new Thickness(0, 0, 0, 16);
        Glyph.Background = Brushes.Transparent;
        if (iconClick != null) Glyph.MouseLeftButtonDown += delegate(object s, MouseButtonEventArgs e) { e.Handled = true; iconClick(); };
        Children.Add(Glyph);
        Clip = clipShape;
        SizeChanged += delegate
        {
            clipShape.Rect = new Rect(0, 0, ActualWidth, ActualHeight);
            clipShape.RadiusX = clipShape.RadiusY = 24;
            fill.BeginAnimation(HeightProperty, null);
            Redraw();
        };
        MouseLeftButtonDown += delegate(object s, MouseButtonEventArgs e) { CaptureMouse(); FromMouse(e); e.Handled = true; };
        MouseMove += delegate(object s, MouseEventArgs e) { if (IsMouseCaptured) FromMouse(e); };
        MouseLeftButtonUp += delegate { ReleaseMouseCapture(); };
        MouseWheel += delegate(object s, MouseWheelEventArgs e) { Set(value + (e.Delta > 0 ? 0.03 : -0.03), true); e.Handled = true; };
    }

    public bool Dragging { get { return IsMouseCaptured; } }

    // Setting Value from outside (a key press, the volume pop-up) animates the fill.
    public double Value { get { return value; } set { this.value = Math.Max(0, Math.Min(1, value)); Glide(); } }

    void FromMouse(MouseEventArgs e) { Set(1 - e.GetPosition(this).Y / Math.Max(1, ActualHeight), false); }

    void Set(double v, bool glide)
    {
        value = Math.Max(0, Math.Min(1, v));
        if (glide) Glide(); else { fill.BeginAnimation(HeightProperty, null); Redraw(); }
        var h = Changed;
        if (h != null) h(value);
    }

    void Glide()
    {
        var a = new DoubleAnimation(ActualHeight * value, TimeSpan.FromMilliseconds(420)) { EasingFunction = Spring.Soft };
        Timeline.SetDesiredFrameRate(a, Island.Fps);
        fill.BeginAnimation(HeightProperty, a);
        Glyph.Foreground = value > 0.2 ? glyphDark : glyphLight;
    }

    void Redraw()
    {
        fill.Height = ActualHeight * value;
        Glyph.Foreground = value > 0.2 ? glyphDark : glyphLight;
    }
}

// A small line graph that remembers the last 40 readings (60 seconds at 1.5 s per reading).
class Graph : FrameworkElement
{
    readonly double[] data = new double[40];
    int count;
    readonly Pen pen;
    readonly Brush area;
    public bool AutoScale;    // for network speed: the top of the graph follows the busiest recent reading

    public Graph(Color color)
    {
        pen = new Pen(new SolidColorBrush(color), 1.6) { LineJoin = PenLineJoin.Round };
        pen.Freeze();
        var gradient = new LinearGradientBrush(Color.FromArgb(90, color.R, color.G, color.B), Color.FromArgb(0, color.R, color.G, color.B), 90);
        gradient.Freeze();
        area = gradient;
        Height = 40;
    }

    public void Add(double v)
    {
        if (count < data.Length) data[count++] = v;
        else { Array.Copy(data, 1, data, 0, data.Length - 1); data[data.Length - 1] = v; }
        InvalidateVisual();
    }

    protected override void OnRender(DrawingContext dc)
    {
        double w = ActualWidth, h = ActualHeight;
        if (count < 2 || w <= 0) return;
        double step = w / (data.Length - 1);
        double x0 = w - (count - 1) * step;
        double top = 100;
        if (AutoScale)
        {
            top = 64 * 1024;
            for (int i = 0; i < count; i++) top = Math.Max(top, data[i] * 1.2);
        }
        var line = new StreamGeometry();
        var fillShape = new StreamGeometry();
        using (var c = line.Open())
        using (var f = fillShape.Open())
        {
            f.BeginFigure(new Point(x0, h), true, true);
            for (int i = 0; i < count; i++)
            {
                var p = new Point(x0 + i * step, h - 1 - Math.Max(0, Math.Min(top, data[i])) / top * (h - 2));
                if (i == 0) c.BeginFigure(p, false, false); else c.LineTo(p, true, true);
                f.LineTo(p, false, false);
            }
            f.LineTo(new Point(w, h), false, false);
        }
        line.Freeze();
        fillShape.Freeze();
        dc.DrawGeometry(area, null, fillShape);
        dc.DrawGeometry(null, pen, line);
    }
}

// ---------------------------------------------------------------- The island

// A damped spring, like iOS uses: a quick start, a slight overshoot and a soft settle.
class Spring : EasingFunctionBase
{
    double damping = 0.78;

    public static readonly Spring Soft = Make(0.86), Bouncy = Make(0.7);

    static Spring Make(double damping)
    {
        var s = new Spring { damping = damping, EasingMode = EasingMode.EaseIn };
        s.Freeze();
        return s;
    }

    protected override double EaseInCore(double t)
    {
        double w = 6.6 / damping;
        double wd = w * Math.Sqrt(1 - damping * damping);
        return 1 - Math.Exp(-damping * w * t) * (Math.Cos(wd * t) + damping * w / wd * Math.Sin(wd * t));
    }

    protected override Freezable CreateInstanceCore() { return new Spring { damping = damping }; }
}

class Island : Window
{
    // Sizes are in WPF units (1.25 screen pixels each on this laptop).
    const double LineW = 124, LineH = 6, PillW = 236, PillH = 34, PopW = 340, PopH = 44, NoteW = 392, NoteH = 62;
    const double ExpW = 520, ExpH = 262, WinW = 580, Gap = 5, SmallWinH = 84, BigWinH = ExpH + 40;
    const int TabMedia = 0, TabControls = 1, TabStats = 2, TabClipboard = 3, TabShelf = 4, TabTimer = 5, TabCount = 6;
    // Build.cmd makes three files that differ only in this number: WinUltra.exe (120 fps, the standard
    // build), WinUltra-HighFPS.exe (144 fps, for 144 Hz+ screens) and WinUltra-LowFPS.exe (60 fps, for slower PCs).
    // WacOS: one build that animates at the display's own rate (165 on a 165 Hz screen, 60 on a 60 Hz one)
    // instead of three fixed builds. HIGHFPS/LOWFPS still pin it; WINULTRA_FPS overrides it for testing.
#if HIGHFPS
    internal static readonly int Fps = 144;
#elif LOWFPS
    internal static readonly int Fps = 60;
#else
    internal static readonly int Fps = DisplayFps();
#endif
    static int DisplayFps()
    {
        int env;
        if (int.TryParse(Environment.GetEnvironmentVariable("WINULTRA_FPS"), out env) && env >= 30 && env <= 500) return env;
        var dm = new Native.DEVMODE { dmSize = (short)Marshal.SizeOf(typeof(Native.DEVMODE)) };
        int hz = Native.EnumDisplaySettings(null, -1, ref dm) ? dm.dmDisplayFrequency : 0;    // -1: current mode, primary display
        return hz >= 30 && hz <= 500 ? hz : 120;
    }
    const string ClockGlyph = "";

    enum Mode { Collapsed, Popup, Expanded }
    Mode mode = Mode.Collapsed;
    int animGen;
    IntPtr hwnd;
    uint openMessage;
    uint fmtExclude, fmtCanInclude, fmtViewerIgnore;

    readonly Grid root = new Grid();
    readonly Border island = new Border();
    readonly Border hitStrip = new Border();
    readonly Border lineGlow = new Border();
    readonly Grid aurora = new Grid();
    readonly Border rimOverlay = new Border();
    readonly Border baseLayer = new Border();
    readonly Border pillBadge = UI.GlassBadge(24, 12);
    readonly Grid glass = new Grid();
    readonly ImageBrush glassBrush = new ImageBrush { Stretch = Stretch.Fill, ViewportUnits = BrushMappingMode.Absolute };
    readonly RadialGradientBrush auroraA, auroraB;
    readonly RectangleGeometry clip = new RectangleGeometry();
    readonly Grid collapsedLayer = new Grid(), popupLayer = new Grid(), expandedLayer = new Grid();
    bool built;
    DispatcherTimer hoverTimer, leaveTimer, popupTimer, pollTimer, tickTimer, trimTimer, clipTimer, noteTimer, tabHoverTimer;
    int tabHoverTarget = -1;
    System.Threading.Timer sensorTimer;
    bool pinnedOpen, dragInside, draggingOut, hidden, lineMode = true;
    DateTime pinnedAt, forceShowUntil, ignoreClipUntil;
    int clipAttempts;

    // collapsed status
    readonly Border pillArt = new Border { Width = 22, Height = 22, CornerRadius = new CornerRadius(6) };
    readonly TextBlock pillIcon = UI.Icon("", 12.8, UI.Fg);
    readonly TextBlock pillText = UI.Text("", 14, UI.Fg, FontWeights.SemiBold);
    readonly StackPanel pillBars = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
    bool barsRunning;

    // tabs
    readonly Border[] tabButtons = new Border[TabCount];
    readonly TextBlock[] tabLabels = new TextBlock[TabCount];
    readonly Grid[] pages = new Grid[TabCount];
    int tab;
    TextBlock batteryLabel;

    // media page
    Border artBig;
    TextBlock mediaApp, mediaTitle, mediaArtist, playGlyph, posText, durText;
    Grid progressRow;
    Border progressFill;

    // controls page
    Border wifiTile, btTile, nightTile, muteTile;
    VerticalBarSlider brightSlider, volSlider;

    // stats page
    Graph cpuGraph, gpuGraph, netGraph;
    TextBlock netDown, netUp;
    TextBlock cpuValue, gpuValue, battBig, battStatus;
    Border battFill;

    // media state
    MediaManager mediaManager;
    MediaSession session;
    bool playing;
    string mediaKey, title, artist, appName;
    ImageSource artwork;
    Color tint = Color.FromRgb(0x4A, 0x6C, 0xFF);
    int mediaVersion;
    Windows.Foundation.TypedEventHandler<MediaSession, Windows.Media.Control.MediaPropertiesChangedEventArgs> propsHandler;
    Windows.Foundation.TypedEventHandler<MediaSession, Windows.Media.Control.PlaybackInfoChangedEventArgs> playbackHandler;

    // controls
    readonly Volume volume = new Volume();

    // camera and microphone dot
    readonly PrivacyWatch privacy = new PrivacyWatch();
    readonly System.Windows.Shapes.Ellipse privacyDot = new System.Windows.Shapes.Ellipse { Width = 11, Height = 11, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Top, IsHitTestVisible = false, Visibility = Visibility.Collapsed };
    readonly TranslateTransform privacyShift = new TranslateTransform();
    // A soft glow around the dot, so it stands out next to the bright line (camera green, microphone orange).
    readonly System.Windows.Media.Effects.DropShadowEffect privacyGlow = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 14, ShadowDepth = 0, Opacity = 0.95, Color = Color.FromRgb(0x30, 0xD1, 0x58) };
    bool privacyPulsing;
    List<string> camApps = new List<string>(), micApps = new List<string>();
    TextBlock privacyLabel;
    DispatcherTimer privacyTimer;

    // Bluetooth connect pop-ups
    const string BatteryKey = "{104EA319-6EE2-4701-BD47-8DDBF425BBE5} 2";
    readonly List<Windows.Devices.Enumeration.DeviceWatcher> btWatchers = new List<Windows.Devices.Enumeration.DeviceWatcher>();
    readonly Dictionary<string, string> btNames = new Dictionary<string, string>();
    int btCompleted;
    string lastBtName;
    DateTime lastBtAt;

    // brightness keys and Caps Lock
    ManagementEventWatcher brightWatcher;
    DateTime ownBrightnessUntil;
    Popup brightPopupCache;
    readonly IntPtr rawBuffer = Marshal.AllocHGlobal(64);

    // clipboard picker hotkey
    bool pickerMode;
    IntPtr pasteTarget;
    string pickerKeys = "";

    // focus timer
    bool focusMode;
    Radio wifi, bluetooth;
    readonly Sensors sensors = new Sensors();
    readonly object sensorGate = new object();

    // power
    bool onAc, wasOnAc;
    int batteryPercent = -1, lowWarned = 101;

    // notifications
    Windows.UI.Notifications.Management.UserNotificationListener listener;
    HashSet<uint> seenNotes;
    bool notePolling;

    // clipboard
    readonly List<Clip> clips = new List<Clip>();
    StackPanel clipList;
    TextBlock clipCount;

    // shelf
    readonly List<ShelfItem> shelf = new List<ShelfItem>();
    StackPanel shelfList;
    Point dragStart;
    ShelfItem dragCandidate;

    // timer and stopwatch
    bool timerRunning, timerPaused;
    DateTime timerEnd;
    TimeSpan timerLeft;
    int customMinutes = 15;
    readonly Stopwatch stopwatch = new Stopwatch();
    TextBlock timerText, timerPauseLabel, swText, swStartLabel, customLabel;
    Popup volumePopupCache;

    class Popup { public Grid Root; public Border Fill; public TextBlock Value; public TextBlock Glyph; }

    public Island()
    {
        Title = WinUltra.App.IslandTitle;
        WindowStyle = WindowStyle.None;
        AllowsTransparency = true;
        Background = Brushes.Transparent;
        Topmost = true;
        ShowInTaskbar = false;
        ShowActivated = false;
        ResizeMode = ResizeMode.NoResize;
        Width = WinW;
        Height = SmallWinH;
        Top = 0;
        Left = (SystemParameters.PrimaryScreenWidth - WinW) / 2;
        FontFamily = UI.TextFont;
        UseLayoutRounding = true;
        TextOptions.SetTextRenderingMode(this, TextRenderingMode.Grayscale);
        TextOptions.SetTextFormattingMode(this, TextFormattingMode.Display);
        TextOptions.SetTextHintingMode(this, TextHintingMode.Fixed);

        // Every pixel of the island is opaque: this screen blends see-through layered-window
        // pixels in linear light, which washes dark glass out. The glass is a blurred copy of
        // the screen behind (see Backdrop) under a dark tint, with a bright rim on top.
        island.HorizontalAlignment = HorizontalAlignment.Center;
        island.VerticalAlignment = VerticalAlignment.Top;
        island.Margin = new Thickness(0, Gap, 0, 0);
        island.Width = LineW;
        island.Height = LineH;
        // The solid dark base is its own layer, faded out while the glass shows: painted as the
        // island's background it left a dark fringe round the anti-aliased edge of the glass.
        baseLayer.Background = new LinearGradientBrush(Color.FromRgb(0x1C, 0x1C, 0x24), Color.FromRgb(0x07, 0x07, 0x0A), 90);
        baseLayer.IsHitTestVisible = false;
        island.Background = Brushes.Transparent;
        var rim = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 1) };
        rim.GradientStops.Add(new GradientStop(Color.FromArgb(0x9A, 255, 255, 255), 0));
        rim.GradientStops.Add(new GradientStop(Color.FromArgb(0x18, 255, 255, 255), 0.45));
        rim.GradientStops.Add(new GradientStop(Color.FromArgb(0x10, 255, 255, 255), 0.7));
        rim.GradientStops.Add(new GradientStop(Color.FromArgb(0x55, 255, 255, 255), 1));
        // The bright rim is drawn just inside the shape: on the edge itself the rounded clip
        // cut it in half along the curves, which made the corners look stepped.
        rimOverlay.BorderBrush = rim;
        rimOverlay.BorderThickness = new Thickness(1);
        rimOverlay.Margin = new Thickness(0.75);
        rimOverlay.IsHitTestVisible = false;
        rimOverlay.Opacity = 0;

        glass.Children.Add(new Border { Background = glassBrush });
        var tintBrush = new LinearGradientBrush(Color.FromArgb(0x8C, 0x10, 0x10, 0x16), Color.FromArgb(0xB8, 0x08, 0x08, 0x0C), 90);
        glass.Children.Add(new Border { Background = tintBrush });
        glass.Opacity = 0;
        glass.IsHitTestVisible = false;
        RenderOptions.SetBitmapScalingMode(glass, BitmapScalingMode.Linear);
        island.Clip = clip;
        island.SizeChanged += delegate { Reshape(); };

        auroraA = Glow(tint);
        auroraB = Glow(Color.FromRgb(0xB0, 0x4B, 0xFF));
        aurora.Children.Add(new Border { Width = 360, Height = 240, Background = auroraA, HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top, Margin = new Thickness(-120, -110, 0, 0) });
        aurora.Children.Add(new Border { Width = 380, Height = 260, Background = auroraB, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(0, 0, -150, -140) });
        aurora.Opacity = 0;
        aurora.IsHitTestVisible = false;

        // The idle state: a thin line with a soft blue-violet-pink sheen.
        var lineBrush = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
        lineBrush.GradientStops.Add(new GradientStop(Color.FromRgb(0x6F, 0x8C, 0xFF), 0));
        lineBrush.GradientStops.Add(new GradientStop(Color.FromRgb(0xB9, 0x7C, 0xFF), 0.5));
        lineBrush.GradientStops.Add(new GradientStop(Color.FromRgb(0xFF, 0x8F, 0xCF), 1));
        lineBrush.Freeze();
        lineGlow.Background = lineBrush;
        lineGlow.IsHitTestVisible = false;

        var sheen = new Border { IsHitTestVisible = false, VerticalAlignment = VerticalAlignment.Top, Height = 60 };
        sheen.Background = new LinearGradientBrush(Color.FromArgb(0x16, 255, 255, 255), Color.FromArgb(0, 255, 255, 255), 90);

        var layers = new Grid();
        layers.Children.Add(baseLayer);
        layers.Children.Add(glass);
        layers.Children.Add(aurora);
        layers.Children.Add(sheen);
        layers.Children.Add(lineGlow);
        layers.Children.Add(collapsedLayer);
        layers.Children.Add(popupLayer);
        layers.Children.Add(expandedLayer);
        layers.Children.Add(rimOverlay);
        island.Child = layers;

        // The line is too thin to aim at, so an invisible strip around it catches the mouse.
        hitStrip.Width = 200;
        hitStrip.Height = 16;
        hitStrip.VerticalAlignment = VerticalAlignment.Top;
        hitStrip.HorizontalAlignment = HorizontalAlignment.Center;
        hitStrip.Background = new SolidColorBrush(Color.FromArgb(1, 0, 0, 0));
        root.Children.Add(hitStrip);
        root.Children.Add(island);
        privacyDot.RenderTransform = privacyShift;
        privacyDot.Effect = privacyGlow;
        root.Children.Add(privacyDot);
        root.AllowDrop = true;
        Content = root;

        hoverTimer = MakeTimer(120, delegate { hoverTimer.Stop(); Expand(false); });
        leaveTimer = MakeTimer(380, delegate { leaveTimer.Stop(); MaybeCollapse(); });
        popupTimer = MakeTimer(2000, delegate { popupTimer.Stop(); EndPopup(); });
        pollTimer = MakeTimer(1500, delegate { Poll(); });
        tickTimer = MakeTimer(1000, delegate { Tick(); });
        trimTimer = MakeTimer(3000, delegate { trimTimer.Stop(); if (mode != Mode.Expanded) Native.TrimMemory(); });
        clipTimer = MakeTimer(90, delegate { clipTimer.Stop(); ReadClipboard(); });
        noteTimer = MakeTimer(3000, delegate { PollNotifications(); });
        privacyTimer = MakeTimer(300, delegate { privacyTimer.Stop(); UpdatePrivacy(); });
        MakeTimer(2000, delegate { UpdatePrivacy(); }).Start();    // microphone sessions
        tabHoverTimer = MakeTimer(140, delegate { tabHoverTimer.Stop(); if (mode == Mode.Expanded && tabHoverTarget >= 0) SelectTab(tabHoverTarget); });

        BuildCollapsed();
        foreach (var layer in new[] { collapsedLayer, popupLayer, expandedLayer })
        {
            layer.Visibility = Visibility.Collapsed;
            layer.Opacity = 0;
            layer.RenderTransformOrigin = new Point(0.5, 0);
            layer.RenderTransform = new ScaleTransform(1, 1);
        }

        root.MouseEnter += delegate
        {
            leaveTimer.Stop();
            pinnedOpen = false;
            if (mode != Mode.Expanded) hoverTimer.Start();
        };
        root.MouseLeave += delegate
        {
            hoverTimer.Stop();
            if (mode == Mode.Expanded) leaveTimer.Start();
        };
        root.MouseLeftButtonUp += delegate { if (mode != Mode.Expanded) Expand(false); };
        root.DragEnter += OnDragOver;
        root.DragOver += OnDragOver;
        root.DragLeave += delegate { dragInside = false; leaveTimer.Stop(); leaveTimer.Start(); };
        root.Drop += OnDrop;

        SourceInitialized += OnSourceInitialized;
        Loaded += OnLoaded;
        Closed += delegate { Cleanup(); };
        SystemEvents.DisplaySettingsChanged += delegate
        {
            Dispatcher.BeginInvoke(new Action(delegate
            {
                Left = (SystemParameters.PrimaryScreenWidth - Width) / 2;
            }));
        };
    }

    static RadialGradientBrush Glow(Color c)
    {
        var b = new RadialGradientBrush();
        b.GradientStops.Add(new GradientStop(Color.FromArgb(0x50, c.R, c.G, c.B), 0));
        b.GradientStops.Add(new GradientStop(Color.FromArgb(0x18, c.R, c.G, c.B), 0.55));
        b.GradientStops.Add(new GradientStop(Color.FromArgb(0, c.R, c.G, c.B), 1));
        return b;
    }

    void SetTint(Color c)
    {
        tint = c;
        var a = new ColorAnimation(Color.FromArgb(0x50, c.R, c.G, c.B), TimeSpan.FromMilliseconds(600));
        auroraA.GradientStops[0].BeginAnimation(GradientStop.ColorProperty, a);
        auroraA.GradientStops[1].BeginAnimation(GradientStop.ColorProperty, new ColorAnimation(Color.FromArgb(0x18, c.R, c.G, c.B), TimeSpan.FromMilliseconds(600)));
        auroraA.GradientStops[2].Color = Color.FromArgb(0, c.R, c.G, c.B);
    }

    DispatcherTimer MakeTimer(int ms, EventHandler tick)
    {
        var t = new DispatcherTimer(DispatcherPriority.Normal) { Interval = TimeSpan.FromMilliseconds(ms) };
        t.Tick += tick;
        return t;
    }

    void Later(int ms, Action action)
    {
        var t = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(ms) };
        t.Tick += delegate { t.Stop(); action(); };
        t.Start();
    }

    void Ui(Action a) { Dispatcher.BeginInvoke(a); }

    // Sizes the (mostly see-through) window around the island, keeping it centred.
    void Resize(double w, double h)
    {
        if (Math.Abs(Width - w) > 0.5) { Width = w; Left = (SystemParameters.PrimaryScreenWidth - w) / 2; }
        if (Math.Abs(Height - h) > 0.5) Height = h;
    }

    void Reshape()
    {
        double w = island.ActualWidth, h = island.ActualHeight;
        double r = Math.Min(h / 2, 28);
        island.CornerRadius = new CornerRadius(r);
        clip.Rect = new Rect(0, 0, w, h);
        clip.RadiusX = r;
        clip.RadiusY = r;
        // Keep the glass pinned to the screen while the island grows around it.
        glassBrush.Viewport = new Rect((w - ExpW) / 2, 0, ExpW, ExpH);
        rimOverlay.CornerRadius = new CornerRadius(Math.Max(0, r - 0.75));
        // The camera/mic dot rides just right of the island, like the iPhone's.
        privacyShift.X = w / 2 + 14;
        privacyDot.Margin = new Thickness(0, Gap + h / 2 - 5.5, 0, 0);
    }

    // The solid base hides under the glass, unless frosted glass is switched off.
    double BaseUnderGlass
    {
        get
        {
#if FRIENDS
            return WinUltra.App.Options.FrostedGlass ? 0 : 1;
#else
            return 0;
#endif
        }
    }

    // Takes a fresh blurred copy of the screen where the open panel will be.
    void CaptureBackdrop()
    {
#if FRIENDS
        if (!WinUltra.App.Options.FrostedGlass) { glassBrush.ImageSource = null; return; }
#endif
        try
        {
            var source = PresentationSource.FromVisual(this);
            if (source == null) return;
            double s = source.CompositionTarget.TransformToDevice.M11;
            int x = (int)Math.Round((SystemParameters.PrimaryScreenWidth - ExpW) / 2 * s), y = (int)Math.Round((Top + Gap) * s);
            glassBrush.ImageSource = Backdrop.Capture(x, y, (int)Math.Round(ExpW * s), (int)Math.Round(ExpH * s));
        }
        catch (Exception ex) { Store.Log("backdrop: " + ex.Message); }
    }

    // ---------------------------------------------------------------- window setup

    void OnSourceInitialized(object sender, EventArgs e)
    {
        hwnd = new WindowInteropHelper(this).Handle;
        int ex = Native.GetWindowLong(hwnd, -20);
        Native.SetWindowLong(hwnd, -20, ex | 0x08000000 | 0x80);    // WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW
        HwndSource.FromHwnd(hwnd).AddHook(WndProc);
        Native.RegisterHotKey(hwnd, 1, 0x0001 | 0x0002 | 0x4000, 0x20);    // Ctrl+Alt+Space, no repeat
        // The clipboard picker. Not Ctrl+Alt+V, which is Paste Special in Word.
        if (Native.RegisterHotKey(hwnd, 2, 0x0008 | 0x0004 | 0x4000, 0x56)) pickerKeys = "Win+Shift+V";
        else if (Native.RegisterHotKey(hwnd, 2, 0x0001 | 0x0002 | 0x0004 | 0x4000, 0x56)) pickerKeys = "Ctrl+Alt+Shift+V";
#if !FRIENDS
        // Keyboard input in the background, only to notice Caps Lock (RIDEV_INPUTSINK).
        var kb = new[] { new Native.RAWINPUTDEVICE { UsagePage = 1, Usage = 6, Flags = 0x100, Target = hwnd } };
        Native.RegisterRawInputDevices(kb, 1, (uint)Marshal.SizeOf(typeof(Native.RAWINPUTDEVICE)));
#endif
        openMessage = Native.RegisterWindowMessage("DynamicIslandOpenTab");
        fmtExclude = Native.RegisterClipboardFormat("ExcludeClipboardContentFromMonitorProcessing");
        fmtCanInclude = Native.RegisterClipboardFormat("CanIncludeInClipboardHistory");
        fmtViewerIgnore = Native.RegisterClipboardFormat("Clipboard Viewer Ignore");
#if FRIENDS
        if (WinUltra.App.Options.ClipboardHistory) Native.AddClipboardFormatListener(hwnd);
#else
        Native.AddClipboardFormatListener(hwnd);
#endif
    }

    void OnLoaded(object sender, EventArgs e)
    {
        LoadClips();
        LoadShelf();
        InitMedia();
        InitRadios();
        InitPower();
#if FRIENDS
        if (WinUltra.App.Options.Notifications) InitNotifications();
        if (WinUltra.App.Options.CapsLock) SetCapsLockPopup(true);
#else
        InitNotifications();
#endif
        volume.Changed += (level, muted, external) => Ui(delegate { OnVolumeChanged(level, muted, external); });
        volume.DefaultDeviceChanged += () => Ui(delegate { volume.Init(); });
        volume.Init();
        pollTimer.Start();
        ApplyCollapsed(false);
        try { privacy.Changed += () => Ui(delegate { privacyTimer.Stop(); privacyTimer.Start(); }); privacy.Start(); UpdatePrivacy(); }
        catch (Exception ex) { Store.Log("privacy: " + ex.Message); }
        InitBluetoothWatch();
        InitBrightnessWatch();
        Later(8000, delegate { if (mode != Mode.Expanded) Native.TrimMemory(); });
    }

    // ---------------------------------------------------------------- called from the tray menu

    public void OpenFromTray() { OpenPinned(-1); }

#if FRIENDS
    const string PickerHint = " opens this list";

    public void SetClipboardHistory(bool on)
    {
        if (on) Native.AddClipboardFormatListener(hwnd);
        else { Native.RemoveClipboardFormatListener(hwnd); clips.RemoveAll(c => !c.Pinned); RenderClips(); }
    }

    public void SetNotifications(bool on)
    {
        if (!on) { noteTimer.Stop(); return; }
        if (listener == null) InitNotifications(); else noteTimer.Start();
    }

    // Checks whether the Caps Lock light changed, a few times a second, only while this is switched on.
    DispatcherTimer capsTimer;
    bool capsWasOn;

    public void SetCapsLockPopup(bool on)
    {
        if (capsTimer == null)
        {
            capsTimer = MakeTimer(350, delegate
            {
                bool now = (Native.GetKeyState(0x14) & 1) != 0;
                if (now != capsWasOn) { capsWasOn = now; ShowCapsLock(); }
            });
        }
        capsWasOn = (Native.GetKeyState(0x14) & 1) != 0;
        if (on) capsTimer.Start(); else capsTimer.Stop();
    }

    public void SetFrostedGlass(bool on) { if (!on) glassBrush.ImageSource = null; }
#else
    const string PickerHint = " pastes from here";
#endif

    public void ShowWelcome()
    {
        var p = MakePopup("\uE945", UI.Purple, "WinUltra is running", false);
        p.Value.Text = "Hover the line";
        p.Value.Foreground = UI.Dim;
        ShowPopup(p.Root, 380, PopH, 4500, true);
    }

    void Cleanup()
    {
        try
        {
            Native.UnregisterHotKey(hwnd, 1);
            Native.UnregisterHotKey(hwnd, 2);
            if (brightWatcher != null) brightWatcher.Stop();
            foreach (var bw in btWatchers) bw.Stop();
            Native.RemoveClipboardFormatListener(hwnd);
        }
        catch (Exception) { }
    }

    IntPtr WndProc(IntPtr h, int msg, IntPtr w, IntPtr l, ref bool handled)
    {
        if (msg == 0x0312 && w.ToInt32() == 1)    // WM_HOTKEY
        {
            if (mode == Mode.Expanded) Collapse(); else OpenPinned(-1);
            handled = true;
        }
        else if (msg == 0x0312 && w.ToInt32() == 2)    // the clipboard picker hotkey
        {
            pasteTarget = Native.GetForegroundWindow();
            pickerMode = true;
            OpenPinned(TabClipboard);
            handled = true;
        }
#if !FRIENDS
        else if (msg == 0x00FF)    // WM_INPUT: only Caps Lock matters
        {
            uint size = 64;
            if (Native.GetRawInputData(l, 0x10000003, rawBuffer, ref size, 24) != unchecked((uint)-1) && Marshal.ReadInt32(rawBuffer) == 1)
            {
                int flags = Marshal.ReadInt16(rawBuffer, 26), vk = Marshal.ReadInt16(rawBuffer, 30);
                if (vk == 0x14 && (flags & 1) == 1) Later(40, ShowCapsLock);
            }
        }
#endif
        else if (msg == 0x031D)    // WM_CLIPBOARDUPDATE
        {
            clipAttempts = 0;
            clipTimer.Stop();
            clipTimer.Start();
        }
        else if (openMessage != 0 && msg == (int)openMessage)
        {
            OpenPinned(w.ToInt32());
            handled = true;
        }
        return IntPtr.Zero;
    }

    void OpenPinned(int tabIndex)
    {
        forceShowUntil = DateTime.Now.AddSeconds(10);
        if (hidden) { hidden = false; Show(); }
        Expand(true);
        if (tabIndex >= 0 && tabIndex < TabCount) SelectTab(tabIndex);
    }

    // ---------------------------------------------------------------- states and animation

    static DoubleAnimation Anim(double to, double ms, IEasingFunction ease, double delayMs)
    {
        var a = new DoubleAnimation(to, TimeSpan.FromMilliseconds(ms)) { EasingFunction = ease, BeginTime = TimeSpan.FromMilliseconds(delayMs) };
        Timeline.SetDesiredFrameRate(a, Fps);
        return a;
    }

    void AnimateTo(double w, double h, bool bouncy, Action done)
    {
        int gen = ++animGen;
        double ms = bouncy ? 620 : 520;
        IEasingFunction ease = bouncy ? (IEasingFunction)Spring.Bouncy : Spring.Soft;
        var aw = Anim(w, ms, ease, 0);
        var ah = Anim(h, ms, ease, 0);
        ah.Completed += delegate
        {
            if (gen != animGen) return;
            // Hold the final size without keeping the animation clocks alive.
            island.BeginAnimation(WidthProperty, null);
            island.BeginAnimation(HeightProperty, null);
            island.Width = w;
            island.Height = h;
            if (done != null) done();
        };
        island.BeginAnimation(WidthProperty, aw);
        island.BeginAnimation(HeightProperty, ah);
    }

    // Content fades in while growing from 94% to full size, so it seems to bloom out of the island.
    void ShowLayer(Grid layer, int delayMs)
    {
        layer.Visibility = Visibility.Visible;
        layer.IsHitTestVisible = true;
        layer.BeginAnimation(OpacityProperty, Anim(1, 260, new CubicEase { EasingMode = EasingMode.EaseOut }, delayMs));
        var s = (ScaleTransform)layer.RenderTransform;
        s.BeginAnimation(ScaleTransform.ScaleXProperty, Anim(1, 520, Spring.Soft, delayMs * 0.5));
        s.BeginAnimation(ScaleTransform.ScaleYProperty, Anim(1, 520, Spring.Soft, delayMs * 0.5));
        if (layer.Opacity < 0.05) { s.ScaleX = 0.94; s.ScaleY = 0.94; }
    }

    void HideLayer(Grid layer)
    {
        if (layer.Visibility != Visibility.Visible) return;
        layer.IsHitTestVisible = false;
        var a = Anim(0, 120, new CubicEase { EasingMode = EasingMode.EaseIn }, 0);
        a.Completed += delegate
        {
            if (layer.IsHitTestVisible) return;    // shown again meanwhile
            layer.Visibility = Visibility.Collapsed;
            var s = (ScaleTransform)layer.RenderTransform;
            s.BeginAnimation(ScaleTransform.ScaleXProperty, null);
            s.BeginAnimation(ScaleTransform.ScaleYProperty, null);
            s.ScaleX = 0.94;
            s.ScaleY = 0.94;
        };
        layer.BeginAnimation(OpacityProperty, a);
    }

    void FadeTo(UIElement e, double opacity, double ms)
    {
        e.BeginAnimation(OpacityProperty, Anim(opacity, ms, new CubicEase { EasingMode = EasingMode.EaseOut }, 0));
    }

    void Expand(bool pinned)
    {
        pinnedOpen = pinned;
        pinnedAt = DateTime.Now;
        if (mode == Mode.Expanded) return;
        if (!built) BuildExpanded();
        if (mode == Mode.Collapsed) CaptureBackdrop();    // from a pop-up, keep the glass it already has
        Native.SetWindowPos(hwnd, new IntPtr(-1), 0, 0, 0, 0, 0x13);    // back above any other always-on-top window
        popupTimer.Stop();
        mode = Mode.Expanded;
        Resize(WinW, BigWinH);
        hitStrip.Visibility = Visibility.Collapsed;
        HideLayer(collapsedLayer);
        HideLayer(popupLayer);
        StopBars();
        FadeTo(lineGlow, 0, 160);
        FadeTo(rimOverlay, 1, 300);
        FadeTo(glass, 1, 320);
        FadeTo(baseLayer, BaseUnderGlass, 320);
        FadeTo(aurora, 0.6, 500);
        ShowLayer(expandedLayer, 110);
        AnimateTo(ExpW, ExpH, true, null);
        StartLive();
    }

    void MaybeCollapse()
    {
        if (mode != Mode.Expanded || draggingOut || dragInside) return;
        if (pinnedOpen) return;
        if (CursorOverIsland()) return;
        Collapse();
    }

    void Collapse()
    {
        if (mode != Mode.Expanded) return;
        mode = Mode.Collapsed;
        pinnedOpen = false;
        pickerMode = false;
        StopLive();
        HideLayer(expandedLayer);
        ApplyCollapsed(true);
    }

    bool CursorOverIsland()
    {
        Native.POINT p;
        if (!Native.GetCursorPos(out p) || PresentationSource.FromVisual(island) == null) return false;
        var local = island.PointFromScreen(new Point(p.X, p.Y));
        if (local.X >= 0 && local.Y >= -Gap && local.X <= island.ActualWidth && local.Y <= island.ActualHeight) return true;
        if (hitStrip.Visibility != Visibility.Visible) return false;
        local = hitStrip.PointFromScreen(new Point(p.X, p.Y));
        return local.X >= 0 && local.Y >= 0 && local.X <= hitStrip.ActualWidth && local.Y <= hitStrip.ActualHeight;
    }

    void ShowPopup(UIElement content, double w, double h, int ms, bool force)
    {
        if (mode == Mode.Expanded) return;
        if (hidden && !force) return;
        if (force && hidden) { hidden = false; forceShowUntil = DateTime.Now.AddMilliseconds(ms + 500); Show(); }
        if (popupLayer.Children.Count != 1 || popupLayer.Children[0] != content)
        {
            popupLayer.Children.Clear();
            popupLayer.Children.Add(content);
            if (mode == Mode.Popup) { popupLayer.Opacity = 0; ShowLayer(popupLayer, 60); }
        }
        bool fresh = mode != Mode.Popup;
        if (fresh)
        {
            CaptureBackdrop();
            Native.SetWindowPos(hwnd, new IntPtr(-1), 0, 0, 0, 0, 0x13);    // back above any other always-on-top window
        }
        mode = Mode.Popup;
        hitStrip.Visibility = Visibility.Collapsed;
        if (fresh)
        {
            StopBars();
            HideLayer(collapsedLayer);
            FadeTo(lineGlow, 0, 140);
            FadeTo(rimOverlay, 1, 260);
            FadeTo(glass, 1, 260);
            FadeTo(baseLayer, BaseUnderGlass, 260);
            FadeTo(aurora, 0.5, 400);
            ShowLayer(popupLayer, 90);
        }
        if (fresh) Resize(WinW, SmallWinH);
        if (Math.Abs(island.Width - w) > 0.5 || Math.Abs(island.Height - h) > 0.5 || fresh) AnimateTo(w, h, true, null);
        popupTimer.Interval = TimeSpan.FromMilliseconds(ms);
        popupTimer.Stop();
        popupTimer.Start();
    }

    void EndPopup()
    {
        if (mode != Mode.Popup) return;
        mode = Mode.Collapsed;
        HideLayer(popupLayer);
        ApplyCollapsed(true);
    }

    // The resting state: a compact pill while something is live (music, a timer, charging), otherwise the line.
    void ApplyCollapsed(bool animate)
    {
        if (mode != Mode.Collapsed) return;
        bool line = !HasStatus();
        lineMode = line;
        hitStrip.Visibility = line ? Visibility.Visible : Visibility.Collapsed;
        double w = line ? LineW : PillW, h = line ? LineH : PillH;
        FadeTo(glass, 0, 300);    // the resting pill is solid black, like the iPhone's
        FadeTo(baseLayer, 1, 200);
        if (line)
        {
            HideLayer(collapsedLayer);
            StopBars();
            FadeTo(lineGlow, 0.9, 260);
            FadeTo(rimOverlay, 0, 200);
            FadeTo(aurora, 0, 260);
        }
        else
        {
            FadeTo(lineGlow, 0, 160);
            FadeTo(rimOverlay, 1, 300);
            FadeTo(aurora, 0.55, 400);
            ShowLayer(collapsedLayer, 140);
            if (playing && !timerRunning && !stopwatch.IsRunning && !hidden) StartBars();
        }
        Action done = delegate
        {
            // At rest the window hugs the line or pill, so the music bars redraw a small area.
            if (mode == Mode.Collapsed) Resize(line ? 262 : PillW + 84, line ? 30 : PillH + Gap + 14);
            trimTimer.Stop();
            trimTimer.Start();
        };
        if (!animate) { island.Width = w; island.Height = h; lineGlow.Opacity = line ? 0.9 : 0; done(); return; }
        Resize(Math.Max(Width, w + 30), Math.Max(Height, h + Gap + 12));    // room for the spring's overshoot
        if (Math.Abs(island.ActualWidth - w) > 0.5 || Math.Abs(island.ActualHeight - h) > 0.5) AnimateTo(w, h, false, done);
        else done();
    }

    // ---------------------------------------------------------------- full-screen check and the slow poll

    readonly StringBuilder className = new StringBuilder(64);

    void Poll()
    {
        bool full = DateTime.Now >= forceShowUntil && FullScreenInFront();
        if (full && !hidden)
        {
            hidden = true;
            if (mode == Mode.Expanded) Collapse();
            StopBars();
            Hide();
        }
        else if (!full && hidden)
        {
            hidden = false;
            Show();
            Native.SetWindowPos(hwnd, new IntPtr(-1), 0, 0, 0, 0, 0x13);    // HWND_TOPMOST, no move/size/activate
            if (mode == Mode.Collapsed && playing && !lineMode && !timerRunning && !stopwatch.IsRunning) StartBars();
        }

        if (pinnedOpen && mode == Mode.Expanded && (DateTime.Now - pinnedAt).TotalSeconds > 12 && !CursorOverIsland())
            Collapse();
        if (mode == Mode.Expanded && !pinnedOpen && !draggingOut && !dragInside && !CursorOverIsland())
            Collapse();
    }

    bool FullScreenInFront()
    {
        IntPtr fg = Native.GetForegroundWindow();
        if (fg == IntPtr.Zero || fg == hwnd) return false;
        className.Length = 0;
        Native.GetClassName(fg, className, className.Capacity);
        string cls = className.ToString();
        if (cls == "Progman" || cls == "WorkerW" || cls == "Shell_TrayWnd" || cls == "Shell_SecondaryTrayWnd" || cls == "XamlExplorerHostIslandWindow") return false;
        if (cls == "SequoiaMesa") return false;    // the WacOS desktop: a window the size of the screen, not a full-screen app

        int state;
        if (Native.SHQueryUserNotificationState(out state) == 0 && (state == 3 || state == 4)) return true;    // D3D full screen, presentation mode

        Native.RECT r;
        // A maximised window with an auto-hide taskbar also covers the screen, but it is not full screen.
        if (Native.IsZoomed(fg) || !Native.GetWindowRect(fg, out r)) return false;
        var mi = new Native.MONITORINFO { cbSize = Marshal.SizeOf(typeof(Native.MONITORINFO)) };
        if (!Native.GetMonitorInfo(Native.MonitorFromWindow(fg, 2), ref mi) || (mi.dwFlags & 1) == 0) return false;
        return r.Left <= mi.rcMonitor.Left && r.Top <= mi.rcMonitor.Top && r.Right >= mi.rcMonitor.Right && r.Bottom >= mi.rcMonitor.Bottom;
    }

    // ---------------------------------------------------------------- collapsed pill

    void BuildCollapsed()
    {
        var g = UI.Columns(UI.Auto, UI.Star, UI.Auto);
        g.Margin = new Thickness(7, 0, 14, 0);
        var left = new Grid { Width = 24, Height = 24, VerticalAlignment = VerticalAlignment.Center };
        left.Children.Add(pillBadge);
        left.Children.Add(pillArt);
        left.Children.Add(pillIcon);
        g.Children.Add(UI.At(left, 0));
        var right = new Grid { VerticalAlignment = VerticalAlignment.Center };
        right.Children.Add(pillText);
        right.Children.Add(pillBars);
        g.Children.Add(UI.At(right, 2));
        pillText.FontFeatures();
        double[] speeds = { 0.42, 0.33, 0.51, 0.38 };
        foreach (double s in speeds)
        {
            var bar = new Border { Width = 3, Height = 14, Margin = new Thickness(1.5, 0, 1.5, 0), CornerRadius = new CornerRadius(1.5), Background = UI.Green, RenderTransformOrigin = new Point(0.5, 0.5), RenderTransform = new ScaleTransform(1, 0.4), Tag = s };
            pillBars.Children.Add(bar);
        }
        collapsedLayer.Children.Add(g);
        UpdateCollapsed();
    }

    bool HasStatus() { return timerRunning || stopwatch.IsRunning || playing || (onAc && batteryPercent >= 0 && batteryPercent < 100 && charging); }

    bool charging;

    // Decides what the compact pill shows: a running timer, the stopwatch, music, or charging.
    void UpdateCollapsed()
    {
        bool wasLine = lineMode;
        pillArt.Visibility = Visibility.Collapsed;
        pillIcon.Visibility = Visibility.Collapsed;
        pillText.Visibility = Visibility.Collapsed;
        pillBars.Visibility = Visibility.Collapsed;

        if (timerRunning)
        {
            var tone = focusMode ? UI.Purple : UI.Orange;
            pillIcon.Text = focusMode ? "\uE708" : ClockGlyph; pillIcon.Foreground = tone; pillIcon.Visibility = Visibility.Visible;
            pillText.Text = Format(TimerLeft()); pillText.Foreground = tone; pillText.Visibility = Visibility.Visible;
        }
        else if (stopwatch.IsRunning)
        {
            pillIcon.Text = ClockGlyph; pillIcon.Foreground = UI.Fg; pillIcon.Visibility = Visibility.Visible;
            pillText.Text = Format(stopwatch.Elapsed); pillText.Foreground = UI.Fg; pillText.Visibility = Visibility.Visible;
        }
        else if (playing)
        {
            if (artwork != null) { pillArt.Background = new ImageBrush(artwork) { Stretch = Stretch.UniformToFill }; pillArt.Visibility = Visibility.Visible; }
            else { pillIcon.Text = ""; pillIcon.Foreground = UI.Fg; pillIcon.Visibility = Visibility.Visible; }
            pillBars.Visibility = Visibility.Visible;
        }
        else if (HasStatus())
        {
            pillIcon.Text = ""; pillIcon.Foreground = UI.Green; pillIcon.Visibility = Visibility.Visible;
            pillText.Text = batteryPercent + "%"; pillText.Foreground = UI.Green; pillText.Visibility = Visibility.Visible;
        }

        pillBadge.Visibility = pillIcon.Visibility;
        if (!playing || timerRunning || stopwatch.IsRunning) StopBars();
        if (mode == Mode.Collapsed && (wasLine != !HasStatus() || (playing && !barsRunning && !timerRunning && !stopwatch.IsRunning))) ApplyCollapsed(true);
    }

    void StartBars()
    {
        if (barsRunning) return;
        barsRunning = true;
        foreach (Border bar in pillBars.Children)
        {
            var a = new DoubleAnimation(0.25, 1, TimeSpan.FromSeconds((double)bar.Tag)) { AutoReverse = true, RepeatBehavior = RepeatBehavior.Forever, EasingFunction = new SineEase() };
            Timeline.SetDesiredFrameRate(a, 15);
            ((ScaleTransform)bar.RenderTransform).BeginAnimation(ScaleTransform.ScaleYProperty, a);
        }
    }

    void StopBars()
    {
        if (!barsRunning) return;
        barsRunning = false;
        foreach (Border bar in pillBars.Children) ((ScaleTransform)bar.RenderTransform).BeginAnimation(ScaleTransform.ScaleYProperty, null);
    }

    static string Format(TimeSpan t)
    {
        if (t < TimeSpan.Zero) t = TimeSpan.Zero;
        return t.TotalHours >= 1 ? string.Format("{0}:{1:00}:{2:00}", (int)t.TotalHours, t.Minutes, t.Seconds) : string.Format("{0}:{1:00}", t.Minutes, t.Seconds);
    }

    // ---------------------------------------------------------------- pop-ups

    Popup MakePopup(string glyph, Brush color, string text, bool withBar)
    {
        var p = new Popup();
        var g = UI.Columns(UI.Auto, UI.Auto, UI.Star, UI.Auto);
        g.Margin = new Thickness(16, 0, 18, 0);
        p.Glyph = UI.Icon(glyph, 17, color);
        g.Children.Add(UI.At(p.Glyph, 0));
        if (text.Length > 0)
        {
            var label = UI.Text(text, 14, UI.Fg, FontWeights.SemiBold);
            label.Margin = new Thickness(10, 0, 12, 0);
            label.MaxWidth = 200;
            g.Children.Add(UI.At(label, 1));
        }
        if (withBar)
        {
            // The bar fills the space between the icon and the number; the fill is scaled, so it
            // is right however wide that space turns out.
            var track = new Grid { Height = 6, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(text.Length > 0 ? 0 : 14, 0, 14, 0) };
            track.Children.Add(new Border { Background = UI.B("#33FFFFFF"), CornerRadius = new CornerRadius(3) });
            p.Fill = new Border { Background = color, CornerRadius = new CornerRadius(3), RenderTransformOrigin = new Point(0, 0.5), RenderTransform = new ScaleTransform(0, 1) };
            track.Children.Add(p.Fill);
            g.Children.Add(UI.At(track, 2));
        }
        p.Value = UI.Text("", 14, color, FontWeights.SemiBold);
        g.Children.Add(UI.At(p.Value, 3));
        p.Root = g;
        return p;
    }

    void OnVolumeChanged(float level, bool muted, bool external)
    {
        if (mode == Mode.Expanded)
        {
            if (built && !volSlider.Dragging) volSlider.Value = level;
            if (built) SetVolumeGlyph(level, muted);
            return;
        }
        if (!external) return;
        if (volumePopupCache == null) volumePopupCache = MakePopup("", UI.Fg, "", true);
        var p = volumePopupCache;
        p.Glyph.Text = muted ? "" : "";
        p.Value.Text = muted ? "Muted" : ((int)Math.Round(level * 100)).ToString();
        p.Fill.RenderTransform.BeginAnimation(ScaleTransform.ScaleXProperty, Anim(muted ? 0 : level, 260, Spring.Soft, 0));
        p.Fill.Opacity = muted ? 0.4 : 1;
        ShowPopup(p.Root, PopW, PopH, 1800, false);
    }

    void InitPower()
    {
        ReadPower();
        wasOnAc = onAc;
        try
        {
            Windows.Devices.Power.Battery.AggregateBattery.ReportUpdated += (s, a) => Ui(OnPowerChanged);
        }
        catch (Exception) { }
        SystemEvents.PowerModeChanged += (s, a) => { if (a.Mode == PowerModes.StatusChange) Ui(OnPowerChanged); };
        UpdateCollapsed();
        Later(60000, PowerPoll);
    }

    void PowerPoll()
    {
        OnPowerChanged();
        Later(60000, PowerPoll);
    }

    void ReadPower()
    {
        Native.SYSTEM_POWER_STATUS s;
        if (!Native.GetSystemPowerStatus(out s)) return;
        onAc = s.ACLineStatus == 1;
        charging = (s.BatteryFlag & 8) != 0;
        batteryPercent = s.BatteryLifePercent == 255 || (s.BatteryFlag & 128) != 0 ? -1 : s.BatteryLifePercent;
    }

    void OnPowerChanged()
    {
        int before = batteryPercent;
        ReadPower();
        if (built) UpdateBattery();
        if (onAc != wasOnAc)
        {
            wasOnAc = onAc;
            var p = onAc ? MakePopup("", UI.Green, "Charger connected", false) : MakePopup("", UI.Fg, "On battery", false);
            p.Value.Text = batteryPercent >= 0 ? batteryPercent + "%" : "";
            ShowPopup(p.Root, PopW, PopH, 2400, false);
            lowWarned = 101;
        }
        else if (!onAc && batteryPercent >= 0 && before != batteryPercent && (batteryPercent <= 20 && lowWarned > 20 || batteryPercent <= 10 && lowWarned > 10))
        {
            lowWarned = batteryPercent;
            var p = MakePopup("", UI.Red, "Battery low", false);
            p.Value.Text = batteryPercent + "%";
            ShowPopup(p.Root, PopW, PopH, 3000, false);
        }
        UpdateCollapsed();
    }

    // ---------------------------------------------------------------- camera/mic dot, Bluetooth, brightness, Caps Lock

    void UpdatePrivacy()
    {
        var cam = PrivacyWatch.InUse("webcam").Concat(PrivacyWatch.LiveCamera()).Distinct().ToList();
        var mic = PrivacyWatch.InUse("microphone").Concat(PrivacyWatch.LiveMicrophone()).Distinct().ToList();
        string started = cam.FirstOrDefault(a => !camApps.Contains(a));
        bool camStarted = started != null;
        if (started == null) started = mic.FirstOrDefault(a => !micApps.Contains(a));
        camApps = cam;
        micApps = mic;
        bool any = cam.Count > 0 || mic.Count > 0;
        // Green for the camera, orange for the microphone alone, as on the iPhone.
        privacyDot.Fill = cam.Count > 0 ? UI.Green : UI.Orange;
        privacyGlow.Color = cam.Count > 0 ? Color.FromRgb(0x30, 0xD1, 0x58) : Color.FromRgb(0xFF, 0x9F, 0x0A);
        privacyDot.Visibility = any ? Visibility.Visible : Visibility.Collapsed;
        if (any && !privacyPulsing)
        {
            var pulse = new DoubleAnimation(1, 0.45, TimeSpan.FromSeconds(0.9)) { AutoReverse = true, RepeatBehavior = RepeatBehavior.Forever, EasingFunction = new SineEase() };
            Timeline.SetDesiredFrameRate(pulse, 20);
            privacyDot.BeginAnimation(UIElement.OpacityProperty, pulse);
            privacyPulsing = true;
        }
        else if (!any && privacyPulsing)
        {
            privacyDot.BeginAnimation(UIElement.OpacityProperty, null);    // stops the pulse, so nothing redraws while idle
            privacyPulsing = false;
        }
        if (privacyLabel != null)
        {
            privacyLabel.Text = any ? "● " + string.Join(", ", cam.Concat(mic).Distinct()) : "";
            privacyLabel.Foreground = cam.Count > 0 ? UI.Green : UI.Orange;
        }
        if (started != null)
        {
            var p = camStarted ? MakePopup("", UI.Green, "Camera in use", false) : MakePopup("", UI.Orange, "Microphone in use", false);
            p.Value.Text = started;
            ShowPopup(p.Root, PopW, PopH, 2600, false);
        }
    }

    void InitBluetoothWatch()
    {
        try
        {
            var selectors = new[]
            {
                Windows.Devices.Bluetooth.BluetoothDevice.GetDeviceSelectorFromConnectionStatus(Windows.Devices.Bluetooth.BluetoothConnectionStatus.Connected),
                Windows.Devices.Bluetooth.BluetoothLEDevice.GetDeviceSelectorFromConnectionStatus(Windows.Devices.Bluetooth.BluetoothConnectionStatus.Connected),
            };
            foreach (var selector in selectors)
            {
                var watcher = Windows.Devices.Enumeration.DeviceInformation.CreateWatcher(selector, new[] { "System.Devices.Aep.ContainerId" }, Windows.Devices.Enumeration.DeviceInformationKind.AssociationEndpoint);
                watcher.Added += (s, info) =>
                {
                    string id = info.Id, name = info.Name;
                    object container;
                    info.Properties.TryGetValue("System.Devices.Aep.ContainerId", out container);
                    Ui(delegate { OnBluetoothAdded(id, name, container); });
                };
                watcher.Removed += (s, update) => { string id = update.Id; Ui(delegate { OnBluetoothRemoved(id); }); };
                watcher.Updated += (s, update) => { };
                watcher.EnumerationCompleted += (s, o) => Ui(delegate { btCompleted++; });    // devices already connected at start-up don't pop
                btWatchers.Add(watcher);
                watcher.Start();
            }
        }
        catch (Exception ex) { Store.Log("bluetooth watch: " + ex.Message); }
    }

    bool BluetoothReady { get { return btWatchers.Count > 0 && btCompleted >= btWatchers.Count; } }

    async void OnBluetoothAdded(string id, string name, object container)
    {
        bool known = btNames.ContainsKey(id);
        btNames[id] = name;
        if (!BluetoothReady || known || string.IsNullOrEmpty(name)) return;
        if (name == lastBtName && (DateTime.Now - lastBtAt).TotalSeconds < 5) return;    // dual-mode devices arrive twice
        lastBtName = name;
        lastBtAt = DateTime.Now;
        string battery = "";
        try
        {
            if (container is Guid)
            {
                var found = await Windows.Devices.Enumeration.DeviceInformation.FindAllAsync("System.Devices.ContainerId:=\"{" + container + "}\"", new[] { BatteryKey }, Windows.Devices.Enumeration.DeviceInformationKind.Device);
                foreach (var d in found)
                {
                    object v;
                    if (d.Properties.TryGetValue(BatteryKey, out v) && v is byte) { battery = (byte)v + "%"; break; }
                }
            }
        }
        catch (Exception) { }
        var p = MakePopup("", UI.Blue, name, false);
        p.Value.Text = battery.Length > 0 ? battery : "Connected";
        ShowPopup(p.Root, PopW, PopH, 2600, false);
    }

    void OnBluetoothRemoved(string id)
    {
        string name;
        if (!btNames.TryGetValue(id, out name)) return;
        btNames.Remove(id);
        if (!BluetoothReady || string.IsNullOrEmpty(name)) return;
        if (name == lastBtName && (DateTime.Now - lastBtAt).TotalSeconds < 2) return;
        lastBtName = name;
        lastBtAt = DateTime.Now;
        var p = MakePopup("", UI.Dim, name, false);
        p.Value.Text = "Disconnected";
        p.Value.Foreground = UI.Dim;
        ShowPopup(p.Root, PopW, PopH, 2200, false);
    }

    // The brightness keys raise a WMI event from the display driver; nothing is polled.
    void InitBrightnessWatch()
    {
        try
        {
            brightWatcher = new ManagementEventWatcher(new ManagementScope(@"root\WMI"), new EventQuery("SELECT * FROM WmiMonitorBrightnessEvent"));
            brightWatcher.EventArrived += (s, e) =>
            {
                int level = Convert.ToInt32(e.NewEvent["Brightness"]);
                Ui(delegate { OnBrightnessChanged(level); });
            };
            brightWatcher.Start();
        }
        catch (Exception ex) { brightWatcher = null; Store.Log("brightness watch: " + ex.Message); }
    }

    void OnBrightnessChanged(int level)
    {
        if (mode == Mode.Expanded)
        {
            if (built && !brightSlider.Dragging) brightSlider.Value = level / 100.0;
            return;
        }
        if (DateTime.Now < ownBrightnessUntil) return;
        if (brightPopupCache == null) brightPopupCache = MakePopup("", UI.Fg, "", true);
        var p = brightPopupCache;
        p.Value.Text = level.ToString();
        p.Fill.RenderTransform.BeginAnimation(ScaleTransform.ScaleXProperty, Anim(level / 100.0, 260, Spring.Soft, 0));
        ShowPopup(p.Root, PopW, PopH, 1800, false);
    }

    void ShowCapsLock()
    {
        bool on = (Native.GetKeyState(0x14) & 1) != 0;
        var p = MakePopup("", on ? UI.Orange : UI.Dim, "Caps Lock", false);
        p.Value.Text = on ? "On" : "Off";
        p.Value.Foreground = on ? UI.Orange : UI.Dim;
        ShowPopup(p.Root, 250, PopH, 1400, false);
    }

    // ---------------------------------------------------------------- notifications

    async void InitNotifications()
    {
        try
        {
            listener = Windows.UI.Notifications.Management.UserNotificationListener.Current;
            if (await listener.RequestAccessAsync() != Windows.UI.Notifications.Management.UserNotificationListenerAccessStatus.Allowed) { listener = null; return; }
            var list = await listener.GetNotificationsAsync(Windows.UI.Notifications.NotificationKinds.Toast);
            seenNotes = new HashSet<uint>(list.Select(n => n.Id));
            // Windows offers no change event to unpackaged apps, so the island asks for the list.
            // Each ask costs about 35 ms of CPU, so it asks every 3 s while you are using the PC,
            // every 20 s once you have been idle for 2 minutes, and not at all behind a full-screen app.
            noteTimer.Start();
        }
        catch (Exception ex) { listener = null; Store.Log("notifications: " + ex.Message); }
    }

    async void PollNotifications()
    {
        var input = new Native.LASTINPUTINFO { cbSize = (uint)Marshal.SizeOf(typeof(Native.LASTINPUTINFO)) };
        bool idle = Native.GetLastInputInfo(ref input) && (uint)Environment.TickCount - input.dwTime > 120000;
        var interval = TimeSpan.FromMilliseconds(idle ? 20000 : 3000);
        if (noteTimer.Interval != interval) noteTimer.Interval = interval;
        if (listener == null || notePolling || hidden) return;
#if FRIENDS
        if (!WinUltra.App.Options.Notifications) return;
#endif
        notePolling = true;
        try
        {
            var list = await listener.GetNotificationsAsync(Windows.UI.Notifications.NotificationKinds.Toast);
            var ids = new HashSet<uint>();
            Windows.UI.Notifications.UserNotification newest = null;
            foreach (var n in list)
            {
                ids.Add(n.Id);
                if (!seenNotes.Contains(n.Id) && (newest == null || n.CreationTime > newest.CreationTime)) newest = n;
            }
            seenNotes = ids;
            int state;
            bool quiet = Native.SHQueryUserNotificationState(out state) == 0 && state != 5 && state != 7;    // busy, full screen or quiet time
            if (newest != null && !quiet && !hidden && !focusMode) await ShowNotification(newest);    // Focus holds them back
        }
        catch (Exception ex) { Store.Log("notification poll: " + ex.Message); }
        notePolling = false;
    }

    async Task ShowNotification(Windows.UI.Notifications.UserNotification n)
    {
        string app = "", head = "", body = "";
        ImageSource logo = null;
        try { app = n.AppInfo.DisplayInfo.DisplayName; } catch (Exception) { }
        try
        {
            var binding = n.Notification.Visual.GetBinding(Windows.UI.Notifications.KnownNotificationBindings.ToastGeneric);
            if (binding != null)
            {
                var texts = binding.GetTextElements().Select(t => t.Text).Where(t => !string.IsNullOrWhiteSpace(t)).ToList();
                if (texts.Count > 0) head = texts[0];
                if (texts.Count > 1) body = string.Join("  ", texts.Skip(1));
            }
        }
        catch (Exception) { }
        try
        {
            var reference = n.AppInfo.DisplayInfo.GetLogo(new Windows.Foundation.Size(48, 48));
            var ms = new MemoryStream();
            using (var ras = await reference.OpenReadAsync())
            using (var st = ras.AsStreamForRead())
                st.CopyTo(ms);
            ms.Position = 0;
            var bi = new BitmapImage();
            bi.BeginInit();
            bi.CacheOption = BitmapCacheOption.OnLoad;
            bi.DecodePixelWidth = 64;
            bi.StreamSource = ms;
            bi.EndInit();
            bi.Freeze();
            logo = bi;
        }
        catch (Exception) { }
        if (string.IsNullOrEmpty(head)) head = "New notification";

        var g = UI.Columns(UI.Px(38), UI.Star);
        g.Margin = new Thickness(14, 0, 22, 0);
        var icon = new Border { Width = 34, Height = 34, CornerRadius = new CornerRadius(10), VerticalAlignment = VerticalAlignment.Center, Background = UI.Panel_ };
        if (logo != null) icon.Child = new Image { Source = logo, Width = 26, Height = 26 };
        else icon.Child = UI.Icon("", 16, UI.Fg);
        g.Children.Add(UI.At(icon, 0));
        var s = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(10, 0, 0, 0) };
        s.Children.Add(UI.Text(app, 11, UI.Dim, FontWeights.SemiBold));
        s.Children.Add(UI.Text(head, 13.5, UI.Fg, FontWeights.SemiBold));
        if (body.Length > 0) s.Children.Add(UI.Text(body.Replace('\n', ' '), 12, UI.Dim, FontWeights.Normal));
        g.Children.Add(UI.At(s, 1));
        ShowPopup(g, NoteW, NoteH, 4200, false);
    }

    // ---------------------------------------------------------------- expanded panel

    void BuildExpanded()
    {
        built = true;
        expandedLayer.Width = ExpW;
        expandedLayer.Height = ExpH;
        expandedLayer.HorizontalAlignment = HorizontalAlignment.Center;
        expandedLayer.VerticalAlignment = VerticalAlignment.Top;

        var panel = new Grid { Margin = new Thickness(16, 12, 16, 14) };
        panel.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        panel.RowDefinitions.Add(new RowDefinition { Height = UI.Star });

        // Tab bar: icons, with the name shown only on the open tab.
        var bar = UI.Columns(UI.Star, UI.Auto);
        var tabs = new StackPanel { Orientation = Orientation.Horizontal };
        string[] names = { "Media", "Controls", "Stats", "Clipboard", "Shelf", "Timer" };
        string[] glyphs = { "", "", "", "", "", ClockGlyph };
        for (int i = 0; i < TabCount; i++)
        {
            int index = i;
            var content = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(11, 0, 11, 0), VerticalAlignment = VerticalAlignment.Center };
            content.Children.Add(UI.Icon(glyphs[i], 13, UI.Fg));
            tabLabels[i] = UI.Text(names[i], 12.5, UI.Fg, FontWeights.SemiBold);
            tabLabels[i].Margin = new Thickness(7, 0, 0, 0);
            content.Children.Add(tabLabels[i]);
            tabButtons[i] = UI.Button(content, 0, 28, 14, Brushes.Transparent, delegate { SelectTab(index); });
            tabButtons[i].Margin = new Thickness(0, 0, 2, 0);
            // Resting the mouse on a tab for a moment opens it; no click needed.
            tabButtons[i].MouseEnter += delegate
            {
                tabHoverTimer.Stop();
                if (index == tab) return;
                tabHoverTarget = index;
                tabHoverTimer.Start();
            };
            tabButtons[i].MouseLeave += delegate { if (tabHoverTarget == index) tabHoverTimer.Stop(); };
            tabs.Children.Add(tabButtons[i]);
        }
        bar.Children.Add(UI.At(tabs, 0));
        privacyLabel = UI.Text("", 12, UI.Orange, FontWeights.SemiBold);
        privacyLabel.Margin = new Thickness(0, 0, 12, 0);
        privacyLabel.HorizontalAlignment = HorizontalAlignment.Right;
        privacyLabel.MaxWidth = 150;
        bar.Children.Add(UI.At(privacyLabel, 0));
        batteryLabel = UI.Text("", 12.5, UI.Dim, FontWeights.SemiBold);
        batteryLabel.Margin = new Thickness(0, 0, 4, 0);
        bar.Children.Add(UI.At(batteryLabel, 1));
        panel.Children.Add(UI.Row(bar, 0));

        var host = new Grid { Margin = new Thickness(0, 10, 0, 0) };
        pages[TabMedia] = BuildMedia();
        pages[TabControls] = BuildControls();
        pages[TabStats] = BuildStats();
        pages[TabClipboard] = BuildClipboard();
        pages[TabShelf] = BuildShelf();
        pages[TabTimer] = BuildTimer();
        foreach (var p in pages)
        {
            p.RenderTransform = new TranslateTransform();
            host.Children.Add(p);
        }
        panel.Children.Add(UI.Row(host, 1));
        expandedLayer.Children.Add(panel);
        SelectTab(playing ? TabMedia : TabControls);
        UpdateMediaUi();
    }

    void SelectTab(int index)
    {
        if (!built) BuildExpanded();
        int from = tab;
        tab = index;
        for (int i = 0; i < TabCount; i++)
        {
            tabButtons[i].Background = i == index ? UI.GlassFill(Color.FromRgb(0x8A, 0x8A, 0x9A), 0x40) : Brushes.Transparent;
            tabButtons[i].BorderBrush = i == index ? UI.Specular : null;
            tabButtons[i].BorderThickness = new Thickness(i == index ? 1 : 0);
            tabLabels[i].Visibility = i == index ? Visibility.Visible : Visibility.Collapsed;
            pages[i].Visibility = i == index ? Visibility.Visible : Visibility.Collapsed;
        }
        if (from != index)
        {
            // Slide in a little from the side the tab sits on, with a quick fade.
            var page = pages[index];
            page.Opacity = 0;
            page.BeginAnimation(OpacityProperty, Anim(1, 200, new CubicEase { EasingMode = EasingMode.EaseOut }, 0));
            var t = (TranslateTransform)page.RenderTransform;
            t.X = index > from ? 14 : -14;
            t.BeginAnimation(TranslateTransform.XProperty, Anim(0, 460, Spring.Soft, 0));
        }
        if (index == TabClipboard) RenderClips();
        if (index == TabShelf) RenderShelf();
        if (index == TabTimer) RenderTimer();
        if (index == TabMedia) UpdateProgress();
        UpdateSensors();
        UpdateTickRate();
    }

    // ---------------------------------------------------------------- Media

    Grid BuildMedia()
    {
        var page = UI.Columns(UI.Px(150), UI.Px(18), UI.Star);
        artBig = new Border { Width = 150, Height = 150, CornerRadius = new CornerRadius(20), VerticalAlignment = VerticalAlignment.Center, BorderThickness = new Thickness(1), BorderBrush = UI.B("#22FFFFFF") };
        RenderOptions.SetBitmapScalingMode(artBig, BitmapScalingMode.HighQuality);
        page.Children.Add(UI.At(artBig, 0));

        var info = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        mediaApp = UI.Text("", 11.5, UI.Dim, FontWeights.SemiBold);
        mediaTitle = UI.Text("Nothing playing", 18, UI.Fg, FontWeights.SemiBold);
        mediaTitle.Margin = new Thickness(0, 2, 0, 0);
        mediaArtist = UI.Text("", 13.5, UI.Dim, FontWeights.Normal);
        info.Children.Add(mediaApp);
        info.Children.Add(mediaTitle);
        info.Children.Add(mediaArtist);

        progressRow = new Grid { Margin = new Thickness(0, 14, 0, 0) };
        progressRow.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        progressRow.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        var track = new Grid { Height = 5 };
        track.Children.Add(new Border { Background = UI.B("#2EFFFFFF"), CornerRadius = new CornerRadius(2.5) });
        progressFill = new Border { Background = UI.Fg, CornerRadius = new CornerRadius(2.5), HorizontalAlignment = HorizontalAlignment.Left, Width = 0 };
        track.Children.Add(progressFill);
        progressRow.Children.Add(UI.Row(track, 0));
        var times = UI.Columns(UI.Star, UI.Auto);
        times.Margin = new Thickness(0, 5, 0, 0);
        posText = UI.Text("", 11, UI.Dim, FontWeights.Normal);
        durText = UI.Text("", 11, UI.Dim, FontWeights.Normal);
        times.Children.Add(UI.At(posText, 0));
        times.Children.Add(UI.At(durText, 1));
        progressRow.Children.Add(UI.Row(times, 1));
        info.Children.Add(progressRow);

        var controls = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 8, 0, 0) };
        controls.Children.Add(UI.Button(UI.Icon("", 20, UI.Fg), 52, 42, 21, null, delegate { MediaCommand(0); }));
        playGlyph = UI.Icon("", 26, UI.Fg);
        controls.Children.Add(UI.Button(playGlyph, 62, 42, 21, null, delegate { MediaCommand(1); }));
        controls.Children.Add(UI.Button(UI.Icon("", 20, UI.Fg), 52, 42, 21, null, delegate { MediaCommand(2); }));
        info.Children.Add(controls);
        page.Children.Add(UI.At(info, 2));
        return page;
    }

    void UpdateMediaUi()
    {
        if (!built) return;
        mediaApp.Text = appName ?? "";
        mediaTitle.Text = title ?? "Nothing playing";
        mediaArtist.Text = artist ?? "";
        playGlyph.Text = playing ? "" : "";
        if (artwork != null) { artBig.Background = new ImageBrush(artwork) { Stretch = Stretch.UniformToFill }; artBig.Child = null; }
        else { artBig.Background = UI.Panel_; artBig.Child = UI.Icon("", 40, UI.Dim); }
        UpdateProgress();
    }

    void UpdateProgress()
    {
        if (!built) return;
        var s = session;
        TimeSpan pos = TimeSpan.Zero, len = TimeSpan.Zero;
        try
        {
            if (s != null && title != null)
            {
                var tl = s.GetTimelineProperties();
                len = tl.EndTime - tl.StartTime;
                pos = tl.Position - tl.StartTime;
                if (playing) pos += DateTimeOffset.Now - tl.LastUpdatedTime;
            }
        }
        catch (Exception) { }
        if (len.TotalSeconds < 1) { progressRow.Visibility = Visibility.Collapsed; return; }
        if (pos > len) pos = len;
        if (pos < TimeSpan.Zero) pos = TimeSpan.Zero;
        progressRow.Visibility = Visibility.Visible;
        double w = Math.Max(0, ((FrameworkElement)progressFill.Parent).ActualWidth) * pos.TotalSeconds / len.TotalSeconds;
        progressFill.BeginAnimation(WidthProperty, Anim(w, 900, null, 0));
        posText.Text = Format(pos);
        durText.Text = "-" + Format(len - pos);
    }

    // ---------------------------------------------------------------- Controls

    Grid BuildControls()
    {
        // iOS Control Center: a 2x2 grid of toggles on the left, two tall sliders on the right.
        var page = UI.Columns(UI.Star, UI.Px(12), UI.Px(72), UI.Px(10), UI.Px(72));
        var grid = UI.Columns(UI.Star, UI.Px(10), UI.Star);
        grid.RowDefinitions.Add(new RowDefinition { Height = UI.Star });
        grid.RowDefinitions.Add(new RowDefinition { Height = UI.Px(10) });
        grid.RowDefinitions.Add(new RowDefinition { Height = UI.Star });
        wifiTile = Tile("\uE701", "Wi-Fi", delegate { ToggleRadio(wifi); });
        btTile = Tile("\uE702", "Bluetooth", delegate { ToggleRadio(bluetooth); });
        nightTile = Tile("\uE708", "Night light", ToggleNightLight);
        muteTile = Tile("\uE74F", "Mute", delegate { if (volume.Available) { volume.Muted = !volume.Muted; SetVolumeGlyph(volume.Level, volume.Muted); } });
        grid.Children.Add(UI.Row(UI.At(wifiTile, 0), 0));
        grid.Children.Add(UI.Row(UI.At(btTile, 2), 0));
        grid.Children.Add(UI.Row(UI.At(nightTile, 0), 2));
        grid.Children.Add(UI.Row(UI.At(muteTile, 2), 2));
        page.Children.Add(UI.At(grid, 0));

        brightSlider = new VerticalBarSlider("\uE706", null);
        brightSlider.Changed += v => { ownBrightnessUntil = DateTime.Now.AddSeconds(1.5); Brightness.SetLater((int)Math.Round(v * 100)); };
        page.Children.Add(UI.At(brightSlider, 2));
        volSlider = new VerticalBarSlider("\uE767", delegate { if (volume.Available) { volume.Muted = !volume.Muted; SetVolumeGlyph(volume.Level, volume.Muted); } });
        volSlider.Changed += v => { if (volume.Available) { volume.Level = (float)v; SetVolumeGlyph((float)v, volume.Muted); } };
        page.Children.Add(UI.At(volSlider, 4));
        return page;
    }

    // A glass tile: a round icon badge that lights up in colour when on, the name, and "On"/"Off".
    Border Tile(string glyph, string label, Action click)
    {
        var g = new Grid();
        g.RowDefinitions.Add(new RowDefinition { Height = UI.Star });
        g.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        var badge = new Border { Width = 36, Height = 36, CornerRadius = new CornerRadius(18), Background = UI.B("#2EFFFFFF"), HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top, Child = UI.Icon(glyph, 15, UI.Fg) };
        g.Children.Add(UI.Row(badge, 0));
        var words = new StackPanel();
        words.Children.Add(UI.Text(label, 12.8, UI.Fg, FontWeights.SemiBold));
        var state = UI.Text("Off", 11.2, UI.Dim, FontWeights.Normal);
        words.Children.Add(state);
        g.Children.Add(UI.Row(words, 1));
        var b = UI.Button(g, 0, 0, 22, UI.Card, click);
        b.Padding = new Thickness(12, 11, 12, 10);
        b.BorderThickness = new Thickness(1);
        b.BorderBrush = UI.Rim;
        b.Tag = Tuple.Create(badge, state);
        return b;
    }

    void SetTile(Border tile, int state, Brush onColor)
    {
        var parts = (Tuple<Border, TextBlock>)tile.Tag;
        parts.Item1.Background = state == 1 ? onColor : UI.B("#2EFFFFFF");
        parts.Item2.Text = state < 0 ? "Not available" : state == 1 ? "On" : "Off";
        tile.Background = state == 1 ? UI.B("#24FFFFFF") : UI.Card;
        tile.Opacity = state < 0 ? 0.45 : 1;
    }

    void SetVolumeGlyph(float level, bool muted)
    {
        if (muteTile != null) SetTile(muteTile, muted ? 1 : 0, UI.Red);
        volSlider.Glyph.Text = muted ? "" : level < 0.01 ? "" : level < 0.34 ? "" : level < 0.67 ? "" : "";
    }

    // ---------------------------------------------------------------- Stats

    Grid BuildStats()
    {
        var page = UI.Columns(UI.Star, UI.Px(8), UI.Star, UI.Px(8), UI.Star, UI.Px(8), UI.Star);
        cpuGraph = new Graph(Color.FromRgb(0x6F, 0x8C, 0xFF));
        gpuGraph = new Graph(Color.FromRgb(0xB9, 0x7C, 0xFF));
        netGraph = new Graph(Color.FromRgb(0x5E, 0xD0, 0xE0)) { AutoScale = true };
        cpuValue = UI.Text("–", 20, UI.Fg, FontWeights.Light);
        gpuValue = UI.Text("–", 20, UI.Fg, FontWeights.Light);
        netDown = UI.Text("–", 14.4, UI.Fg, FontWeights.Normal);
        page.Children.Add(UI.At(GraphCard("CPU", cpuValue, null, cpuGraph), 0));
        var gpuTemp = UI.Text("", 11.2, UI.Dim, FontWeights.SemiBold);
        gpuValue.Tag = gpuTemp;
        page.Children.Add(UI.At(GraphCard("GPU", gpuValue, gpuTemp, gpuGraph), 2));
        netUp = UI.Text("", 11.2, UI.Dim, FontWeights.Normal);
        page.Children.Add(UI.At(GraphCard("Network", netDown, netUp, netGraph, true), 4));

        var b = new StackPanel { VerticalAlignment = VerticalAlignment.Top };
        b.Children.Add(UI.Text("Battery", 12, UI.Dim, FontWeights.SemiBold));
        battBig = UI.Text("–", 20, UI.Fg, FontWeights.Light);
        b.Children.Add(battBig);
        battStatus = UI.Text("", 12, UI.Dim, FontWeights.Normal);
        b.Children.Add(battStatus);
        var track = new Grid { Height = 8, Margin = new Thickness(0, 34, 0, 0) };
        track.Children.Add(new Border { Background = UI.B("#2EFFFFFF"), CornerRadius = new CornerRadius(4) });
        battFill = new Border { CornerRadius = new CornerRadius(4), HorizontalAlignment = HorizontalAlignment.Left, Width = 0 };
        track.Children.Add(battFill);
        b.Children.Add(track);
        var card = UI.Panel(b, new Thickness(12, 12, 12, 12));
        page.Children.Add(UI.At(card, 6));
        return page;
    }

    Border GraphCard(string label, TextBlock value, TextBlock extra, Graph graph) { return GraphCard(label, value, extra, graph, false); }

    // extraBelow puts the second reading under the first (upload under download) instead of beside it.
    Border GraphCard(string label, TextBlock value, TextBlock extra, Graph graph, bool extraBelow)
    {
        var g = new Grid();
        g.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        g.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        g.RowDefinitions.Add(new RowDefinition { Height = UI.Star });
        g.Children.Add(UI.Row(UI.Text(label, 12, UI.Dim, FontWeights.SemiBold), 0));
        if (extraBelow)
        {
            var stack = new StackPanel { Margin = new Thickness(0, 3, 0, 0) };
            stack.Children.Add(value);
            stack.Children.Add(extra);
            g.Children.Add(UI.Row(stack, 1));
        }
        else
        {
            var line = UI.Columns(UI.Auto, UI.Star);
            line.Children.Add(UI.At(value, 0));
            if (extra != null) { extra.Margin = new Thickness(6, 4, 0, 0); line.Children.Add(UI.At(extra, 1)); }
            g.Children.Add(UI.Row(line, 1));
        }
        graph.Margin = new Thickness(0, 8, 0, 0);
        graph.Height = double.NaN;
        g.Children.Add(UI.Row(graph, 2));
        return UI.Panel(g, new Thickness(12, 12, 12, 12));
    }

    // 1536 -> "1.5 KB/s", 2500000 -> "2.4 MB/s".
    public static string Speed(double bytes)
    {
        if (bytes < 0) return "–";
        if (bytes < 1024 * 1024) return Math.Round(bytes / 1024) + " KB/s";
        return (bytes / 1024 / 1024).ToString("0.0") + " MB/s";
    }

    void UpdateBattery()
    {
        if (!built) return;
        batteryLabel.Text = batteryPercent < 0 ? "" : batteryPercent + "%" + (onAc ? "  ⚡" : "");
        batteryLabel.Foreground = onAc ? UI.Green : (batteryPercent >= 0 && batteryPercent <= 20 ? UI.Red : UI.Dim);
        battBig.Text = batteryPercent < 0 ? "No battery" : batteryPercent + "%";
        battStatus.Text = !onAc ? "On battery" : charging ? "Charging" : "Plugged in";
        var color = batteryPercent >= 0 && batteryPercent <= 20 && !onAc ? UI.Red : UI.Green;
        battFill.Background = color;
        double full = Math.Max(0, ((FrameworkElement)battFill.Parent).ActualWidth);
        if (full <= 0) full = 120;
        battFill.BeginAnimation(WidthProperty, Anim(full * Math.Max(0, batteryPercent) / 100.0, 600, Spring.Soft, 0));
    }

    // Starts readings that only matter while the panel is open.
    void StartLive()
    {
        UpdateBattery();
        UpdateRadioTiles();
        SetTile(nightTile, NightLight.State() == 1 ? 1 : 0, UI.Orange);
        SetTile(muteTile, volume.Available ? (volume.Muted ? 1 : 0) : -1, UI.Red);
        if (volume.Available)
        {
            volSlider.Value = volume.Level;
            SetVolumeGlyph(volume.Level, volume.Muted);
        }
        ThreadPool.QueueUserWorkItem(delegate
        {
            int b = Brightness.Get();
            Ui(delegate
            {
                brightSlider.IsEnabled = b >= 0;
                brightSlider.Opacity = b >= 0 ? 1 : 0.4;
                if (b >= 0 && !brightSlider.Dragging) brightSlider.Value = b / 100.0;
            });
        });
        UpdateSensors();
        UpdateProgress();
        UpdateTickRate();
    }

    void StopLive()
    {
        UpdateSensors();
        UpdateTickRate();
    }

    // CPU, GPU and network readings (including the GPU temperature) run only while the Stats tab is on screen.
    void UpdateSensors()
    {
        bool want = mode == Mode.Expanded && tab == TabStats;
        if (want && sensorTimer == null) sensorTimer = new System.Threading.Timer(SensorTick, null, 0, 1500);
        else if (!want && sensorTimer != null) { sensorTimer.Dispose(); sensorTimer = null; }
    }

    void SensorTick(object state)
    {
        if (!Monitor.TryEnter(sensorGate)) return;
        try
        {
            double cpu, gpu, down, up;
            int temp;
            sensors.Sample(out cpu, out gpu, out temp, out down, out up);
            Ui(delegate
            {
                if (down >= 0) { netGraph.Add(down); netDown.Text = "\u2193 " + Speed(down); netUp.Text = "\u2191 " + Speed(up); }
                if (cpu >= 0) { cpuGraph.Add(cpu); cpuValue.Text = Math.Round(cpu) + "%"; }
                if (gpu >= 0) { gpuGraph.Add(gpu); gpuValue.Text = Math.Round(gpu) + "%"; }
                ((TextBlock)gpuValue.Tag).Text = temp >= 0 ? temp + " °C" : "";
            });
        }
        catch (Exception ex) { Store.Log("sensors: " + ex.Message); }
        finally { Monitor.Exit(sensorGate); }
    }

    // ---------------------------------------------------------------- media

    async void InitMedia()
    {
        try
        {
            mediaManager = await MediaManager.RequestAsync();
            propsHandler = (s, a) => Ui(RefreshMedia);
            playbackHandler = (s, a) => Ui(RefreshMedia);
            mediaManager.CurrentSessionChanged += (s, a) => Ui(HookSession);
            HookSession();
        }
        catch (Exception ex) { Store.Log("media: " + ex.Message); }
    }

    void HookSession()
    {
        try
        {
            if (session != null)
            {
                session.MediaPropertiesChanged -= propsHandler;
                session.PlaybackInfoChanged -= playbackHandler;
            }
            session = mediaManager.GetCurrentSession();
            if (session != null)
            {
                session.MediaPropertiesChanged += propsHandler;
                session.PlaybackInfoChanged += playbackHandler;
            }
        }
        catch (Exception ex) { Store.Log("media session: " + ex.Message); session = null; }
        RefreshMedia();
    }

    static string PrettyApp(string id)
    {
        if (string.IsNullOrEmpty(id)) return "";
        string s = id;
        int bang = s.LastIndexOf('!');
        if (bang >= 0) s = s.Substring(bang + 1);
        int slash = s.LastIndexOfAny(new[] { '\\', '/' });
        if (slash >= 0) s = s.Substring(slash + 1);
        if (s.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) s = s.Substring(0, s.Length - 4);
        if (s.Length > 0) s = char.ToUpper(s[0]) + s.Substring(1);
        return s == "App" ? "" : s;
    }

    // The average colour of the artwork, brightened, tints the island's glow.
    static Color AverageColor(BitmapSource art)
    {
        var small = new FormatConvertedBitmap(new TransformedBitmap(art, new ScaleTransform(8.0 / art.PixelWidth, 8.0 / art.PixelHeight)), PixelFormats.Bgra32, null, 0);
        int w = small.PixelWidth, h = small.PixelHeight;
        var px = new byte[w * h * 4];
        small.CopyPixels(px, w * 4, 0);
        double r = 0, g = 0, b = 0;
        for (int i = 0; i < px.Length; i += 4) { b += px[i]; g += px[i + 1]; r += px[i + 2]; }
        double n = w * h, max = Math.Max(1, Math.Max(r, Math.Max(g, b)) / n);
        double k = 255 / max;
        return Color.FromRgb((byte)Math.Min(255, r / n * k), (byte)Math.Min(255, g / n * k), (byte)Math.Min(255, b / n * k));
    }

    async void RefreshMedia()
    {
        int version = ++mediaVersion;
        var s = session;
        string newTitle = null, newArtist = null;
        bool isPlaying = false;
        ImageSource art = artwork;
        try
        {
            if (s != null)
            {
                isPlaying = s.GetPlaybackInfo().PlaybackStatus == PlaybackStatus.Playing;
                var props = await s.TryGetMediaPropertiesAsync();
                if (version != mediaVersion) return;
                newTitle = props.Title;
                newArtist = props.Artist;
                string key = s.SourceAppUserModelId + "|" + newTitle + "|" + newArtist;
                if (key != mediaKey)
                {
                    art = null;
                    if (props.Thumbnail != null)
                    {
                        var ms = new MemoryStream();
                        using (var ras = await props.Thumbnail.OpenReadAsync())
                        using (var st = ras.AsStreamForRead())
                            st.CopyTo(ms);
                        ms.Position = 0;
                        var bi = new BitmapImage();
                        bi.BeginInit();
                        bi.CacheOption = BitmapCacheOption.OnLoad;
                        bi.DecodePixelWidth = 300;
                        bi.StreamSource = ms;
                        bi.EndInit();
                        bi.Freeze();
                        art = bi;
                    }
                    if (version != mediaVersion) return;
                    mediaKey = key;
                    try { SetTint(art != null ? AverageColor((BitmapSource)art) : Color.FromRgb(0x4A, 0x6C, 0xFF)); } catch (Exception) { }
                }
                appName = PrettyApp(s.SourceAppUserModelId);
            }
        }
        catch (Exception ex) { Store.Log("media read: " + ex.Message); }
        if (version != mediaVersion) return;

        if (s == null || string.IsNullOrEmpty(newTitle)) { newTitle = null; newArtist = null; art = null; mediaKey = null; isPlaying = false; appName = null; }
        artwork = art;
        playing = isPlaying;
        title = newTitle;
        artist = newArtist;
        UpdateMediaUi();
        UpdateCollapsed();
        UpdateTickRate();
    }

    async void MediaCommand(int which)
    {
        var s = session;
        if (s == null) return;
        try
        {
            if (which == 0) await s.TrySkipPreviousAsync();
            else if (which == 1) await s.TryTogglePlayPauseAsync();
            else await s.TrySkipNextAsync();
        }
        catch (Exception) { }
    }

    // ---------------------------------------------------------------- radios and night light

    async void InitRadios()
    {
        try
        {
            await Radio.RequestAccessAsync();
            var all = await Radio.GetRadiosAsync();
            foreach (var r in all)
            {
                if (r.Kind == RadioKind.WiFi && wifi == null) wifi = r;
                else if (r.Kind == RadioKind.Bluetooth && bluetooth == null) bluetooth = r;
            }
            if (wifi != null) wifi.StateChanged += (s, a) => Ui(UpdateRadioTiles);
            if (bluetooth != null) bluetooth.StateChanged += (s, a) => Ui(UpdateRadioTiles);
        }
        catch (Exception ex) { Store.Log("radios: " + ex.Message); }
        UpdateRadioTiles();
    }

    void UpdateRadioTiles()
    {
        if (!built) return;
        SetTile(wifiTile, wifi == null ? -1 : wifi.State == RadioState.On ? 1 : 0, UI.Blue);
        SetTile(btTile, bluetooth == null ? -1 : bluetooth.State == RadioState.On ? 1 : 0, UI.Blue);
    }

    async void ToggleRadio(Radio r)
    {
        if (r == null) return;
        try { await r.SetStateAsync(r.State == RadioState.On ? RadioState.Off : RadioState.On); }
        catch (Exception ex) { Store.Log("radio toggle: " + ex.Message); }
        UpdateRadioTiles();
    }

    void ToggleNightLight()
    {
        int state = NightLight.State();
        if (state < 0 || !NightLight.Set(state == 0))
        {
            try { Process.Start("ms-settings:nightlight"); } catch (Exception) { }
            return;
        }
        SetTile(nightTile, state == 0 ? 1 : 0, UI.Orange);
    }

    // ---------------------------------------------------------------- clipboard

    Grid BuildClipboard()
    {
        var page = new Grid();
        page.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        page.RowDefinitions.Add(new RowDefinition { Height = UI.Star });
        var head = UI.Columns(UI.Auto, UI.Star, UI.Auto);
        head.Margin = new Thickness(4, 0, 0, 8);
        head.Children.Add(UI.At(UI.Text("Recently copied", 13, UI.Fg, FontWeights.SemiBold), 0));
        clipCount = UI.Text("", 12, UI.Dim, FontWeights.Normal);
        clipCount.Margin = new Thickness(8, 0, 0, 0);
        head.Children.Add(UI.At(clipCount, 1));
        var clear = UI.Text("Clear unpinned", 12, UI.Fg, FontWeights.SemiBold);
        clear.Margin = new Thickness(10, 0, 10, 0);
        head.Children.Add(UI.At(UI.Button(clear, 0, 26, 13, UI.Card, delegate { clips.RemoveAll(c => !c.Pinned); RenderClips(); }), 2));
        page.Children.Add(UI.Row(head, 0));
        clipList = new StackPanel();
        page.Children.Add(UI.Row(new ScrollViewer { Content = clipList, VerticalScrollBarVisibility = ScrollBarVisibility.Hidden, PanningMode = PanningMode.VerticalOnly }, 1));
        return page;
    }

    uint ReadDword(uint format)
    {
        if (!Native.OpenClipboard(hwnd)) throw new IOException("clipboard busy");
        try
        {
            IntPtr h = Native.GetClipboardData(format);
            if (h == IntPtr.Zero) return 1;
            IntPtr p = Native.GlobalLock(h);
            if (p == IntPtr.Zero) return 1;
            uint v = (uint)Marshal.ReadInt32(p);
            Native.GlobalUnlock(h);
            return v;
        }
        finally { Native.CloseClipboard(); }
    }

    void ReadClipboard()
    {
        if (DateTime.Now < ignoreClipUntil) return;
        try
        {
            // Password managers mark their copies with these formats; Windows' own history skips them too.
            if (Native.IsClipboardFormatAvailable(fmtExclude) || Native.IsClipboardFormatAvailable(fmtViewerIgnore)) return;
            if (Native.IsClipboardFormatAvailable(fmtCanInclude) && ReadDword(fmtCanInclude) == 0) return;

            if (Clipboard.ContainsText())
            {
                string text = Clipboard.GetText();
                if (!string.IsNullOrWhiteSpace(text)) AddTextClip(text.Length > 200000 ? text.Substring(0, 200000) : text);
            }
            else if (Clipboard.ContainsImage())
            {
                byte[] png = null;
                var raw = Clipboard.ContainsData("PNG") ? Clipboard.GetData("PNG") as MemoryStream : null;
                if (raw != null) png = raw.ToArray();
                else
                {
                    var img = Clipboard.GetImage();
                    if (img == null || (long)img.PixelWidth * img.PixelHeight > 40000000) return;
                    var opaque = new FormatConvertedBitmap(img, PixelFormats.Bgr32, null, 0);    // clipboard DIBs often carry a blank alpha channel
                    var enc = new PngBitmapEncoder();
                    enc.Frames.Add(BitmapFrame.Create(opaque));
                    using (var ms = new MemoryStream()) { enc.Save(ms); png = ms.ToArray(); }
                }
                AddImage(png);
            }
        }
        catch (Exception)
        {
            if (++clipAttempts <= 3) Later(150, ReadClipboard);
        }
    }

    void AddTextClip(string text)
    {
        var existing = clips.FirstOrDefault(c => !c.IsImage && c.Text == text);
        if (existing != null) clips.Remove(existing);
        clips.Insert(0, existing ?? new Clip { Text = text });
        TrimClips();
    }

    void AddImage(byte[] png)
    {
        string hash;
        using (var md5 = MD5.Create()) hash = Convert.ToBase64String(md5.ComputeHash(png));
        var existing = clips.FirstOrDefault(c => c.IsImage && c.Hash == hash);
        if (existing != null) clips.Remove(existing);
        clips.Insert(0, existing ?? new Clip { IsImage = true, Png = png, Hash = hash, Thumb = Thumbnail(png) });
        TrimClips();
    }

    static ImageSource Thumbnail(byte[] png)
    {
        try
        {
            var bi = new BitmapImage();
            bi.BeginInit();
            bi.CacheOption = BitmapCacheOption.OnLoad;
            bi.DecodePixelHeight = 72;
            bi.StreamSource = new MemoryStream(png);
            bi.EndInit();
            bi.Freeze();
            return bi;
        }
        catch (Exception) { return null; }
    }

    void TrimClips()
    {
        while (clips.Count > 30)
        {
            int i = clips.FindLastIndex(c => !c.Pinned);
            if (i < 0) break;
            clips.RemoveAt(i);
        }
        if (mode == Mode.Expanded && tab == TabClipboard) RenderClips();
    }

    void RenderClips()
    {
        if (!built) return;
        clipList.Children.Clear();
        clipCount.Text = (clips.Count == 0 ? "" : clips.Count + (clips.Count == 1 ? " item" : " items") + "   ") + (pickerKeys.Length > 0 ? pickerKeys + PickerHint : "");
        if (clips.Count == 0)
        {
            var empty = UI.Text("Copy some text or an image and it shows up here.", 13, UI.Dim, FontWeights.Normal);
            empty.Margin = new Thickness(4, 30, 0, 0);
            empty.HorizontalAlignment = HorizontalAlignment.Center;
            clipList.Children.Add(empty);
            return;
        }
        foreach (var c in clips.Where(x => x.Pinned).Concat(clips.Where(x => !x.Pinned)))
        {
            var clip = c;
            var row = UI.Columns(UI.Star, UI.Auto, UI.Auto);
            UIElement body;
            if (clip.IsImage) body = new Image { Source = clip.Thumb, Height = 56, HorizontalAlignment = HorizontalAlignment.Left, Stretch = Stretch.Uniform };
            else
            {
                string preview = clip.Text.Trim();
                if (preview.Length > 400) preview = preview.Substring(0, 400);
                body = new TextBlock { Text = preview, FontSize = 13, Foreground = UI.Fg, FontFamily = UI.TextFont, TextWrapping = TextWrapping.Wrap, TextTrimming = TextTrimming.CharacterEllipsis, MaxHeight = 38, VerticalAlignment = VerticalAlignment.Center };
            }
            row.Children.Add(UI.At(body, 0));
            row.Children.Add(UI.At(UI.Button(UI.Icon(clip.Pinned ? "" : "", 13, clip.Pinned ? UI.Orange : UI.Dim), 30, 30, 15, null, delegate { TogglePin(clip); }), 1));
            row.Children.Add(UI.At(UI.Button(UI.Icon("", 12, UI.Dim), 30, 30, 15, null, delegate { RemoveClip(clip); }), 2));
            var card = UI.Button(row, 0, 0, 14, UI.Card, delegate { CopyBack(clip); });
            card.Padding = new Thickness(12, 7, 6, 7);
            card.Margin = new Thickness(0, 0, 0, 6);
            card.BorderThickness = new Thickness(1);
            card.BorderBrush = UI.Rim;
            clipList.Children.Add(card);
        }
    }

    void CopyBack(Clip clip)
    {
        try
        {
            ignoreClipUntil = DateTime.Now.AddMilliseconds(600);
            if (clip.IsImage)
            {
                var bi = new BitmapImage();
                bi.BeginInit();
                bi.CacheOption = BitmapCacheOption.OnLoad;
                bi.StreamSource = new MemoryStream(clip.Png);
                bi.EndInit();
                bi.Freeze();
                Clipboard.SetImage(bi);
            }
            else Clipboard.SetText(clip.Text);
            clips.Remove(clip);
            clips.Insert(0, clip);
            RenderClips();
#if FRIENDS
            if (pickerMode)
            {
                var copied = MakePopup("\uE8FB", UI.Green, "Copied", false);
                copied.Value.Text = "Press Ctrl+V";
                copied.Value.Foreground = UI.Dim;
                Collapse();
                ShowPopup(copied.Root, 300, PopH, 1800, false);
                return;
            }
#else
            if (pickerMode)
            {
                // Opened with the picker hotkey: put it straight into the window you were typing in.
                IntPtr target = pasteTarget;
                Collapse();
                Later(90, delegate
                {
                    if (target != IntPtr.Zero) Native.SetForegroundWindow(target);
                    Later(60, Native.SendPaste);
                });
                return;
            }
#endif
            var p = MakePopup("", UI.Green, "Copied", false);
            Collapse();
            ShowPopup(p.Root, 200, PopH, 1200, false);
        }
        catch (Exception ex) { Store.Log("copy back: " + ex.Message); }
    }

    void TogglePin(Clip clip)
    {
        clip.Pinned = !clip.Pinned;
        if (clip.IsImage && clip.Pinned && clip.File == null)
        {
            try
            {
                Directory.CreateDirectory(Store.ImageDir);
                clip.File = Guid.NewGuid().ToString("N") + ".png";
                File.WriteAllBytes(System.IO.Path.Combine(Store.ImageDir, clip.File), clip.Png);
            }
            catch (Exception) { clip.File = null; }
        }
        if (!clip.Pinned) DeleteClipFile(clip);
        SaveClips();
        RenderClips();
    }

    void RemoveClip(Clip clip)
    {
        clips.Remove(clip);
        if (clip.Pinned) { DeleteClipFile(clip); SaveClips(); }
        RenderClips();
    }

    void DeleteClipFile(Clip clip)
    {
        if (clip.File == null) return;
        try { File.Delete(System.IO.Path.Combine(Store.ImageDir, clip.File)); } catch (Exception) { }
        clip.File = null;
    }

    void SaveClips()
    {
        var list = clips.Where(c => c.Pinned).Select(c => new SavedClip { Kind = c.IsImage ? "image" : "text", Text = c.IsImage ? null : c.Text, File = c.File }).ToList();
        Store.Save(Store.ClipsFile, list);
    }

    void LoadClips()
    {
        var list = Store.Load<List<SavedClip>>(Store.ClipsFile);
        if (list == null) return;
        foreach (var s in list)
        {
            if (s.Kind == "image")
            {
                try
                {
                    var png = File.ReadAllBytes(System.IO.Path.Combine(Store.ImageDir, s.File));
                    string hash;
                    using (var md5 = MD5.Create()) hash = Convert.ToBase64String(md5.ComputeHash(png));
                    clips.Add(new Clip { IsImage = true, Png = png, Hash = hash, Thumb = Thumbnail(png), Pinned = true, File = s.File });
                }
                catch (Exception) { }
            }
            else if (!string.IsNullOrEmpty(s.Text)) clips.Add(new Clip { Text = s.Text, Pinned = true });
        }
    }

    // ---------------------------------------------------------------- shelf

    Grid BuildShelf()
    {
        var page = new Grid();
        page.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        page.RowDefinitions.Add(new RowDefinition { Height = UI.Star });
        var head = UI.Columns(UI.Auto, UI.Star);
        head.Margin = new Thickness(4, 0, 0, 8);
        head.Children.Add(UI.At(UI.Text("Shelf", 13, UI.Fg, FontWeights.SemiBold), 0));
        var hint = UI.Text("Drag a file out to use it. Pin files, folders or apps to keep them as launchers.", 12, UI.Dim, FontWeights.Normal);
        hint.Margin = new Thickness(10, 0, 0, 0);
        head.Children.Add(UI.At(hint, 1));
        page.Children.Add(UI.Row(head, 0));
        shelfList = new StackPanel();
        page.Children.Add(UI.Row(new ScrollViewer { Content = shelfList, VerticalScrollBarVisibility = ScrollBarVisibility.Hidden }, 1));
        return page;
    }

    void OnDragOver(object sender, DragEventArgs e)
    {
        e.Handled = true;
        if (draggingOut || !e.Data.GetDataPresent(DataFormats.FileDrop)) { e.Effects = DragDropEffects.None; return; }
        e.Effects = (e.AllowedEffects & DragDropEffects.Link) != 0 ? DragDropEffects.Link : DragDropEffects.Copy;
        dragInside = true;
        leaveTimer.Stop();
        if (mode != Mode.Expanded) Expand(false);
        if (tab != TabShelf) SelectTab(TabShelf);
    }

    void OnDrop(object sender, DragEventArgs e)
    {
        e.Handled = true;
        dragInside = false;
        if (draggingOut) { e.Effects = DragDropEffects.None; return; }
        var paths = e.Data.GetData(DataFormats.FileDrop) as string[];
        if (paths == null) { e.Effects = DragDropEffects.None; return; }
        foreach (var p in paths)
            if (!shelf.Any(s => string.Equals(s.Path, p, StringComparison.OrdinalIgnoreCase)))
                shelf.Insert(0, new ShelfItem { Path = p });
        e.Effects = (e.AllowedEffects & DragDropEffects.Link) != 0 ? DragDropEffects.Link : DragDropEffects.Copy;
        SaveShelf();
        RenderShelf();
        leaveTimer.Start();
    }

    static bool Exists(string path) { return File.Exists(path) || Directory.Exists(path); }

    static ImageSource FileIcon(string path)
    {
        try
        {
            IntPtr icon = Native.LargeIcon(path);
            if (icon == IntPtr.Zero)
            {
                var info = new Native.SHFILEINFO();
                Native.SHGetFileInfo(path, 0, ref info, (uint)Marshal.SizeOf(info), 0x100);    // SHGFI_ICON, large
                icon = info.hIcon;
            }
            if (icon == IntPtr.Zero) return null;
            var src = Imaging.CreateBitmapSourceFromHIcon(icon, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions());
            Native.DestroyIcon(icon);
            src.Freeze();
            return src;
        }
        catch (Exception) { return null; }
    }

    void RenderShelf()
    {
        if (!built) return;
        shelfList.Children.Clear();
        if (shelf.Count == 0)
        {
            var drop = new Border
            {
                Height = 150, CornerRadius = new CornerRadius(20), BorderThickness = new Thickness(1.5),
                BorderBrush = new SolidColorBrush(Color.FromArgb(0x44, 255, 255, 255)), Background = UI.Card,
            };
            var s = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
            s.Children.Add(UI.Icon("", 28, UI.Dim));
            var t = UI.Text("Drop files, folders or apps here", 14, UI.Dim, FontWeights.SemiBold);
            t.HorizontalAlignment = HorizontalAlignment.Center;
            t.Margin = new Thickness(0, 8, 0, 0);
            s.Children.Add(t);
            drop.Child = s;
            shelfList.Children.Add(drop);
            return;
        }

        // Pinned files, folders and apps sit at the top as one-click launch tiles.
        var pinned = shelf.Where(x => x.Pinned).ToList();
        if (pinned.Count > 0)
        {
            var wrap = new WrapPanel { Margin = new Thickness(0, 0, 0, 2) };
            foreach (var it in pinned) wrap.Children.Add(LaunchTile(it));
            shelfList.Children.Add(wrap);
        }
        foreach (var it in shelf.Where(x => !x.Pinned)) shelfList.Children.Add(ShelfRow(it));
    }

    Border LaunchTile(ShelfItem item)
    {
        bool there = Exists(item.Path);
        if (item.Icon == null && there) item.Icon = FileIcon(item.Path);
        var g = new Grid { Width = 70 };
        var s = new StackPanel { HorizontalAlignment = HorizontalAlignment.Center };
        s.Children.Add(item.Icon != null && there ? (UIElement)new Image { Source = item.Icon, Width = 32, Height = 32 } : UI.Icon("", 22, UI.Dim));
        string name = System.IO.Path.GetFileNameWithoutExtension(item.Path.TrimEnd('\\'));
        if (string.IsNullOrEmpty(name)) name = item.Path;
        var label = UI.Text(name, 11.2, there ? UI.Fg : UI.Dim, FontWeights.Normal);
        label.HorizontalAlignment = HorizontalAlignment.Center;
        label.MaxWidth = 64;
        label.Margin = new Thickness(0, 5, 0, 0);
        s.Children.Add(label);
        g.Children.Add(s);
        var unpin = UI.Button(UI.Icon("", 10, UI.Fg), 20, 20, 10, UI.B("#66000000"), delegate { item.Pinned = false; SaveShelf(); RenderShelf(); });
        unpin.HorizontalAlignment = HorizontalAlignment.Right;
        unpin.VerticalAlignment = VerticalAlignment.Top;
        unpin.Margin = new Thickness(0, -6, -2, 0);
        unpin.Visibility = Visibility.Hidden;
        unpin.ToolTip = "Unpin";
        g.Children.Add(unpin);
        var tile = new Border { Child = g, Background = UI.Card, BorderBrush = UI.Rim, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(16), Padding = new Thickness(4, 10, 4, 8), Margin = new Thickness(0, 0, 6, 6), Cursor = Cursors.Hand, ToolTip = item.Path };
        tile.MouseEnter += delegate { unpin.Visibility = Visibility.Visible; };
        tile.MouseLeave += delegate { unpin.Visibility = Visibility.Hidden; };
        HookDragAndOpen(tile, item, there);
        return tile;
    }

    Border ShelfRow(ShelfItem item)
    {
        bool there = Exists(item.Path);
        if (item.Icon == null && there) item.Icon = FileIcon(item.Path);
        var row = UI.Columns(UI.Px(40), UI.Star, UI.Auto, UI.Auto, UI.Auto);
        UIElement icon = item.Icon != null && there ? (UIElement)new Image { Source = item.Icon, Width = 32, Height = 32 } : UI.Icon("", 20, UI.Dim);
        row.Children.Add(UI.At(icon, 0));
        var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(4, 0, 8, 0) };
        text.Children.Add(UI.Text(System.IO.Path.GetFileName(item.Path.TrimEnd('\\')), 13, there ? UI.Fg : UI.Dim, FontWeights.SemiBold));
        text.Children.Add(UI.Text(there ? System.IO.Path.GetDirectoryName(item.Path) : "Missing: moved or deleted", 11, there ? UI.Dim : UI.Red, FontWeights.Normal));
        row.Children.Add(UI.At(text, 1));

        // Quick actions appear while the mouse is on the row.
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Visibility = Visibility.Collapsed };
        if (there)
        {
            string path = item.Path;
            actions.Children.Add(ActionButton("", "Copy path", b => { Clipboard.SetText(path); Done(b); }));
            actions.Children.Add(ActionButton("", "Show in folder", b => { try { Process.Start("explorer.exe", "/select,\"" + path + "\""); } catch (Exception) { } }));
            actions.Children.Add(ActionButton("", "Zip", b => ZipItem(path, b)));
            if (IsImage(path)) actions.Children.Add(ActionButton("", "Make a smaller copy", b => ShrinkImage(path, b)));
        }
        row.Children.Add(UI.At(actions, 2));
        var pin = UI.Button(UI.Icon("", 13, UI.Dim), 30, 30, 15, null, delegate { item.Pinned = true; SaveShelf(); RenderShelf(); });
        pin.ToolTip = "Pin as a launcher";
        row.Children.Add(UI.At(pin, 3));
        row.Children.Add(UI.At(UI.Button(UI.Icon("", 12, UI.Dim), 30, 30, 15, null, delegate { shelf.Remove(item); SaveShelf(); RenderShelf(); }), 4));
        var card = new Border { Child = row, Background = UI.Card, BorderBrush = UI.Rim, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(14), Padding = new Thickness(8, 5, 6, 5), Margin = new Thickness(0, 0, 0, 6), Cursor = there ? Cursors.SizeAll : Cursors.Arrow };
        card.MouseEnter += delegate { actions.Visibility = Visibility.Visible; };
        card.MouseLeave += delegate { actions.Visibility = Visibility.Collapsed; };
        HookDragAndOpen(card, item, there);
        return card;
    }

    // Drag out to use the file; a plain click opens it.
    void HookDragAndOpen(Border card, ShelfItem item, bool there)
    {
        card.PreviewMouseLeftButtonDown += delegate(object s, MouseButtonEventArgs e) { dragStart = e.GetPosition(this); dragCandidate = item; };
        card.MouseMove += delegate(object s, MouseEventArgs e)
        {
            if (e.LeftButton != MouseButtonState.Pressed || dragCandidate != item) return;
            var p = e.GetPosition(this);
            if (Math.Abs(p.X - dragStart.X) < SystemParameters.MinimumHorizontalDragDistance && Math.Abs(p.Y - dragStart.Y) < SystemParameters.MinimumVerticalDragDistance) return;
            dragCandidate = null;
            DragOut(item, card);
        };
        card.MouseLeftButtonUp += delegate
        {
            if (dragCandidate != item) return;
            dragCandidate = null;
            if (there) try { Process.Start(new ProcessStartInfo(item.Path) { UseShellExecute = true }); } catch (Exception) { }
        };
    }

    Border ActionButton(string glyph, string tip, Action<Border> act)
    {
        Border b = null;
        b = UI.Button(UI.Icon(glyph, 12.8, UI.Fg), 30, 30, 15, null, delegate { act(b); });
        b.ToolTip = tip;
        return b;
    }

    // Shows a green tick on an action button for a moment.
    void Done(Border b)
    {
        var t = b.Child as TextBlock;
        if (t == null) return;
        string old = t.Text;
        t.Text = "";
        t.Foreground = UI.Green;
        Later(1200, delegate { t.Text = old; t.Foreground = UI.Fg; });
    }

    static bool IsImage(string path)
    {
        string e = System.IO.Path.GetExtension(path).ToLowerInvariant();
        return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".gif" || e == ".tif" || e == ".tiff";
    }

    // "report.zip" -> "report (2).zip" when the name is taken.
    static string FreeName(string path)
    {
        if (!Exists(path)) return path;
        string dir = System.IO.Path.GetDirectoryName(path), name = System.IO.Path.GetFileNameWithoutExtension(path), ext = System.IO.Path.GetExtension(path);
        for (int i = 2; ; i++)
        {
            string next = System.IO.Path.Combine(dir, name + " (" + i + ")" + ext);
            if (!Exists(next)) return next;
        }
    }

    void AddToShelfTop(string path)
    {
        if (!shelf.Any(x => string.Equals(x.Path, path, StringComparison.OrdinalIgnoreCase))) shelf.Insert(0, new ShelfItem { Path = path });
        SaveShelf();
        RenderShelf();
    }

    // Zips a file or folder next to itself, in the background, and puts the zip on the shelf.
    void ZipItem(string path, Border button)
    {
        var t = button.Child as TextBlock;
        if (t != null) t.Foreground = UI.Dim;
        ThreadPool.QueueUserWorkItem(delegate
        {
            try
            {
                string clean = path.TrimEnd('\\');
                string target = FreeName(clean + ".zip");
                if (Directory.Exists(clean)) ZipFile.CreateFromDirectory(clean, target, CompressionLevel.Optimal, true);
                else using (var zip = ZipFile.Open(target, ZipArchiveMode.Create)) zip.CreateEntryFromFile(clean, System.IO.Path.GetFileName(clean), CompressionLevel.Optimal);
                Ui(delegate { AddToShelfTop(target); });
            }
            catch (Exception ex) { Store.Log("zip: " + ex.Message); Ui(delegate { if (t != null) { t.Foreground = UI.Red; } }); }
        });
    }

    // Saves a copy no bigger than 1600 px on its longest side ("photo-small.jpg") and puts it on the shelf.
    void ShrinkImage(string path, Border button)
    {
        var t = button.Child as TextBlock;
        if (t != null) t.Foreground = UI.Dim;
        ThreadPool.QueueUserWorkItem(delegate
        {
            try
            {
                var frame = BitmapDecoder.Create(new Uri(path), BitmapCreateOptions.None, BitmapCacheOption.OnLoad).Frames[0];
                double scale = Math.Min(1.0, 1600.0 / Math.Max(frame.PixelWidth, frame.PixelHeight));
                BitmapSource src = scale < 1 ? (BitmapSource)new TransformedBitmap(frame, new ScaleTransform(scale, scale)) : frame;
                bool png = System.IO.Path.GetExtension(path).ToLowerInvariant() == ".png";
                BitmapEncoder enc = png ? (BitmapEncoder)new PngBitmapEncoder() : new JpegBitmapEncoder { QualityLevel = 85 };
                enc.Frames.Add(BitmapFrame.Create(src));
                string target = FreeName(System.IO.Path.Combine(System.IO.Path.GetDirectoryName(path), System.IO.Path.GetFileNameWithoutExtension(path) + "-small" + (png ? ".png" : ".jpg")));
                using (var fs = File.Create(target)) enc.Save(fs);
                Ui(delegate { AddToShelfTop(target); });
            }
            catch (Exception ex) { Store.Log("shrink: " + ex.Message); Ui(delegate { if (t != null) { t.Foreground = UI.Red; } }); }
        });
    }

    void DragOut(ShelfItem item, UIElement source)
    {
        if (!Exists(item.Path)) return;
        draggingOut = true;
        var result = DragDropEffects.None;
        try
        {
            var data = new DataObject(DataFormats.FileDrop, new[] { item.Path });
            // Copy or link only, so dragging out never moves the original file away.
            result = DragDrop.DoDragDrop(source, data, DragDropEffects.Copy | DragDropEffects.Link);
        }
        catch (Exception ex) { Store.Log("drag out: " + ex.Message); }
        draggingOut = false;
        if (result != DragDropEffects.None && !item.Pinned)
        {
            shelf.Remove(item);
            SaveShelf();
            RenderShelf();
        }
        leaveTimer.Stop();
        leaveTimer.Start();
    }

    void SaveShelf()
    {
        Store.Save(Store.ShelfFile, shelf.Select(s => new SavedShelfItem { Path = s.Path, Pinned = s.Pinned }).ToList());
    }

    void LoadShelf()
    {
        var list = Store.Load<List<SavedShelfItem>>(Store.ShelfFile);
        if (list == null) return;
        foreach (var s in list) if (!string.IsNullOrEmpty(s.Path)) shelf.Add(new ShelfItem { Path = s.Path, Pinned = s.Pinned });
    }

    // ---------------------------------------------------------------- timer and stopwatch

    Grid BuildTimer()
    {
        var page = UI.Columns(UI.Px(300), UI.Px(10), UI.Star);

        var t = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        timerText = UI.Text("15:00", 34, UI.Fg, FontWeights.Light);
        timerText.HorizontalAlignment = HorizontalAlignment.Center;
        timerText.FontFeatures();
        t.Children.Add(timerText);
        var presets = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 6, 0, 0) };
        foreach (int m in new[] { 1, 5, 10, 25 })
        {
            int minutes = m;
            presets.Children.Add(Pad(UI.Button(Centered(UI.Text(m + " min", 12, UI.Fg, FontWeights.SemiBold)), 56, 28, 14, UI.Card, delegate { StartTimer(minutes); })));
        }
        t.Children.Add(presets);
        var custom = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 8, 0, 0) };
        custom.Children.Add(Pad(UI.Button(UI.Icon("", 11, UI.Fg), 28, 28, 14, UI.Card, delegate { customMinutes = Math.Max(1, customMinutes - (customMinutes > 10 ? 5 : 1)); RenderTimer(); })));
        customLabel = UI.Text("15 min", 12.5, UI.Fg, FontWeights.SemiBold);
        customLabel.Width = 52;
        customLabel.TextAlignment = TextAlignment.Center;
        custom.Children.Add(customLabel);
        custom.Children.Add(Pad(UI.Button(UI.Icon("", 11, UI.Fg), 28, 28, 14, UI.Card, delegate { customMinutes = Math.Min(600, customMinutes + (customMinutes >= 10 ? 5 : 1)); RenderTimer(); })));
        custom.Children.Add(Pad(UI.Button(Centered(UI.Text("Start", 12, UI.Fg, FontWeights.SemiBold)), 56, 28, 14, UI.Orange, delegate { StartTimer(customMinutes); })));
        t.Children.Add(custom);
        var tControls = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 8, 0, 0) };
        timerPauseLabel = UI.Text("Pause", 12, UI.Fg, FontWeights.SemiBold);
        tControls.Children.Add(Pad(UI.Button(Centered(UI.Text("Focus 25", 12, UI.Fg, FontWeights.SemiBold)), 78, 28, 14, UI.Purple, StartFocus)));
        tControls.Children.Add(Pad(UI.Button(Centered(timerPauseLabel), 72, 28, 14, UI.Card, PauseTimer)));
        tControls.Children.Add(Pad(UI.Button(Centered(UI.Text("Cancel", 12, UI.Fg, FontWeights.SemiBold)), 72, 28, 14, UI.Card, CancelTimer)));
        t.Children.Add(tControls);
        page.Children.Add(UI.At(UI.Panel(t, new Thickness(8)), 0));

        var w = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        w.Children.Add(Centered(UI.Text("Stopwatch", 12, UI.Dim, FontWeights.SemiBold)));
        swText = UI.Text("0:00.0", 30, UI.Fg, FontWeights.Light);
        swText.HorizontalAlignment = HorizontalAlignment.Center;
        swText.FontFeatures();
        w.Children.Add(swText);
        var wControls = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 10, 0, 0) };
        swStartLabel = UI.Text("Start", 12, UI.Fg, FontWeights.SemiBold);
        wControls.Children.Add(Pad(UI.Button(Centered(swStartLabel), 66, 28, 14, UI.Green, delegate
        {
            if (stopwatch.IsRunning) stopwatch.Stop(); else stopwatch.Start();
            RenderTimer();
            UpdateTickRate();
            UpdateCollapsed();
        })));
        wControls.Children.Add(Pad(UI.Button(Centered(UI.Text("Reset", 12, UI.Fg, FontWeights.SemiBold)), 66, 28, 14, UI.Card, delegate { stopwatch.Reset(); RenderTimer(); UpdateTickRate(); UpdateCollapsed(); })));
        w.Children.Add(wControls);
        page.Children.Add(UI.At(UI.Panel(w, new Thickness(8)), 2));
        return page;
    }

    static UIElement Centered(TextBlock t) { t.HorizontalAlignment = HorizontalAlignment.Center; return t; }
    static Border Pad(Border b) { b.Margin = new Thickness(3, 0, 3, 0); return b; }

    TimeSpan TimerLeft() { return timerPaused ? timerLeft : timerEnd - DateTime.Now; }

    void StartTimer(int minutes)
    {
        timerRunning = true;
        timerPaused = false;
        timerEnd = DateTime.Now.AddMinutes(minutes);
        RenderTimer();
        UpdateTickRate();
        UpdateCollapsed();
    }

    void PauseTimer()
    {
        if (!timerRunning) return;
        if (timerPaused) { timerEnd = DateTime.Now + timerLeft; timerPaused = false; }
        else { timerLeft = timerEnd - DateTime.Now; timerPaused = true; }
        RenderTimer();
    }

    // A 25-minute focus session: the pill turns purple and the island holds back notification pop-ups.
    void StartFocus()
    {
        StartTimer(25);
        focusMode = true;
        RenderTimer();
        UpdateCollapsed();
    }

    void CancelTimer()
    {
        focusMode = false;
        timerRunning = false;
        timerPaused = false;
        RenderTimer();
        UpdateTickRate();
        UpdateCollapsed();
    }

    void RenderTimer()
    {
        if (!built) return;
        timerText.Text = timerRunning ? Format(TimerLeft()) : Format(TimeSpan.FromMinutes(customMinutes));
        timerText.Foreground = timerRunning ? (focusMode ? UI.Purple : UI.Orange) : UI.Fg;
        timerPauseLabel.Text = timerPaused ? "Resume" : "Pause";
        customLabel.Text = customMinutes + " min";
        var e = stopwatch.Elapsed;
        swText.Text = string.Format("{0}:{1:00}.{2}", (int)e.TotalMinutes, e.Seconds, e.Milliseconds / 100);
        swStartLabel.Text = stopwatch.IsRunning ? "Stop" : "Start";
    }

    // The tick only runs while something on screen needs it: a timer, the stopwatch, or the song position.
    void UpdateTickRate()
    {
        bool songVisible = mode == Mode.Expanded && tab == TabMedia && playing;
        bool needed = timerRunning || stopwatch.IsRunning || songVisible;
        if (!needed) { tickTimer.Stop(); return; }
        int ms = mode == Mode.Expanded && tab == TabTimer && stopwatch.IsRunning ? 100 : 1000;
        if (tickTimer.Interval.TotalMilliseconds != ms) tickTimer.Interval = TimeSpan.FromMilliseconds(ms);
        if (!tickTimer.IsEnabled) tickTimer.Start();
    }

    void Tick()
    {
        if (timerRunning && !timerPaused && DateTime.Now >= timerEnd)
        {
            timerRunning = false;
            TimerDone();
            UpdateCollapsed();
        }
        if (mode == Mode.Expanded && tab == TabTimer) RenderTimer();
        if (mode == Mode.Expanded && tab == TabMedia) UpdateProgress();
        if (mode == Mode.Collapsed && !hidden)
        {
            if (timerRunning) pillText.Text = Format(TimerLeft());
            else if (stopwatch.IsRunning) pillText.Text = Format(stopwatch.Elapsed);
        }
        UpdateTickRate();
    }

    void TimerDone()
    {
        bool wasFocus = focusMode;
        focusMode = false;
        try { new SoundPlayer(System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), @"Media\Alarm01.wav")).Play(); }
        catch (Exception) { SystemSounds.Exclamation.Play(); }
        RenderTimer();
        if (mode == Mode.Expanded) { SelectTab(TabTimer); return; }
        var p = wasFocus ? MakePopup("\uE708", UI.Purple, "Focus done", false) : MakePopup(ClockGlyph, UI.Orange, "Timer done", false);
        p.Value.Text = "";
        ShowPopup(p.Root, 240, PopH, 6000, true);
    }
}

static class TextBlockExtensions
{
    // Tabular digits so the countdown in the pill doesn't wobble.
    public static void FontFeatures(this TextBlock t) { System.Windows.Documents.Typography.SetNumeralAlignment(t, FontNumeralAlignment.Tabular); }
}

// ---------------------------------------------------------------- self test

static class SelfTest
{
    static T Wait<T>(Windows.Foundation.IAsyncOperation<T> op) { return op.AsTask().Result; }

    public static void Run(string outFile)
    {
        var r = new StringBuilder();
        Action<string> say = s => r.AppendLine(s);

        say("Dynamic Island self test, " + DateTime.Now);

        try
        {
            var mgr = Wait(MediaManager.RequestAsync());
            var s = mgr.GetCurrentSession();
            if (s == null) say("Media: no media session open right now (reading works, nothing is playing).");
            else
            {
                var p = Wait(s.TryGetMediaPropertiesAsync());
                say("Media: app=" + s.SourceAppUserModelId + " status=" + s.GetPlaybackInfo().PlaybackStatus + " title=\"" + p.Title + "\" artist=\"" + p.Artist + "\" artwork=" + (p.Thumbnail != null));
            }
            say("Media sessions open: " + mgr.GetSessions().Count);
        }
        catch (Exception ex) { say("Media: FAILED " + ex.Message); }

        try
        {
            var v = new Volume();
            if (!v.Init()) say("Volume: FAILED to open the default speakers");
            else
            {
                float before = v.Level;
                float step = before > 0.5f ? before - 0.02f : before + 0.02f;
                v.Level = step;
                Thread.Sleep(200);
                float stepped = v.Level;
                v.Level = before;
                Thread.Sleep(200);
                say(string.Format("Volume: was {0:0.000}, stepped to {1:0.000}, put back to {2:0.000}, muted={3}", before, stepped, v.Level, v.Muted));
            }
        }
        catch (Exception ex) { say("Volume: FAILED " + ex.Message); }

        try
        {
            int before = Brightness.Get();
            if (before < 0) say("Brightness: not available");
            else
            {
                int step = before >= 95 ? before - 5 : before + 5;
                Brightness.Set(step);
                Thread.Sleep(700);
                int stepped = Brightness.Get();
                Brightness.Set(before);
                Thread.Sleep(700);
                say("Brightness: was " + before + ", stepped to " + stepped + ", put back to " + Brightness.Get());
            }
        }
        catch (Exception ex) { say("Brightness: FAILED " + ex.Message); }

        try
        {
            say("Radio access: " + Wait(Radio.RequestAccessAsync()));
            foreach (var radio in Wait(Radio.GetRadiosAsync())) say("Radio: " + radio.Kind + " \"" + radio.Name + "\" " + radio.State);
        }
        catch (Exception ex) { say("Radios: FAILED " + ex.Message); }

        say("Night light: " + (NightLight.State() == 1 ? "on" : NightLight.State() == 0 ? "off" : "blob not recognised (button will open Settings)"));
        try
        {
            var l = Windows.UI.Notifications.Management.UserNotificationListener.Current;
            say("Notifications: access " + Wait(l.RequestAccessAsync()) + ", " + Wait(l.GetNotificationsAsync(Windows.UI.Notifications.NotificationKinds.Toast)).Count + " in the notification centre");
        }
        catch (Exception ex) { say("Notifications: FAILED " + ex.Message); }

        if (Environment.GetCommandLineArgs().Contains("--nightlight"))
        {
            // Flip it, read it back, then put the original bytes back exactly.
            var original = NightLight.Snapshot();
            int before = NightLight.State();
            bool ok = NightLight.Set(before != 1);
            Thread.Sleep(2500);
            int after = NightLight.State();
            NightLight.Restore(original);
            Thread.Sleep(1000);
            say("Night light toggle: was " + before + ", write ok=" + ok + ", read back " + after + ", restored to " + NightLight.State());
        }

        try
        {
            var sensors = new Sensors();
            sensors.Init();
            for (int i = 0; i < 3; i++)
            {
                Thread.Sleep(1000);
                double cpu, gpu, down, up;
                int temp;
                sensors.Sample(out cpu, out gpu, out temp, out down, out up);
                say(string.Format("Sensors: CPU {0:0.0}%  GPU {1:0.0}%  GPU temp {2}  network down {3} up {4}", cpu, gpu, temp < 0 ? "n/a" : temp + " C", Island.Speed(down), Island.Speed(up)));
            }
        }
        catch (Exception ex) { say("Sensors: FAILED " + ex.Message); }

        try
        {
            var report = Windows.Devices.Power.Battery.AggregateBattery.GetReport();
            Native.SYSTEM_POWER_STATUS ps;
            Native.GetSystemPowerStatus(out ps);
            say("Battery: " + ps.BatteryLifePercent + "% AC=" + ps.ACLineStatus + " WinRT status=" + report.Status);
        }
        catch (Exception ex) { say("Battery: FAILED " + ex.Message); }

        try
        {
            var pw = new PrivacyWatch();
            bool reported = false;
            pw.Changed += () => reported = true;
            pw.Start();
            Thread.Sleep(1500);
            Func<List<string>, string> show = l => l.Count == 0 ? "none" : string.Join(", ", l);
            say("Camera monitor: " + (pw.CameraMonitorRunning ? "running" : "NOT running") + (reported ? ", first report received" : ", no report yet")
                + "; camera in use: " + show(PrivacyWatch.LiveCamera()) + "; microphone in use: " + show(PrivacyWatch.LiveMicrophone())
                + "; registry says camera: " + show(PrivacyWatch.InUse("webcam")) + ", microphone: " + show(PrivacyWatch.InUse("microphone")));
        }
        catch (Exception ex) { say("Camera monitor: FAILED " + ex.Message); }

        File.WriteAllText(outFile, r.ToString());
    }
}
