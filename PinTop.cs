// PinTop v4 - tiny always-on-top pinner with tight instant marker
// Hotkeys: Ctrl+Space  OR  Ctrl+Alt+T  OR  Ctrl+F11
// Marker: thin 3px blue border flush with the window + small PIN badge.
// The frame follows via Windows move/size events (instant), with a 1s
// safety timer only for cleanup. Still ~0% CPU idle.
// If hotkey doesn't work: right-click tray icon > "Pin / unpin active window".
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Media;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

static class Program
{
    [STAThread]
    static void Main()
    {
        bool created;
        using (var mutex = new Mutex(true, "PinTop_SingleInstance_Mutex", out created))
        {
            if (!created)
            {
                MessageBox.Show("PinTop is already running (check system tray).",
                    "PinTop", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new PinTopForm());
        }
    }
}

// Thin click-through border drawn flush around a pinned window.
sealed class PinFrame : Form
{
    public IntPtr Target;
    static readonly Color FrameColor = Color.FromArgb(0, 120, 215);
    static readonly Color KeyColor = Color.Magenta;

    public PinFrame(IntPtr target)
    {
        Target = target;
        FormBorderStyle = FormBorderStyle.None;
        ShowInTaskbar = false;
        TopMost = true;
        StartPosition = FormStartPosition.Manual;
        BackColor = KeyColor;
        TransparencyKey = KeyColor;
        ControlBox = false;
        MaximizeBox = false;
        MinimizeBox = false;
        ShowIcon = false;
        Text = "";
        Width = 200;
        Height = 100;
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint |
                 ControlStyles.OptimizedDoubleBuffer, true);
    }

    protected override CreateParams CreateParams
    {
        get
        {
            CreateParams cp = base.CreateParams;
            const int WS_EX_TOOLWINDOW = 0x00000080;
            const int WS_EX_LAYERED = 0x00080000;
            const int WS_EX_TRANSPARENT = 0x00000020; // click-through
            const int WS_EX_NOACTIVATE = 0x08000000;
            cp.ExStyle |= WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
            return cp;
        }
    }

    protected override bool ShowWithoutActivation { get { return true; } }

    protected override void OnPaint(PaintEventArgs e)
    {
        Graphics g = e.Graphics;
        const int T = 3; // border thickness
        using (Brush b = new SolidBrush(FrameColor))
        {
            // full connected sides first (corners meet), badge painted on top
            g.FillRectangle(b, 0, 0, Width, T);                // top
            g.FillRectangle(b, 0, Height - T, Width, T);       // bottom
            g.FillRectangle(b, 0, 0, T, Height);               // left
            g.FillRectangle(b, Width - T, 0, T, Height);       // right
            g.FillRectangle(b, 0, 0, 74, 18);                  // badge
        }
        try
        {
            using (Font fe = new Font("Segoe UI Emoji", 8))
            using (Font ft = new Font("Segoe UI", 8, FontStyle.Bold))
            using (Brush tb = new SolidBrush(Color.White))
            {
                g.DrawString("\uD83D\uDCCC", fe, tb, 3, 0);
                g.DrawString("PIN", ft, tb, 22, 2);
            }
        }
        catch { }
    }
}

sealed class PinTopForm : Form
{
    const uint MOD_CONTROL = 0x0002;
    const uint MOD_ALT = 0x0001;
    const uint VK_SPACE = 0x20;
    const uint VK_T = 0x54;
    const uint VK_F11 = 0x7A;
    const int HK_CTRL_SPACE = 1;
    const int HK_CTRL_ALT_T = 2;
    const int HK_CTRL_F11 = 3;

    static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
    static readonly IntPtr HWND_NOTOPMOST = new IntPtr(-2);
    const uint SWP_NOMOVE = 0x0002;
    const uint SWP_NOSIZE = 0x0001;
    const uint SWP_SHOWWINDOW = 0x0040;
    const int GWL_EXSTYLE = -20;
    const long WS_EX_TOPMOST = 0x00000008L;
    const int WM_HOTKEY = 0x0312;
    const string PIN_PREFIX = "\uD83D\uDCCC "; // 📌

    // Window-event hook (instant follow, no polling)
    const uint EVENT_SYSTEM_MOVESIZESTART = 0x000A;
    const uint EVENT_SYSTEM_MOVESIZEEND = 0x000B;
    const uint EVENT_OBJECT_LOCATIONCHANGE = 0x800B;
    const uint WINEVENT_OUTOFCONTEXT = 0x0000;
    const int OBJID_WINDOW = 0;

    delegate void WinEventDelegate(IntPtr hWinEventHook, uint eventType, IntPtr hwnd,
        int idObject, int idChild, uint dwEventThread, uint dwmsEventTime);

    [StructLayout(LayoutKind.Sequential)]
    struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll", SetLastError = true)] static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);
    [DllImport("user32.dll")] static extern bool UnregisterHotKey(IntPtr hWnd, int id);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", SetLastError = true)] static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
    [DllImport("user32.dll", EntryPoint = "GetWindowLong", SetLastError = true)] static extern int GetWindowLong32(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtr", SetLastError = true)] static extern IntPtr GetWindowLongPtr64(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")] static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll", SetLastError = true)] static extern bool SetWindowText(IntPtr hWnd, string text);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll", CharSet = CharSet.Auto)] static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr hwnd, int dwAttribute, out RECT pvAttribute, int cbAttribute);
    const int DWMWA_EXTENDED_FRAME_BOUNDS = 9;

    // Tight visible bounds (excludes invisible drop-shadow that GetWindowRect includes).
    static bool GetTightRect(IntPtr hwnd, out RECT r)
    {
        try
        {
            if (DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, out r,
                    Marshal.SizeOf(typeof(RECT))) == 0 &&
                r.Right > r.Left && r.Bottom > r.Top)
                return true;
        }
        catch { }
        r = new RECT();
        return GetWindowRect(hwnd, out r);
    }
    [DllImport("user32.dll")] static extern IntPtr SetWinEventHook(uint eventMin, uint eventMax,
        IntPtr hmodWinEventProc, WinEventDelegate lpfnWinEventProc,
        uint idProcess, uint idThread, uint dwFlags);
    [DllImport("user32.dll")] static extern bool UnhookWinEvent(IntPtr hWinEventHook);

    static long GetExStyle(IntPtr hwnd)
    {
        if (IntPtr.Size == 8)
            return GetWindowLongPtr64(hwnd, GWL_EXSTYLE).ToInt64();
        return GetWindowLong32(hwnd, GWL_EXSTYLE);
    }

    readonly NotifyIcon tray;
    readonly HashSet<IntPtr> pinned = new HashSet<IntPtr>();
    readonly Dictionary<IntPtr, PinFrame> frames = new Dictionary<IntPtr, PinFrame>();
    readonly System.Windows.Forms.Timer followTimer;
    readonly ToolStripMenuItem pinnedMenu;
    readonly WinEventDelegate winEventProc; // kept alive: unhook before GC
    IntPtr hookMove = IntPtr.Zero;
    IntPtr hookLoc = IntPtr.Zero;
    int registeredCount;

    public PinTopForm()
    {
        ShowInTaskbar = false;
        WindowState = FormWindowState.Minimized;
        Opacity = 0;
        Load += (s, e) => Hide();

        winEventProc = new WinEventDelegate(WinEventProc);

        var menu = new ContextMenuStrip();
        menu.Items.Add(new ToolStripMenuItem("Pin / unpin active window", null, (s, e) => ToggleForeground()));
        pinnedMenu = new ToolStripMenuItem("Pinned windows");
        menu.Items.Add(pinnedMenu);
        menu.Items.Add(new ToolStripMenuItem("Show active window info (debug)", null, (s, e) => ShowActiveInfo()));
        menu.Items.Add(new ToolStripMenuItem("Unpin all", null, (s, e) => UnpinAll()));
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add(new ToolStripMenuItem("Exit", null, (s, e) => Application.Exit()));
        menu.Opening += (s, e) => RebuildPinnedMenu();

        tray = new NotifyIcon();
        tray.Icon = LoadAppIcon(); // exe's embedded icon, fallback: system icon
        tray.Text = "PinTop - Ctrl+Space or Ctrl+Alt+T to pin";
        tray.ContextMenuStrip = menu;
        tray.Visible = true;
        tray.DoubleClick += (s, e) => ToggleForeground();
        tray.BalloonTipTitle = "PinTop";

        followTimer = new System.Windows.Forms.Timer();
        followTimer.Interval = 1000; // safety net only; live follow comes from window events
        followTimer.Tick += (s, e) => FollowTick();
    }

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        registeredCount = 0;
        if (RegisterHotKey(Handle, HK_CTRL_SPACE, MOD_CONTROL, VK_SPACE)) registeredCount++;
        if (RegisterHotKey(Handle, HK_CTRL_ALT_T, MOD_CONTROL | MOD_ALT, VK_T)) registeredCount++;
        if (RegisterHotKey(Handle, HK_CTRL_F11, MOD_CONTROL, VK_F11)) registeredCount++;

        try
        {
            hookMove = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND,
                IntPtr.Zero, winEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
            hookLoc = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                IntPtr.Zero, winEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
        }
        catch { }

        if (registeredCount == 0)
        {
            MessageBox.Show("PinTop could not register ANY hotkey.\n\n" +
                "You can still pin via tray right-click > 'Pin / unpin active window'.",
                "PinTop - hotkey conflict", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        else
        {
            tray.ShowBalloonTip(3000, "PinTop running",
                "Click a window, then press Ctrl+Space or Ctrl+Alt+T.\nPinned windows get a thin blue PIN frame.",
                ToolTipIcon.Info);
        }
    }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == WM_HOTKEY)
        {
            int id = m.WParam.ToInt32();
            if (id == HK_CTRL_SPACE || id == HK_CTRL_ALT_T || id == HK_CTRL_F11)
            {
                ToggleForeground();
                return;
            }
        }
        base.WndProc(ref m);
    }

    // Fired by Windows the moment a window moves/resizes -> reposition frame instantly.
    void WinEventProc(IntPtr hHook, uint eventType, IntPtr hwnd,
        int idObject, int idChild, uint thread, uint time)
    {
        if (idObject != OBJID_WINDOW || hwnd == IntPtr.Zero) return;
        if (!pinned.Contains(hwnd)) return;
        try
        {
            BeginInvoke(new Action(delegate() { RepositionIfPinned(hwnd); }));
        }
        catch { }
    }

    void RepositionIfPinned(IntPtr hwnd)
    {
        PinFrame f;
        if (!frames.TryGetValue(hwnd, out f) || f == null || f.IsDisposed) return;
        if (!IsWindow(hwnd)) return;
        PositionFrame(hwnd, f);
        try { f.Invalidate(); } catch { }
    }

    void Notify(string msg, ToolTipIcon icon, bool pinnedNow)
    {
        try
        {
            tray.ShowBalloonTip(2000, "PinTop", msg, icon);
            if (pinnedNow) SystemSounds.Asterisk.Play();
            else SystemSounds.Exclamation.Play();
        }
        catch { }
        UpdateTrayText();
    }

    void UpdateTrayText()
    {
        try
        {
            string t = pinned.Count == 0
                ? "PinTop - Ctrl+Space or Ctrl+Alt+T to pin"
                : string.Format("PinTop - {0} window(s) pinned", pinned.Count);
            tray.Text = t.Length > 63 ? t.Substring(0, 63) : t;
        }
        catch { }
    }

    void RebuildPinnedMenu()
    {
        pinnedMenu.DropDownItems.Clear();
        if (pinned.Count == 0)
        {
            ToolStripMenuItem none = new ToolStripMenuItem("(none)") { Enabled = false };
            pinnedMenu.DropDownItems.Add(none);
            return;
        }
        foreach (IntPtr hwnd in new List<IntPtr>(pinned))
        {
            string title = Short(GetTitle(hwnd));
            IntPtr h = hwnd; // copy for closure
            ToolStripMenuItem item = new ToolStripMenuItem("📌 " + title, null, (s, e) => UnpinOne(h));
            pinnedMenu.DropDownItems.Add(item);
        }
    }

    void ShowActiveInfo()
    {
        IntPtr hwnd = GetForegroundWindow();
        if (hwnd == IntPtr.Zero)
        {
            MessageBox.Show("No foreground window found.", "PinTop debug");
            return;
        }
        string title = GetTitle(hwnd);
        string cls = GetClass(hwnd);
        bool isTop = (GetExStyle(hwnd) & WS_EX_TOPMOST) != 0;
        MessageBox.Show(
            "Title: " + title + "\nClass: " + cls +
            "\nHandle: 0x" + hwnd.ToInt64().ToString("X") +
            "\nTopmost now: " + (isTop ? "YES" : "NO"),
            "PinTop - active window", MessageBoxButtons.OK, MessageBoxIcon.Information);
    }

    void ToggleForeground()
    {
        IntPtr hwnd = GetForegroundWindow();
        if (hwnd == IntPtr.Zero || hwnd == Handle || !IsWindow(hwnd) || !IsWindowVisible(hwnd))
        {
            Notify("No pinnable window in front.", ToolTipIcon.Warning, false);
            return;
        }

        string title = GetTitle(hwnd);
        string cls = GetClass(hwnd);
        if (cls == "Progman" || cls == "WorkerW" || cls == "Shell_TrayWnd" || cls == "Shell_SecondaryTrayWnd")
        {
            Notify("That is the desktop/taskbar - click a real window first.", ToolTipIcon.Warning, false);
            return;
        }

        if (pinned.Contains(hwnd) || (GetExStyle(hwnd) & WS_EX_TOPMOST) != 0)
            UnpinOne(hwnd);
        else
            PinOne(hwnd);
    }

    void PinOne(IntPtr hwnd)
    {
        string title = GetTitle(hwnd);
        bool ok = SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        if (!ok)
        {
            int err = Marshal.GetLastWin32Error();
            MessageBox.Show("Pin failed (error " + err + ").\nTry running PinTop as administrator.",
                "PinTop", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        if ((GetExStyle(hwnd) & WS_EX_TOPMOST) == 0)
        {
            Notify("Windows refused to pin: " + Short(title), ToolTipIcon.Warning, false);
            return;
        }
        pinned.Add(hwnd);
        MarkTitle(hwnd, true);
        EnsureFrame(hwnd);
        if (!followTimer.Enabled) followTimer.Start();
        UpdateTrayText();
        Notify("Pinned on top: " + Short(StripPin(GetTitle(hwnd))), ToolTipIcon.Info, true);
    }

    void UnpinOne(IntPtr hwnd)
    {
        if (IsWindow(hwnd))
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        MarkTitle(hwnd, false); // strips 📌 prefix if present
        CloseFrame(hwnd);
        pinned.Remove(hwnd);
        if (pinned.Count == 0) followTimer.Stop();
        UpdateTrayText();
        Notify("Unpinned.", ToolTipIcon.Info, false);
    }

    void UnpinAll()
    {
        foreach (IntPtr hwnd in new List<IntPtr>(pinned))
        {
            if (IsWindow(hwnd))
                SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            MarkTitle(hwnd, false);
            CloseFrame(hwnd);
        }
        pinned.Clear();
        followTimer.Stop();
        IntPtr fg = GetForegroundWindow();
        if (fg != IntPtr.Zero && (GetExStyle(fg) & WS_EX_TOPMOST) != 0)
        {
            SetWindowPos(fg, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            MarkTitle(fg, false);
        }
        UpdateTrayText();
        Notify("All windows unpinned.", ToolTipIcon.Info, false);
    }

    // ---- marker helpers ----

    static string StripPin(string s)
    {
        if (!string.IsNullOrEmpty(s) && s.StartsWith(PIN_PREFIX))
            return s.Substring(PIN_PREFIX.Length);
        return s;
    }

    static void MarkTitle(IntPtr hwnd, bool pin)
    {
        try
        {
            string cur = GetTitle(hwnd);
            if (pin)
            {
                if (!cur.StartsWith(PIN_PREFIX))
                    SetWindowText(hwnd, PIN_PREFIX + cur);
            }
            else
            {
                if (cur.StartsWith(PIN_PREFIX))
                    SetWindowText(hwnd, cur.Substring(PIN_PREFIX.Length));
            }
        }
        catch { }
    }

    void EnsureFrame(IntPtr hwnd)
    {
        PinFrame f;
        if (frames.TryGetValue(hwnd, out f) && f != null && !f.IsDisposed)
            return;
        f = new PinFrame(hwnd);
        frames[hwnd] = f;
        PositionFrame(hwnd, f);
        f.Show();
    }

    void CloseFrame(IntPtr hwnd)
    {
        PinFrame f;
        if (frames.TryGetValue(hwnd, out f))
        {
            frames.Remove(hwnd);
            try { if (f != null && !f.IsDisposed) f.Close(); } catch { }
            try { if (f != null) f.Dispose(); } catch { }
        }
    }

    static void PositionFrame(IntPtr hwnd, PinFrame f)
    {
        RECT r;
        if (!GetTightRect(hwnd, out r)) return;
        // flush with the visible window edge: zero margin
        f.Bounds = new Rectangle(r.Left, r.Top, r.Right - r.Left, r.Bottom - r.Top);
    }

    // Safety net only (closed windows, externally-removed topmost). Live follow is event-driven.
    void FollowTick()
    {
        if (pinned.Count == 0) { followTimer.Stop(); return; }
        foreach (IntPtr hwnd in new List<IntPtr>(pinned))
        {
            if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd))
            {
                PinFrame hf;
                if (frames.TryGetValue(hwnd, out hf) && hf != null && !hf.IsDisposed)
                    try { hf.Hide(); } catch { }
                if (!IsWindow(hwnd))
                {
                    CloseFrame(hwnd);
                    pinned.Remove(hwnd);
                }
                continue;
            }
            if ((GetExStyle(hwnd) & WS_EX_TOPMOST) == 0)
            {
                MarkTitle(hwnd, false);
                CloseFrame(hwnd);
                pinned.Remove(hwnd);
                UpdateTrayText();
                continue;
            }
            PinFrame f;
            if (!frames.TryGetValue(hwnd, out f) || f == null || f.IsDisposed)
            {
                EnsureFrame(hwnd);
                continue;
            }
            if (!f.Visible) try { f.Show(); } catch { }
            RECT r;
            if (!GetTightRect(hwnd, out r)) continue;
            Rectangle want = new Rectangle(r.Left, r.Top, r.Right - r.Left, r.Bottom - r.Top);
            if (f.Bounds != want) f.Bounds = want;
        }
        if (pinned.Count == 0) followTimer.Stop();
    }

    static Icon LoadAppIcon()
    {
        try
        {
            Icon ic = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            if (ic != null) return ic;
        }
        catch { }
        return SystemIcons.Application;
    }

    static string Short(string s)
    {
        if (string.IsNullOrEmpty(s)) return "(untitled window)";
        return s.Length > 60 ? s.Substring(0, 60) + "..." : s;
    }

    static string GetTitle(IntPtr hwnd)
    {
        try { var sb = new StringBuilder(512); GetWindowText(hwnd, sb, sb.Capacity); return sb.ToString(); }
        catch { return ""; }
    }

    static string GetClass(IntPtr hwnd)
    {
        try { var sb = new StringBuilder(256); GetClassName(hwnd, sb, sb.Capacity); return sb.ToString(); }
        catch { return ""; }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            try { followTimer.Stop(); } catch { }
            try { if (hookMove != IntPtr.Zero) UnhookWinEvent(hookMove); } catch { }
            try { if (hookLoc != IntPtr.Zero) UnhookWinEvent(hookLoc); } catch { }
            foreach (IntPtr hwnd in new List<IntPtr>(pinned))
            {
                try { MarkTitle(hwnd, false); } catch { }
                CloseFrame(hwnd);
            }
            try { UnregisterHotKey(Handle, HK_CTRL_SPACE); } catch { }
            try { UnregisterHotKey(Handle, HK_CTRL_ALT_T); } catch { }
            try { UnregisterHotKey(Handle, HK_CTRL_F11); } catch { }
            if (tray != null) { tray.Visible = false; tray.Dispose(); }
        }
        base.Dispose(disposing);
    }
}
