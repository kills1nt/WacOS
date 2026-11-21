param([string]$Match = '.', [switch]$All)
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices; using System.Collections.Generic;
public class WE {
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP f, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EP f, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint c);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int l,t,r,b; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
  public static string D(IntPtr h){ var c=new StringBuilder(256); GetClassName(h,c,256); var t=new StringBuilder(256); GetWindowText(h,t,256); R r; GetWindowRect(h,out r); uint p; GetWindowThreadProcessId(h,out p);
    string pn=""; try{pn=System.Diagnostics.Process.GetProcessById((int)p).ProcessName;}catch{}
    return String.Format("{0:X} [{1}] '{2}' vis={3} {4},{5}-{6},{7} {8} st={9:X} ex={10:X} owner={11:X} parent={12:X}", h.ToInt64(), c, t, IsWindowVisible(h), r.l,r.t,r.r,r.b,pn, GetWindowLong(h,-16), GetWindowLong(h,-20), GetWindow(h,4).ToInt64(), GetParent(h).ToInt64()); }
  public static List<string> All(){ var o=new List<string>(); int z=0; EnumWindows((h,l)=>{ z++; o.Add("z"+z+" "+D(h)); EnumChildWindows(h,(k,m)=>{ o.Add("     child "+D(k)); return true;},IntPtr.Zero); return true;},IntPtr.Zero); return o; }
}
'@
[WE]::All() | Where-Object { $_ -match $Match -and ($All -or $_ -match 'vis=True') }
