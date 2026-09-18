/* FloatKeys - floating virtual keyboard (native Win32 C, no runtime)
 *
 * Like the Windows touch keyboard, but WITH Shift/Ctrl/Alt/Win/Tab/Caps/Esc/F-keys.
 * Single HWND, owner-drawn with GDI. ~5MB RAM, ~100KB exe, no dependencies.
 *
 * Build with Zig (recommended, ~50MB toolchain, no admin):
 *   zig cc -O2 -mwindows -o FloatKeys.exe keyboard.c -luser32 -lgdi32 -lshcore
 * Build with MSVC:
 *   cl /O2 keyboard.c user32.lib gdi32.lib shcore.lib /link /SUBSYSTEM:WINDOWS
 */
#include <windows.h>
#include <windowsx.h>
#include <wchar.h>
#include <ctype.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#endif

/* ---------------- SendInput helpers ---------------- */
#define EXTKEY(k) ((k)==VK_LEFT||(k)==VK_UP||(k)==VK_RIGHT||(k)==VK_DOWN|| \
                   (k)==VK_LWIN||(k)==VK_APPS||(k)==VK_DELETE||(k)==VK_SNAPSHOT|| \
                   (k)==VK_HOME||(k)==VK_END||(k)==VK_PRIOR||(k)==VK_NEXT|| \
                   (k)==VK_INSERT)

static void vk_down(UINT vk) {
    INPUT in_ = {0};
    in_.type = INPUT_KEYBOARD;
    in_.ki.wVk = (WORD)vk;
    if (EXTKEY(vk)) in_.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &in_, sizeof(INPUT));
}
static void vk_up(UINT vk) {
    INPUT in_ = {0};
    in_.type = INPUT_KEYBOARD;
    in_.ki.wVk = (WORD)vk;
    in_.ki.dwFlags = KEYEVENTF_KEYUP | (EXTKEY(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in_, sizeof(INPUT));
}
static void vk_tap(UINT vk) { vk_down(vk); vk_up(vk); }

static void type_unicode(const wchar_t *s) {
    for (; *s; s++) {
        INPUT dn = {0}, up = {0};
        dn.type = INPUT_KEYBOARD; up.type = INPUT_KEYBOARD;
        dn.ki.wScan = *s; dn.ki.dwFlags = KEYEVENTF_UNICODE;
        up.ki.wScan = *s; up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(1, &dn, sizeof(INPUT));
        SendInput(1, &up, sizeof(INPUT));
    }
}

static UINT char_to_vk(wchar_t lo) {
    if ((lo >= L'a' && lo <= L'z') || (lo >= L'A' && lo <= L'Z'))
        return (UINT)towupper(lo);
    if (lo >= L'0' && lo <= L'9') return (UINT)lo;
    switch (lo) {
        case L';': return VK_OEM_1;    /* ;: */
        case L'=': return VK_OEM_PLUS; /* =+ */
        case L',': return VK_OEM_COMMA;
        case L'-': return VK_OEM_MINUS;
        case L'.': return VK_OEM_PERIOD;
        case L'/': return VK_OEM_2;    /* /? */
        case L'`': return VK_OEM_3;
        case L'[': return VK_OEM_4;
        case L'\\': return VK_OEM_5;
        case L']': return VK_OEM_6;
        case L'\'': return VK_OEM_7;
        default: return 0;
    }
}

/* ---------------- model ---------------- */
typedef enum { K_CHAR, K_SPECIAL, K_MOD, K_CAPS, K_SPACE } Kind;
enum { M_OFF = 0, M_HELD = 1, M_LOCKED = 2 };
enum { MX_SHIFT = 0, MX_CTRL = 1, MX_ALT = 2, MX_WIN = 3 };

typedef struct {
    wchar_t text[10];
    wchar_t sub[4];      /* small shifted hint for symbol keys */
    Kind kind;
    UINT vk;             /* SPECIAL vk */
    int mod;             /* MOD_* for K_MOD */
    wchar_t lo, hi;      /* K_CHAR pair */
    float w;
    int row;             /* 0=fn 1=num 2=top 3=home 4=shift 5=bottom */
    RECT rc;
} Key;

#define MAXKEYS 128
static Key g_keys[MAXKEYS];
static int g_nkeys = 0;

static HINSTANCE g_hInst;
static HWND g_hwnd;
static float g_scale = 1.0f;
static int g_opacity = 245;             /* 0..255 */
static BOOL g_showFn = TRUE;
static int g_mods[4] = { M_OFF, M_OFF, M_OFF, M_OFF };
static int g_capsLast = -1;
static const UINT MODVK[4] = { VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN };
static int g_pressed = -1;
static int g_downIdx = -2;              /* key or bar btn held */
static BOOL g_dragging = FALSE;
static POINT g_dragOff;
/* repeat */
static int g_repeatIdx = -1;
static UINT g_repeatVk = 0;
static BOOL g_repeatFirst = FALSE;
/* fonts */
static HFONT g_fKey = NULL, g_fSmall = NULL, g_fBar = NULL;

#define TIMER_CAPS 1
#define TIMER_REPEAT 2
#define TIMER_FLASH 3

/* bar buttons */
enum { BAR_NONE=0, BAR_FN=1, BAR_SM, BAR_BG, BAR_OPDN, BAR_OPUP, BAR_X };
typedef struct { int id; const wchar_t *t; RECT rc; int w; } BarBtn;
static BarBtn g_bar[6] = {
    { BAR_FN,   L"Fn",  {0}, 40 },
    { BAR_SM,   L"A-",  {0}, 36 },
    { BAR_BG,   L"A+",  {0}, 36 },
    { BAR_OPDN, L"\u25D0", {0}, 34 },
    { BAR_OPUP, L"\u25D1", {0}, 34 },
    { BAR_X,    L"\u2715", {0}, 40 },
};

/* colors */
#define C_BG      RGB(30,30,30)
#define C_BAR     RGB(37,37,38)
#define C_KEY     RGB(45,45,45)
#define C_SPEC    RGB(51,51,56)
#define C_ACTIVE  RGB(74,74,74)
#define C_HELD    RGB(76,194,255)
#define C_LOCK    RGB(0,120,212)
#define C_TXT     RGB(255,255,255)
#define C_DIM     RGB(128,128,128)

static void AddKey(const wchar_t *t, Kind k, UINT vk, int mod,
                   wchar_t lo, wchar_t hi, float w, int row) {
    if (g_nkeys >= MAXKEYS) return;
    Key *c = &g_keys[g_nkeys++];
    wcsncpy(c->text, t, 9); c->text[9] = 0;
    c->sub[0] = 0;
    c->kind = k; c->vk = vk; c->mod = mod;
    c->lo = lo; c->hi = hi; c->w = w; c->row = row;
    SetRectEmpty(&c->rc);
}

static void BuildKeys(void) {
    int i;
    g_nkeys = 0;
    /* fn row */
    AddKey(L"Esc", K_SPECIAL, VK_ESCAPE, 0, 0,0, 1.2f, 0);
    for (i = 1; i <= 12; i++) {
        wchar_t b[8]; wsprintfW(b, L"F%d", i);
        AddKey(b, K_SPECIAL, (UINT)(VK_F1 + i - 1), 0, 0,0, 1.0f, 0);
    }
    AddKey(L"PrtSc", K_SPECIAL, VK_SNAPSHOT, 0, 0,0, 1.2f, 0);
    AddKey(L"Del", K_SPECIAL, VK_DELETE, 0, 0,0, 1.2f, 0);
    /* number row */
    AddKey(L"`", K_CHAR, 0,0, L'`', L'~', 1.0f, 1);
    {
        const wchar_t *d = L"1234567890", *s = L"!@#$%^&*()";
        for (i = 0; i < 10; i++) {
            wchar_t t[2] = { d[i], 0 };
            AddKey(t, K_CHAR, 0,0, d[i], s[i], 1.0f, 1);
        }
    }
    AddKey(L"-", K_CHAR, 0,0, L'-', L'_', 1.0f, 1);
    AddKey(L"=", K_CHAR, 0,0, L'=', L'+', 1.0f, 1);
    AddKey(L"Back", K_SPECIAL, VK_BACK, 0, 0,0, 2.0f, 1);
    /* top */
    AddKey(L"Tab", K_SPECIAL, VK_TAB, 0, 0,0, 1.6f, 2);
    {
        const wchar_t *r = L"QWERTYUIOP";
        for (i = 0; r[i]; i++) {
            wchar_t t[2] = { r[i], 0 };
            AddKey(t, K_CHAR, 0,0, (wchar_t)towlower(r[i]), r[i], 1.0f, 2);
        }
    }
    AddKey(L"[", K_CHAR, 0,0, L'[', L'{', 1.0f, 2);
    AddKey(L"]", K_CHAR, 0,0, L']', L'}', 1.0f, 2);
    AddKey(L"\\", K_CHAR, 0,0, L'\\', L'|', 1.5f, 2);
    /* home */
    AddKey(L"Caps", K_CAPS, 0,0, 0,0, 1.8f, 3);
    {
        const wchar_t *r = L"ASDFGHJKL";
        for (i = 0; r[i]; i++) {
            wchar_t t[2] = { r[i], 0 };
            AddKey(t, K_CHAR, 0,0, (wchar_t)towlower(r[i]), r[i], 1.0f, 3);
        }
    }
    AddKey(L";", K_CHAR, 0,0, L';', L':', 1.0f, 3);
    AddKey(L"'", K_CHAR, 0,0, L'\'', L'"', 1.0f, 3);
    AddKey(L"Enter", K_SPECIAL, VK_RETURN, 0, 0,0, 2.2f, 3);
    /* shift row */
    AddKey(L"Shift", K_MOD, 0, MX_SHIFT, 0,0, 2.2f, 4);
    {
        const wchar_t *r = L"ZXCVBNM";
        for (i = 0; r[i]; i++) {
            wchar_t t[2] = { r[i], 0 };
            AddKey(t, K_CHAR, 0,0, (wchar_t)towlower(r[i]), r[i], 1.0f, 4);
        }
    }
    AddKey(L",", K_CHAR, 0,0, L',', L'<', 1.0f, 4);
    AddKey(L".", K_CHAR, 0,0, L'.', L'>', 1.0f, 4);
    AddKey(L"/", K_CHAR, 0,0, L'/', L'?', 1.0f, 4);
    AddKey(L"Shift", K_MOD, 0, MX_SHIFT, 0,0, 2.2f, 4);
    AddKey(L"\u25B2", K_SPECIAL, VK_UP, 0, 0,0, 1.0f, 4);
    /* bottom */
    AddKey(L"Ctrl", K_MOD, 0, MX_CTRL, 0,0, 1.5f, 5);
    AddKey(L"Win", K_MOD, 0, MX_WIN, 0,0, 1.5f, 5);
    AddKey(L"Alt", K_MOD, 0, MX_ALT, 0,0, 1.5f, 5);
    AddKey(L"Space", K_SPACE, 0,0, 0,0, 6.0f, 5);
    AddKey(L"Alt", K_MOD, 0, MX_ALT, 0,0, 1.3f, 5);
    AddKey(L"\u2630", K_SPECIAL, VK_APPS, 0, 0,0, 1.3f, 5);
    AddKey(L"Ctrl", K_MOD, 0, MX_CTRL, 0,0, 1.3f, 5);
    AddKey(L"\u25C0", K_SPECIAL, VK_LEFT, 0, 0,0, 1.0f, 5);
    AddKey(L"\u25BC", K_SPECIAL, VK_DOWN, 0, 0,0, 1.0f, 5);
    AddKey(L"\u25B6", K_SPECIAL, VK_RIGHT, 0, 0,0, 1.0f, 5);
}

/* ---------------- state logic ---------------- */
/* repaint helpers (defined in layout section; change-only to avoid flicker) */
static void InvKey(int idx);
static void InvMod(int m);
static void InvCaps(void);
static void InvBar(int id);
static BOOL ComboActive(void) {
    return g_mods[MX_CTRL] != M_OFF || g_mods[MX_ALT] != M_OFF || g_mods[MX_WIN] != M_OFF;
}
static void ConsumeHeld(void) {
    int i;
    for (i = 0; i < 4; i++)
        if (g_mods[i] == M_HELD) { vk_up(MODVK[i]); g_mods[i] = M_OFF; InvMod(i); }
}
static BOOL ShiftedFor(wchar_t lo) {
    BOOL caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
    BOOL sh = g_mods[MX_SHIFT] != M_OFF;
    if ((lo >= L'a' && lo <= L'z') || (lo >= L'A' && lo <= L'Z'))
        return caps != sh;
    return sh;
}
static void EmitText(const wchar_t *s) {
    int ss = g_mods[MX_SHIFT];
    BOOL sdn = ss != M_OFF, combo = ComboActive();
    if (sdn && !combo) vk_up(VK_SHIFT);
    type_unicode(s);
    if (sdn && !combo) {
        if (ss == M_LOCKED) vk_down(VK_SHIFT);
        else if (g_mods[MX_SHIFT] != M_OFF) { g_mods[MX_SHIFT] = M_OFF; InvMod(MX_SHIFT); }
    }
    ConsumeHeld();
}
static void EmitVk(UINT vk) { vk_tap(vk); ConsumeHeld(); }

static void ToggleMod(int m) {
    if (g_mods[m] == M_OFF) { g_mods[m] = M_HELD; vk_down(MODVK[m]); }
    else if (g_mods[m] == M_HELD) g_mods[m] = M_LOCKED;
    else { g_mods[m] = M_OFF; vk_up(MODVK[m]); }
    InvMod(m);
}

static BOOL IsRepeatable(UINT vk) {
    return vk==VK_BACK||vk==VK_DELETE||vk==VK_SPACE||
           vk==VK_LEFT||vk==VK_UP||vk==VK_DOWN||vk==VK_RIGHT;
}

static void PressKey(int idx) {
    Key *k;
    wchar_t buf[2];
    if (idx < 0 || idx >= g_nkeys) return;
    k = &g_keys[idx];
    g_pressed = idx;
    SetTimer(g_hwnd, TIMER_FLASH, 80, NULL);
    InvKey(idx);
    if (k->kind == K_MOD) { ToggleMod(k->mod); return; }
    if (k->kind == K_CAPS) { vk_tap(VK_CAPITAL); InvCaps(); return; }
    if (k->kind == K_SPACE) {
        if (ComboActive()) {
            g_repeatIdx = idx; g_repeatVk = VK_SPACE; g_repeatFirst = TRUE;
            SetTimer(g_hwnd, TIMER_REPEAT, 450, NULL);
            EmitVk(VK_SPACE);
        } else {
            buf[0]=L' '; buf[1]=0; EmitText(buf);
        }
        return;
    }
    if (k->kind == K_CHAR) {
        wchar_t ch = ShiftedFor(k->lo) ? k->hi : k->lo;
        if (ComboActive()) {
            UINT vk = char_to_vk(k->lo);
            if (vk) EmitVk(vk); else { buf[0]=ch; buf[1]=0; EmitText(buf); }
        } else { buf[0]=ch; buf[1]=0; EmitText(buf); }
        return;
    }
    if (k->kind == K_SPECIAL) {
        if (IsRepeatable(k->vk)) {
            g_repeatIdx = idx; g_repeatVk = k->vk; g_repeatFirst = TRUE;
            SetTimer(g_hwnd, TIMER_REPEAT, 450, NULL);
        }
        EmitVk(k->vk);
        return;
    }
}

/* ---------------- layout & paint ---------------- */
static void RebuildFonts(void) {
    if (g_fKey) DeleteObject(g_fKey);
    if (g_fSmall) DeleteObject(g_fSmall);
    if (g_fBar) DeleteObject(g_fBar);
    g_fKey = CreateFontW(-(int)(16*g_scale),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    g_fSmall = CreateFontW(-(int)(10*g_scale),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    g_fBar = CreateFontW(-(int)(13*g_scale),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
}

static int BarH(void) { return (int)(30*g_scale); }

static void ComputeLayout(int cw, int ch) {
    int pad = 6, gap = 4, bh = BarH(), hintH = (int)(20*g_scale);
    int rows[6], nrows = 0, r, i;
    int y, avail, rh, x;
    /* bar buttons right-aligned */
    x = cw - pad;
    for (i = 5; i >= 0; i--) {
        int w = (int)(g_bar[i].w * g_scale);
        g_bar[i].rc.right = x; g_bar[i].rc.left = x - w;
        g_bar[i].rc.top = 3; g_bar[i].rc.bottom = bh - 3;
        x -= w + 3;
    }
    rows[0]=0;
    nrows = g_showFn ? 6 : 5;
    for (r = 0; r < nrows; r++) rows[r] = g_showFn ? r : r + 1;
    avail = ch - bh - pad*2 - hintH - gap*(nrows-1);
    rh = nrows ? avail / nrows : 0;
    y = bh + pad;
    for (r = 0; r < nrows; r++) {
        int row = rows[r], n = 0, ci;
        float tot = 0;
        for (i = 0; i < g_nkeys; i++)
            if (g_keys[i].row == row) { tot += g_keys[i].w; n++; }
        x = pad;
        ci = 0;
        for (i = 0; i < g_nkeys; i++) if (g_keys[i].row == row) {
            int wpx = (int)((cw - pad*2 - gap*(n-1)) * (g_keys[i].w / tot));
            if (++ci == n) wpx = cw - pad - x; /* absorb rounding */
            g_keys[i].rc.left = x; g_keys[i].rc.top = y;
            g_keys[i].rc.right = x + wpx; g_keys[i].rc.bottom = y + rh;
            x += wpx + gap;
        }
        y += rh + gap;
    }
    /* hide fn rects when hidden */
    if (!g_showFn)
        for (i = 0; i < g_nkeys; i++)
            if (g_keys[i].row == 0) SetRectEmpty(&g_keys[i].rc);
}

static int VisibleRow(int row) {
    if (row == 0 && !g_showFn) return 0;
    return 1;
}

/* change-only repaints: never invalidate the whole window on a timer */
static void InvKey(int idx) {
    if (idx >= 0 && idx < g_nkeys && VisibleRow(g_keys[idx].row))
        InvalidateRect(g_hwnd, &g_keys[idx].rc, FALSE);
}
static void InvMod(int m) {
    int i;
    for (i = 0; i < g_nkeys; i++)
        if (g_keys[i].kind == K_MOD && g_keys[i].mod == m) InvKey(i);
}
static void InvCaps(void) {
    int i;
    for (i = 0; i < g_nkeys; i++)
        if (g_keys[i].kind == K_CAPS) InvKey(i);
}
static void InvBar(int id) {
    int i;
    for (i = 0; i < 6; i++)
        if (g_bar[i].id == id) InvalidateRect(g_hwnd, &g_bar[i].rc, FALSE);
}

static void FillRR(HDC dc, RECT *rc, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    HPEN p = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, rc->left, rc->top, rc->right, rc->bottom, 8, 8);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);
}

static void PaintKey(HDC dc, Key *k, int idx) {
    COLORREF bg = C_KEY, fg = C_TXT;
    BOOL capsOn;
    if (k->kind == K_SPECIAL || k->kind == K_CAPS || k->kind == K_MOD) bg = C_SPEC;
    if (k->kind == K_MOD) {
        if (g_mods[k->mod] == M_HELD) { bg = C_HELD; fg = RGB(0,0,0); }
        else if (g_mods[k->mod] == M_LOCKED) { bg = C_LOCK; fg = C_TXT; }
    }
    if (k->kind == K_CAPS) {
        capsOn = (GetKeyState(VK_CAPITAL) & 1) != 0;
        if (capsOn) { bg = C_LOCK; fg = C_TXT; }
    }
    if (idx == g_pressed) bg = C_ACTIVE;
    FillRR(dc, &k->rc, bg);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    if (k->kind == K_CHAR && !(((k->lo>=L'a'&&k->lo<=L'z')||(k->lo>=L'A'&&k->lo<=L'Z')))) {
        /* symbol: small shift-hint top, main char center */
        RECT r = k->rc;
        SelectObject(dc, g_fSmall);
        SetTextColor(dc, C_DIM);
        if (g_mods[MX_SHIFT] != M_OFF) SetTextColor(dc, fg);
        DrawTextW(dc, &k->hi, 1, &r, DT_TOP|DT_CENTER|DT_SINGLELINE);
        r.top += (int)(10*g_scale);
        SelectObject(dc, g_fKey);
        SetTextColor(dc, fg);
        DrawTextW(dc, &k->lo, 1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    } else {
        SelectObject(dc, g_fKey);
        DrawTextW(dc, k->text, -1, &k->rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
}

static void OnPaint(void) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(g_hwnd, &ps);
    RECT cl; int i;
    GetClientRect(g_hwnd, &cl);
    {
        HBRUSH b = CreateSolidBrush(C_BG);
        FillRect(dc, &cl, b);
        DeleteObject(b);
    }
    /* bar */
    {
        RECT bar = {0,0,cl.right,BarH()};
        HBRUSH b = CreateSolidBrush(C_BAR);
        FillRect(dc, &bar, b);
        DeleteObject(b);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(204,204,204));
        SelectObject(dc, g_fBar);
        {
            RECT t = {8,0,cl.right,BarH()};
            DrawTextW(dc, L"\u283F floatkeys \u2014 drag to move", -1, &t,
                      DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        }
        for (i = 0; i < 6; i++) {
            FillRR(dc, &g_bar[i].rc, (g_bar[i].id==g_downIdx-100)?C_ACTIVE:C_SPEC);
            SetTextColor(dc, C_TXT);
            SelectObject(dc, g_fBar);
            DrawTextW(dc, g_bar[i].t, -1, &g_bar[i].rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
    }
    for (i = 0; i < g_nkeys; i++) {
        if (!VisibleRow(g_keys[i].row)) continue;
        if (IsRectEmpty(&g_keys[i].rc)) continue;
        PaintKey(dc, &g_keys[i], i);
    }
    /* hint */
    {
        RECT h = {6, cl.bottom - (int)(20*g_scale), cl.right-6, cl.bottom-2};
        SetTextColor(dc, RGB(128,128,128));
        SelectObject(dc, g_fSmall);
        DrawTextW(dc, L"Tap Shift/Ctrl/Alt/Win once = hold next key \u2022 twice = lock \u2022 e.g. Ctrl+C, Win+L, Alt+Tab",
                  -1, &h, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    EndPaint(g_hwnd, &ps);
}

static int HitBar(int x, int y) {
    int i;
    for (i = 0; i < 6; i++)
        if (PtInRect(&g_bar[i].rc, (POINT){x,y})) return g_bar[i].id;
    return BAR_NONE;
}
static int HitKey(int x, int y) {
    int i;
    POINT p = {x,y};
    for (i = 0; i < g_nkeys; i++) {
        if (!VisibleRow(g_keys[i].row)) continue;
        if (PtInRect(&g_keys[i].rc, p)) return i;
    }
    return -1;
}

/* ---------------- chrome actions ---------------- */
static void ApplyOpacity(void) {
    SetLayeredWindowAttributes(g_hwnd, 0, (BYTE)g_opacity, LWA_ALPHA);
}
static void ResizeForScale(void) {
    int w = (int)(880*g_scale);
    int h = g_showFn ? (int)(392*g_scale) : (int)(340*g_scale);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0,0, w,h,
        SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RebuildFonts();
    {
        RECT cl; GetClientRect(g_hwnd,&cl);
        ComputeLayout(cl.right, cl.bottom);
    }
    InvalidateRect(g_hwnd, NULL, TRUE);
}
static void DoBar(int id) {
    switch (id) {
        case BAR_X: DestroyWindow(g_hwnd); break;
        case BAR_FN:
            g_showFn = !g_showFn;
            ResizeForScale();
            break;
        case BAR_SM:
            if (g_scale > 0.75f) { g_scale -= 0.1f; ResizeForScale(); }
            break;
        case BAR_BG:
            if (g_scale < 1.5f) { g_scale += 0.1f; ResizeForScale(); }
            break;
        case BAR_OPDN:
            g_opacity -= 13; if (g_opacity < 102) g_opacity = 102;
            ApplyOpacity(); break;
        case BAR_OPUP:
            g_opacity += 13; if (g_opacity > 255) g_opacity = 255;
            ApplyOpacity(); break;
    }
}

/* ---------------- window proc ---------------- */
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCACTIVATE: return TRUE;
        case WM_CREATE:
            g_capsLast = (GetKeyState(VK_CAPITAL) & 1) != 0;
            SetTimer(h, TIMER_CAPS, 500, NULL);
            return 0;
        case WM_SIZE: {
            int cw = LOWORD(l), ch = HIWORD(l);
            ComputeLayout(cw, ch);
            return 0;
        }
        case WM_PAINT: OnPaint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
            int b = HitBar(x, y);
            SetCapture(h);
            if (b != BAR_NONE) { g_downIdx = 100 + b; InvBar(b); }
            else {
                int k = HitKey(x, y);
                if (k >= 0) { g_downIdx = k; PressKey(k); }
                else if (y < BarH()) {
                    POINT pt; GetCursorPos(&pt);
                    RECT wr; GetWindowRect(h, &wr);
                    g_dragging = TRUE;
                    g_dragOff.x = pt.x - wr.left; g_dragOff.y = pt.y - wr.top;
                    g_downIdx = -3;
                } else g_downIdx = -2;
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (g_dragging) {
                POINT pt; GetCursorPos(&pt);
                SetWindowPos(h, HWND_TOPMOST,
                    pt.x - g_dragOff.x, pt.y - g_dragOff.y, 0,0,
                    SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            }
            return 0;
        case WM_LBUTTONUP: {
            if (g_downIdx >= 100) {
                int id = g_downIdx - 100;
                int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
                g_downIdx = -2;
                InvBar(id);
                if (HitBar(x,y) == id) DoBar(id);
            } else g_downIdx = -2;
            g_dragging = FALSE;
            ReleaseCapture();
            KillTimer(h, TIMER_REPEAT);
            g_repeatIdx = -1;
            return 0;
        }
        case WM_TIMER:
            if (w == TIMER_CAPS) {
                /* repaint Caps key ONLY when its real state changed */
                int cur = (GetKeyState(VK_CAPITAL) & 1) != 0;
                if (cur != g_capsLast) { g_capsLast = cur; InvCaps(); }
            }
            else if (w == TIMER_REPEAT) {
                if (g_repeatIdx >= 0) {
                    vk_tap(g_repeatVk);
                    if (g_repeatFirst) {
                        g_repeatFirst = FALSE;
                        KillTimer(h, TIMER_REPEAT);
                        SetTimer(h, TIMER_REPEAT, 60, NULL);
                    }
                }
            }
            else if (w == TIMER_FLASH) { KillTimer(h, TIMER_FLASH); { int pi = g_pressed; g_pressed = -1; InvKey(pi); } }
            return 0;
        case WM_DESTROY: {
            int i;
            for (i = 0; i < 4; i++)
                if (g_mods[i] != M_OFF) vk_up(MODVK[i]);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

/* ---------------- entry ---------------- */
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    WNDCLASSEXW wc = {0};
    /* DPI awareness first */
    {
        HMODULE sh = LoadLibraryW(L"shcore.dll");
        if (sh) {
            FARPROC f = GetProcAddress(sh, "SetProcessDpiAwareness");
            if (f) ((HRESULT(WINAPI*)(int))f)(1);
            FreeLibrary(sh);
        } else SetProcessDPIAware();
    }
    g_hInst = hi;
    BuildKeys();
    RebuildFonts();
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"FloatKeys";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    /* window/taskbar icon: use embedded exe icon (keyboard.rc ID 1) */
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.cbSize = sizeof(wc);
    wc.hbrBackground = CreateSolidBrush(C_BG);
    RegisterClassExW(&wc);
    {
        int sw = GetSystemMetrics(SM_CXSCREEN), sh2 = GetSystemMetrics(SM_CYSCREEN);
        int w = (int)(880*g_scale), h2 = (int)(392*g_scale);
        g_hwnd = CreateWindowExW(
            WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_COMPOSITED,
            L"FloatKeys", L"FloatKeys",
            WS_POPUP|WS_VISIBLE,
            sw - w - 40, sh2 - h2 - 80, w, h2,
            NULL, NULL, hi, NULL);
        if (!g_hwnd) return 1;
        ApplyOpacity();
        {
            RECT cl; GetClientRect(g_hwnd,&cl);
            ComputeLayout(cl.right, cl.bottom);
        }
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(g_hwnd);
    }
    {
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
