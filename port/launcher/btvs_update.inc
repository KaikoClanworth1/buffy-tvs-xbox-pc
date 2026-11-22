/* Updates: the newest release on GitHub. Part of btvs_launcher.c.
 * Modelled on the Chaos Bleeds launcher's launcher_update.inc; the release
 * conventions are the GitHub area's (docs/github.md, "Release conventions").
 *
 * CHECK  When the window opens (launcher.ini [Launcher] CheckUpdates=0 turns
 *        that off) and on "Check for updates", a thread asks GitHub for the
 *        releases and takes the newest version that is published (no draft,
 *        no pre-release) and has a file ending in -Windows-x64.zip. Nothing
 *        about the player or the PC is sent: one GET with a generic
 *        User-Agent. No answer, 404 (the repository is private today) or
 *        anything unreadable means "no update", shown only when the player
 *        asked. Play never waits for it. Nothing is downloaded or changed
 *        until the player says yes.
 *
 * FETCH  update\download.part beside the launcher, carried on from where it
 *        stopped when the same file is asked for again; its size and SHA-256
 *        must be the release's (the file's "digest" in GitHub's answer, else
 *        the release's <zip>.sha256 file), or nothing happens. Unpacked into
 *        update\unpack (unzip.c).
 *
 * APPLY  is done by the NEW release's own launcher, started from update\unpack
 *        with --apply-update <release folder> <game folder>: it knows what its
 *        release holds and how its game is built; the old one only fetched it.
 *        It moves the release's files into the release folder (the old ones to
 *        update\backup), copies the programs into the game folder when that is
 *        another folder, builds the game's code again when the exe changed
 *        (the DLL is tied to one exact exe; the translated C of the last build
 *        is reused when the translate program is the same one), prepares the
 *        shaders and makes the icon again. Every step is written to
 *        update\apply.journal first; if anything fails, or the journal is
 *        still there at the next start, it is undone backwards and the install
 *        is the one from before. Saved games, settings, reports, logs and the
 *        disc's files are never touched: only names a release has are.
 *
 * WHO    An install made through RecompLauncher (RECOMPLAUNCHER=1 at
 *        --install: btvs_install.ini says so) is updated by RecompLauncher:
 *        here nothing is checked, fetched or replaced. A developer's build
 *        (the game's code inside the exe, as in the owner's GAME folder) is
 *        checked like any other, so the line shows there too, but it is not
 *        replaced by a release: it is updated by building.
 *
 * (tests) BTVS_UPDATE_API=<url> asks there instead (http only to this PC),
 * BTVS_UPDATE_VERSION=<x.y.z> pretends this build is that version,
 * BTVS_UPDATE_FAIL_AT=swap|build|end makes the apply fail at that point
 * (crash: the applier ends there without undoing anything). */

#include <winhttp.h>
#include "unzip.h"

#define UPDATE_API      L"https://api.github.com/repos/KaikoClanworth1/buffy-tvs-xbox-pc/releases?per_page=20"
#define UPDATE_ZIP_END  L"-Windows-x64.zip"
#define UPDATE_DIR      L"update"
#define INSTALL_INI     L"btvs_install.ini"     /* in the game folder: who installed it */
#define UPDATE_INI      L"btvs_update.ini"      /* in the game folder: the last check, for the game's menu */

#define WM_APP_UPDCHECK (WM_APP + 10)   /* lParam UpdateInfo* (heap) */
#define WM_APP_UPDREADY (WM_APP + 11)   /* wParam ok, lParam heap WCHAR* message; fetched and unpacked */
#define WM_APP_UPDDONE  (WM_APP + 12)   /* wParam ok; applied (the applier's window) */

typedef struct {
    int   ok;                       /* the list was read */
    int   newer;                    /* ...and has a newer version than this one */
    int   quiet;                    /* the failure is the kind not worth a line (no such repository, offline) */
    WCHAR version[32];              /* newest version, "0.3.0" */
    WCHAR name[128];                /* the release's title */
    WCHAR page[512];                /* its web page */
    WCHAR zip_name[260], zip_url[1024], sha_url[1024];
    WCHAR sha256[65];               /* lower-case hex, or "" */
    uint64_t zip_size;
    WCHAR notes[4096];
    WCHAR summary[512];             /* the notes' first paragraph */
    WCHAR error[256];
} UpdateInfo;

static UpdateInfo    s_upd;                 /* the last check (main thread) */
static volatile LONG s_upd_busy;            /* a check or a fetch running */
static int           s_upd_manual;          /* the check was the button's */
static int           s_updating;            /* 1 fetching (old launcher), 2 applying (new launcher) */
static WCHAR         s_upd_line[512];       /* what the Play view says about updates */
static WCHAR         s_upd_msg[1024];       /* why the last fetch or apply failed */
static WCHAR         s_upd_root[MAX_PATH];  /* the unpacked release */
static WCHAR         s_apply_release[MAX_PATH], s_apply_game[MAX_PATH];

static void layout(void);

static const WCHAR *my_version(void)
{
    static WCHAR v[32];
    if (!v[0] && !GetEnvironmentVariableW(L"BTVS_UPDATE_VERSION", v, 32))
        wcscpy_s(v, 32, WIDEN(BTVS_VERSION));
    return v;
}

/* "v1.2.3" -> a number to compare (four parts of 0-65535). */
static uint64_t version_key(const WCHAR *s)
{
    uint64_t k = 0;
    int part;
    while (*s && (*s < L'0' || *s > L'9'))
        s++;
    for (part = 0; part < 4; part++) {
        uint64_t n = 0;
        while (*s >= L'0' && *s <= L'9')
            n = n * 10 + (uint64_t)(*s++ - L'0');
        k = (k << 16) | (n > 0xFFFF ? 0xFFFF : n);
        if (*s == L'.')
            s++;
        else
            s = L"";
    }
    return k;
}

/* Why this install is not the launcher's to update, or NULL. */
static const WCHAR *update_not_ours(void)
{
    WCHAR ini[MAX_PATH], owner[64], env[8];
    const WCHAR *dir = is_game_folder(s_game_dir) ? s_game_dir : s_launcher_dir;
    join(ini, dir, INSTALL_INI);
    GetPrivateProfileStringW(L"Install", L"Owner", L"", owner, 64, ini);
    if (!_wcsicmp(owner, L"RecompLauncher") || GetEnvironmentVariableW(L"RECOMPLAUNCHER", env, 8))
        return L"Updates for this copy come through RecompLauncher.";
    return NULL;
}

/* A developer's build: the game's code is inside the exe. It can look for a newer release, but a
 * release cannot take its place (deploy.sh makes this folder). */
static int update_dev_build(void)
{
    const WCHAR *dir = is_game_folder(s_game_dir) ? s_game_dir : s_launcher_dir;
    return has_program(dir) && !exe_needs_game_dll(dir);
}

/* ── a little JSON reading (the text stays as it is; these find things) ── */

static const char *js_ws(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;
    return p;
}

static const char *js_skip(const char *p)       /* past one value, or NULL if it is broken */
{
    p = js_ws(p);
    if (!p || !*p)
        return NULL;
    if (*p == '"') {
        for (p++; *p && *p != '"'; p++)
            if (*p == '\\' && p[1])
                p++;
        return *p ? p + 1 : NULL;
    }
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        p = js_ws(p + 1);
        if (*p == close)
            return p + 1;
        for (;;) {
            if (close == '}') {
                p = js_skip(p);
                p = js_ws(p);
                if (!p || *p != ':')
                    return NULL;
                p++;
            }
            p = js_ws(js_skip(p));
            if (!p)
                return NULL;
            if (*p == ',') {
                p++;
                continue;
            }
            return *p == close ? p + 1 : NULL;
        }
    }
    while (*p && !strchr(",}] \t\r\n", *p))
        p++;
    return p;
}

static const char *js_get(const char *obj, const char *key)     /* an object's member, or NULL */
{
    size_t kl = strlen(key);
    const char *p = js_ws(obj);
    if (!p || *p != '{')
        return NULL;
    p = js_ws(p + 1);
    while (p && *p == '"') {
        const char *k = p + 1, *e = js_skip(p);
        int match;
        if (!e)
            return NULL;
        match = (size_t)(e - 1 - k) == kl && !memcmp(k, key, kl);
        p = js_ws(e);
        if (*p != ':')
            return NULL;
        p = js_ws(p + 1);
        if (match)
            return p;
        p = js_ws(js_skip(p));
        if (!p || *p != ',')
            return NULL;
        p = js_ws(p + 1);
    }
    return NULL;
}

static const char *js_at(const char *arr, int i)                /* an array's i-th value, or NULL */
{
    const char *p = js_ws(arr);
    if (!p || *p != '[')
        return NULL;
    p = js_ws(p + 1);
    if (*p == ']')
        return NULL;
    while (i-- > 0) {
        p = js_ws(js_skip(p));
        if (!p || *p != ',')
            return NULL;
        p = js_ws(p + 1);
    }
    return p;
}

static unsigned js_hex4(const char *p)
{
    unsigned c = 0;
    int i;
    for (i = 0; i < 4 && p[i]; i++) {
        char h = p[i];
        c = c << 4 | (unsigned)(h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10
                                : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
    }
    return c;
}

static void js_str(const char *v, WCHAR *out, int cap)          /* a string value, unescaped ("" when it is none) */
{
    const char *e;
    WCHAR *w;
    int n = 0;
    out[0] = 0;
    v = js_ws(v);
    if (!v || *v != '"' || !(e = js_skip(v)))
        return;
    w = (WCHAR *)malloc(((size_t)(e - v) + 2) * sizeof(WCHAR));
    if (!w)
        return;
    {   /* the raw text is UTF-8; escapes are put in afterwards, as UTF-16 */
        char *raw = (char *)malloc((size_t)(e - v));
        int rn = (int)(e - v - 2), wn, i;
        WCHAR *t;
        if (!raw) {
            free(w);
            return;
        }
        memcpy(raw, v + 1, (size_t)rn);
        t = (WCHAR *)malloc(((size_t)rn + 2) * sizeof(WCHAR));
        wn = t ? MultiByteToWideChar(CP_UTF8, 0, raw, rn, t, rn + 1) : 0;
        for (i = 0; i < wn; i++) {
            if (t[i] != L'\\' || i + 1 >= wn) {
                w[n++] = t[i];
                continue;
            }
            switch (t[++i]) {
            case L'n': w[n++] = L'\n'; break;
            case L'r': break;
            case L't': w[n++] = L' '; break;
            case L'b': case L'f': break;
            case L'u': {
                char h[5] = { 0, 0, 0, 0, 0 };
                int k;
                for (k = 0; k < 4 && i + 1 + k < wn; k++)
                    h[k] = (char)t[i + 1 + k];
                w[n++] = (WCHAR)js_hex4(h);
                i += 4;
                break;
            }
            default: w[n++] = t[i]; break;
            }
        }
        free(t);
        free(raw);
    }
    w[n] = 0;
    wcsncpy_s(out, cap, w, _TRUNCATE);
    free(w);
}

/* ── HTTP (WinHTTP, the system's proxy settings) ──────────────────────── */

/* GET url: into *body (malloc'd, 0-terminated), or appended to `file` from
 * byte `from` on (a Range request; *restarted is set when the server sent the
 * whole file instead, and the file is emptied first). 1 ok. */
static int http_get(const WCHAR *url, char **body, HANDLE file, uint64_t from, uint64_t total_hint, int *restarted,
                    DWORD *status_out, WCHAR *err, int errn)
{
    URL_COMPONENTSW uc;
    WCHAR host[256], path[2048], ua[64], hdr[160];
    HINTERNET ses = NULL, con = NULL, req = NULL;
    DWORD status = 0, sl = sizeof status, got;
    char *buf = NULL;
    static char chunk[65536];
    size_t len = 0, cap = 0;
    uint64_t have = from;
    int ok = 0, last_pm = -1, local;

    if (status_out)
        *status_out = 0;
    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host;
    uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = _countof(path);
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) {
        swprintf_s(err, errn, L"Bad address.");
        return 0;
    }
    local = !_wcsicmp(host, L"127.0.0.1") || !_wcsicmp(host, L"localhost");
    if (uc.nScheme != INTERNET_SCHEME_HTTPS && !local) {
        swprintf_s(err, errn, L"Not a secure address.");      /* plain http only to this PC (tests) */
        return 0;
    }
    swprintf_s(ua, 64, L"BuffyTVS-Launcher/%s", WIDEN(BTVS_VERSION));
    ses = WinHttpOpen(ua, local ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (ses) {
        WinHttpSetTimeouts(ses, 10000, 10000, 15000, 30000);
        con = WinHttpConnect(ses, host, uc.nPort, 0);
    }
    if (con)
        req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (file && from)
        swprintf_s(hdr, 160, L"Range: bytes=%llu-\r\n", (unsigned long long)from);
    else
        wcscpy_s(hdr, 160, file ? L"Accept: application/octet-stream\r\n" : L"Accept: application/vnd.github+json\r\n");
    if (!req || !WinHttpSendRequest(req, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
             || !WinHttpReceiveResponse(req, NULL)) {
        swprintf_s(err, errn, L"No connection (error %lu).", GetLastError());
        goto done;
    }
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &sl, WINHTTP_NO_HEADER_INDEX);
    if (status_out)
        *status_out = status;
    if (file && from && status == 200) {            /* the server does not do parts: from the start */
        SetFilePointer(file, 0, NULL, FILE_BEGIN);
        SetEndOfFile(file);
        have = 0;
        if (restarted)
            *restarted = 1;
    } else if (!(status == 200 || (file && from && status == 206))) {
        swprintf_s(err, errn, status == 403 || status == 429 ? L"GitHub is busy (too many checks); try again later."
                                                             : L"The server answered %lu.", status);
        goto done;
    }
    for (;;) {
        if (s_cancel && file) {
            swprintf_s(err, errn, L"Stopped. Update again to carry on from here.");
            goto done;
        }
        if (!WinHttpReadData(req, chunk, sizeof chunk, &got)) {
            swprintf_s(err, errn, L"The download broke off (error %lu).", GetLastError());
            goto done;
        }
        if (!got)
            break;
        if (file) {
            DWORD w;
            if (!WriteFile(file, chunk, got, &w, NULL) || w != got) {
                swprintf_s(err, errn, L"Could not write the download (error %lu). Is the disk full?", GetLastError());
                goto done;
            }
            have += got;
            if (total_hint) {
                int pm = (int)(have * 1000 / total_hint);
                if (pm / 10 != last_pm / 10) {
                    last_pm = pm;
                    post_progress(pm > 1000 ? 500 : pm / 2, L"Downloading version %s: %.0f of %.0f MB", s_upd.version,
                                  have / 1048576.0, total_hint / 1048576.0);
                }
            }
        } else {
            if (len + got + 1 > cap) {
                char *g;
                cap = (len + got + 1) * 2;
                if (cap > (32u << 20) || !(g = (char *)realloc(buf, cap))) {
                    swprintf_s(err, errn, L"The answer was too big.");
                    goto done;
                }
                buf = g;
            }
            memcpy(buf + len, chunk, got);
            len += got;
        }
    }
    if (body) {
        if (!buf)
            buf = (char *)calloc(1, 1);
        else
            buf[len] = 0;
        *body = buf;
        buf = NULL;
    }
    ok = 1;
done:
    free(buf);
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return ok;
}

/* ── the check ─────────────────────────────────────────────────────────── */

static void update_check_run(UpdateInfo *u)
{
    WCHAR api[1024];
    char *body = NULL;
    const char *rel;
    uint64_t best = 0;
    DWORD status = 0;
    int i;
    memset(u, 0, sizeof *u);
    if (!GetEnvironmentVariableW(L"BTVS_UPDATE_API", api, 1024))
        wcscpy_s(api, 1024, UPDATE_API);
    if (!http_get(api, &body, NULL, 0, 0, NULL, &status, u->error, 256)) {
        u->quiet = status == 404 || status == 0;        /* no public repository yet, or offline */
        return;
    }
    for (i = 0; (rel = js_at(body, i)) != NULL; i++) {
        const char *assets = js_get(rel, "assets"), *a, *d = js_get(rel, "draft"), *pre = js_get(rel, "prerelease");
        WCHAR tag[64];
        uint64_t key;
        int k, found = 0;
        if ((d && !strncmp(d, "true", 4)) || (pre && !strncmp(pre, "true", 4)))
            continue;
        js_str(js_get(rel, "tag_name"), tag, 64);
        key = version_key(tag);
        if (!tag[0] || key <= best)
            continue;
        for (k = 0; assets && (a = js_at(assets, k)) != NULL; k++) {
            WCHAR an[260], dg[128];
            const char *sz;
            size_t n, e = wcslen(UPDATE_ZIP_END);
            js_str(js_get(a, "name"), an, 260);
            n = wcslen(an);
            if (n <= e || _wcsicmp(an + n - e, UPDATE_ZIP_END))
                continue;
            found = 1;
            best = key;
            wcscpy_s(u->version, 32, tag[0] == L'v' || tag[0] == L'V' ? tag + 1 : tag);
            wcscpy_s(u->zip_name, 260, an);
            js_str(js_get(rel, "name"), u->name, 128);
            js_str(js_get(rel, "html_url"), u->page, 512);
            js_str(js_get(rel, "body"), u->notes, 4096);
            js_str(js_get(a, "browser_download_url"), u->zip_url, 1024);
            js_str(js_get(a, "digest"), dg, 128);
            u->sha256[0] = 0;
            u->sha_url[0] = 0;
            if (!_wcsnicmp(dg, L"sha256:", 7) && wcslen(dg + 7) == 64) {
                wcscpy_s(u->sha256, 65, dg + 7);
                _wcslwr_s(u->sha256, 65);
            }
            sz = js_get(a, "size");
            u->zip_size = sz ? _strtoui64(sz, NULL, 10) : 0;
            break;
        }
        if (!found)
            continue;
        for (k = 0; assets && (a = js_at(assets, k)) != NULL; k++) {    /* <zip>.sha256, GitHub's digest failing */
            WCHAR an[280], want[280];
            js_str(js_get(a, "name"), an, 280);
            swprintf_s(want, 280, L"%s.sha256", u->zip_name);
            if (!_wcsicmp(an, want))
                js_str(js_get(a, "browser_download_url"), u->sha_url, 1024);
        }
    }
    free(body);
    u->ok = 1;
    u->newer = best && best > version_key(my_version());
    {   /* the notes' first paragraph, on one line */
        int n = 0;
        const WCHAR *p = u->notes;
        while (*p == L'\n' || *p == L' ' || *p == L'#')
            p++;
        for (; *p && !(p[0] == L'\n' && p[1] == L'\n') && n < 500; p++)
            u->summary[n++] = *p == L'\n' ? L' ' : *p;
        u->summary[n] = 0;
    }
    if (!best)
        u->quiet = 1;                   /* no release with the zip yet */
}

/* What the game's menu can read (Menu area; btvs_update.c in the game). */
static void update_note_for_game(const UpdateInfo *u)
{
    WCHAR ini[MAX_PATH], t[32];
    SYSTEMTIME st;
    if (!is_game_folder(s_game_dir) || !u->ok)
        return;
    join(ini, s_game_dir, UPDATE_INI);
    GetSystemTime(&st);
    swprintf_s(t, 32, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    WritePrivateProfileStringW(L"Update", L"Installed", my_version(), ini);
    WritePrivateProfileStringW(L"Update", L"Available", u->newer ? u->version : L"", ini);
    WritePrivateProfileStringW(L"Update", L"Notes", u->newer ? u->summary : L"", ini);
    WritePrivateProfileStringW(L"Update", L"Page", u->newer ? u->page : L"", ini);
    WritePrivateProfileStringW(L"Update", L"Checked", t, ini);
}

static void update_line(void)
{
    const WCHAR *no = update_not_ours();
    if (no)
        wcscpy_s(s_upd_line, 512, no);
    else if (s_upd_busy && !s_updating)
        wcscpy_s(s_upd_line, 512, L"Checking for updates...");
    else if (s_upd.newer && update_dev_build())
        swprintf_s(s_upd_line, 512, L"Version %s is available (you have %s). This is a developer's build: it is updated by "
                                    L"building, not from the release.", s_upd.version, my_version());
    else if (s_upd.newer)
        swprintf_s(s_upd_line, 512, L"Version %s is available (you have %s).%s%s", s_upd.version, my_version(),
                   s_upd.summary[0] ? L" " : L"", s_upd.summary);
    else if (s_upd.ok && s_upd.version[0] && s_upd_manual)
        swprintf_s(s_upd_line, 512, L"This is the newest version (%s).", my_version());
    else if (s_upd.error[0] && s_upd_manual)
        swprintf_s(s_upd_line, 512, L"Could not check for updates: %s", s_upd.error);
    else if (s_upd_manual && s_upd.quiet)
        wcscpy_s(s_upd_line, 512, L"No newer version was found.");
    else
        s_upd_line[0] = 0;
}

static DWORD WINAPI update_check_thread(LPVOID unused)
{
    UpdateInfo *u = (UpdateInfo *)malloc(sizeof *u);
    (void)unused;
    if (u)
        update_check_run(u);
    PostMessageW(s_wnd, WM_APP_UPDCHECK, 0, (LPARAM)u);
    return 0;
}

static void update_check(int manual)
{
    if (update_not_ours() || s_updating || InterlockedCompareExchange(&s_upd_busy, 1, 0)) {
        update_line();
        return;
    }
    s_upd_manual = manual;
    update_line();
    layout();
    InvalidateRect(s_wnd, NULL, FALSE);
    CloseHandle(CreateThread(NULL, 0, update_check_thread, NULL, 0, NULL));
}

/* ── the fetch: download, check, unpack ────────────────────────────────── */

static int sha256_of_file(const WCHAR *path, WCHAR *hex)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hh = NULL;
    UCHAR digest[32];
    static UCHAR buf[65536];
    DWORD got;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    int ok = 0, i;
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0
            && BCryptCreateHash(alg, &hh, NULL, 0, NULL, 0, 0) == 0) {
        ok = 1;
        while (ReadFile(f, buf, sizeof buf, &got, NULL) && got)
            if (BCryptHashData(hh, buf, got, 0) != 0)
                ok = 0;
        ok = ok && BCryptFinishHash(hh, digest, 32, 0) == 0;
    }
    if (hh) BCryptDestroyHash(hh);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    for (i = 0; ok && i < 32; i++)
        swprintf_s(hex + i * 2, 3, L"%02x", digest[i]);
    return ok;
}

static uint64_t file_size_of(const WCHAR *p)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &fa))
        return 0;
    return ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
}

/* The unpacked release's own folder (the zip has its files at the top or in one folder). */
static int find_release_root(const WCHAR *unpack, WCHAR *root)
{
    WCHAR pat[MAX_PATH], t[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    join(t, unpack, LAUNCHER_EXE);
    if (file_exists(t)) {
        wcscpy_s(root, MAX_PATH, unpack);
        return 1;
    }
    join(pat, unpack, L"*");
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') {
            join(root, unpack, fd.cFileName);
            join(t, root, LAUNCHER_EXE);
            if (file_exists(t)) {
                FindClose(h);
                return 1;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

/* 1: s_upd_root holds the checked, unpacked release. 0: s_upd_msg says why not. */
static int update_fetch(const UpdateInfo *u)
{
    WCHAR dir[MAX_PATH], part[MAX_PATH], info[MAX_PATH], zip[MAX_PATH], unpack[MAX_PATH], want[1400], have[1400], hex[65], t[MAX_PATH];
    WCHAR sha[65];
    HANDLE f;
    uint64_t from;
    int tries, ok = 0;

    s_upd_msg[0] = 0;
    if (!u->zip_url[0]) {
        wcscpy_s(s_upd_msg, 1024, L"There is no update to download.");
        return 0;
    }
    join(dir, s_launcher_dir, UPDATE_DIR);
    join(part, dir, L"download.part");
    join(info, dir, L"download.info");
    join(zip, dir, L"download.zip");
    join(unpack, dir, L"unpack");
    if (!mkdirs(dir) && !dir_exists(dir)) {
        swprintf_s(s_upd_msg, 1024, L"Could not make the folder %s. Is the launcher's folder read-only?", dir);
        return 0;
    }
    /* the release's SHA-256: GitHub's own, else the release's .sha256 file */
    wcscpy_s(sha, 65, u->sha256);
    if (!sha[0] && u->sha_url[0]) {
        char *body = NULL;
        WCHAR e[256];
        if (http_get(u->sha_url, &body, NULL, 0, 0, NULL, NULL, e, 256) && body && strlen(body) >= 64) {
            int i;
            for (i = 0; i < 64; i++)
                sha[i] = (WCHAR)tolower((unsigned char)body[i]);
            sha[64] = 0;
            for (i = 0; i < 64; i++)
                if (!iswxdigit(sha[i]))
                    sha[0] = 0;
        }
        free(body);
    }
    if (!sha[0] || !u->zip_size) {
        wcscpy_s(s_upd_msg, 1024, L"The release gives no size or SHA-256 to check the download against, so it is not used.");
        return 0;
    }
    /* carry on a download of this same file, else start over */
    swprintf_s(want, 1400, L"%s|%llu|%s", u->zip_url, (unsigned long long)u->zip_size, sha);
    if (!read_small(info, have, 1400) || wcscmp(have, want)) {
        DeleteFileW(part);
        write_small(info, want);
    }
    for (tries = 0; tries < 4; tries++) {      /* a connection that drops is taken up again where it stopped */
        from = file_size_of(part);
        if (from > u->zip_size) {
            DeleteFileW(part);
            from = 0;
        }
        if (from == u->zip_size)
            break;
        f = CreateFileW(part, FILE_APPEND_DATA | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) {
            swprintf_s(s_upd_msg, 1024, L"Could not write in %s (error %lu).", dir, GetLastError());
            return 0;
        }
        SetFilePointer(f, 0, NULL, FILE_END);
        if (from)
            blog(L"update: carrying on the download at %llu of %llu bytes", (unsigned long long)from, (unsigned long long)u->zip_size);
        post_progress((int)(from * 500 / u->zip_size), L"Downloading version %s...", u->version);
        ok = http_get(u->zip_url, NULL, f, from, u->zip_size, NULL, NULL, s_upd_msg, 1024);
        CloseHandle(f);
        if (!ok && s_cancel)
            return 0;
        if (file_size_of(part) == u->zip_size)
            break;
        Sleep(1500);
    }
    if (file_size_of(part) != u->zip_size) {
        if (!s_upd_msg[0] || ok)
            wcscpy_s(s_upd_msg, 1024, L"The download is incomplete. Update again to carry on from where it stopped.");
        return 0;
    }
    post_progress(510, L"Checking the download...");
    if (!sha256_of_file(part, hex) || wcscmp(hex, sha)) {
        wcscpy_s(s_upd_msg, 1024, L"The download is damaged (its SHA-256 is not the release's). It was thrown away; update again.");
        DeleteFileW(part);
        DeleteFileW(info);
        return 0;
    }
    DeleteFileW(zip);
    if (!move_patiently(part, zip, MOVEFILE_REPLACE_EXISTING)) {
        wcscpy_s(s_upd_msg, 1024, L"The download could not be renamed.");
        return 0;
    }
    DeleteFileW(info);
    post_progress(540, L"Unpacking version %s...", u->version);
    remove_tree(unpack);
    mkdirs(unpack);
    if (!unzip_file(zip, unpack) || !find_release_root(unpack, s_upd_root)) {
        wcscpy_s(s_upd_msg, 1024, L"The update could not be unpacked.");
        remove_tree(unpack);
        return 0;
    }
    join(t, s_upd_root, GAME_EXE);
    if (!file_exists(t)) {
        wcscpy_s(s_upd_msg, 1024, L"The download is not a release of this port.");
        remove_tree(unpack);
        return 0;
    }
    {   /* the release says which version it is, in a fixed place: build\kit.ini [Kit] Version */
        WCHAR ini[MAX_PATH], v[32];
        swprintf_s(ini, MAX_PATH, L"%s\\" BUILD_DIR L"\\kit.ini", s_upd_root);
        GetPrivateProfileStringW(L"Kit", L"Version", L"", v, 32, ini);
        if (!v[0] || version_key(v) != version_key(u->version)) {
            swprintf_s(s_upd_msg, 1024, L"The download says it is version \"%s\", not %s: not used.", v, u->version);
            remove_tree(unpack);
            return 0;
        }
    }
    DeleteFileW(zip);
    post_progress(600, L"Version %s is unpacked.", u->version);
    return 1;
}

/* ── the apply (the new release's launcher, running from update\unpack) ── */

static FILE *s_journal;

static void journal(const WCHAR *kind, const WCHAR *a, const WCHAR *b)
{
    char u[MAX_PATH * 8];
    WCHAR w[MAX_PATH * 2 + 16];
    if (!s_journal)
        return;
    swprintf_s(w, _countof(w), L"%s\t%s\t%s", kind, a, b ? b : L"");
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL);
    fprintf(s_journal, "%s\n", u);
    fflush(s_journal);
    _commit(_fileno(s_journal));
}

static void remove_any(const WCHAR *p)
{
    DWORD a = GetFileAttributesW(p);
    if (a == INVALID_FILE_ATTRIBUTES)
        return;
    if (a & FILE_ATTRIBUTE_DIRECTORY)
        remove_tree(p);
    else
        DeleteFileW(p);
}

/* The old one aside (journalled first), then the new one in its place. */
static int upd_aside(const WCHAR *dst, const WCHAR *backup)
{
    if (GetFileAttributesW(dst) == INVALID_FILE_ATTRIBUTES)
        return 1;
    remove_any(backup);
    journal(L"B", dst, backup);
    if (move_patiently(dst, backup, 0))
        return 1;
    swprintf_s(s_upd_msg, 1024, L"Could not move %s aside (error %lu). Is the game or another launcher open?", dst, GetLastError());
    return 0;
}

static int upd_place(const WCHAR *src, const WCHAR *dst, int move)
{
    journal(move ? L"M" : L"C", dst, src);
    if (move ? move_patiently(src, dst, 0) : CopyFileW(src, dst, TRUE))
        return 1;
    swprintf_s(s_upd_msg, 1024, L"Could not put %s in place (error %lu).", dst, GetLastError());
    return 0;
}

/* Undo everything the journal lists, last first. Also what a launcher does at
 * its start when a journal is still there (an apply that never finished). */
static int update_rollback(const WCHAR *release)
{
    WCHAR jp[MAX_PATH];
    FILE *f = NULL;
    char **lines = NULL, buf[MAX_PATH * 8];
    int n = 0, cap = 0, i, bad = 0;
    swprintf_s(jp, MAX_PATH, L"%s\\" UPDATE_DIR L"\\apply.journal", release);
    if (s_journal) {
        fclose(s_journal);
        s_journal = NULL;
    }
    if (_wfopen_s(&f, jp, L"rb") || !f)
        return 1;
    while (fgets(buf, sizeof buf, f)) {
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            lines = (char **)realloc(lines, (size_t)cap * sizeof *lines);
        }
        buf[strcspn(buf, "\r\n")] = 0;
        lines[n++] = _strdup(buf);
    }
    fclose(f);
    for (i = n - 1; i >= 0; i--) {
        WCHAR w[MAX_PATH * 2 + 16], *a, *b;
        MultiByteToWideChar(CP_UTF8, 0, lines[i], -1, w, _countof(w));
        a = wcschr(w, L'\t');
        b = a ? wcschr(a + 1, L'\t') : NULL;
        if (a && b) {
            *a++ = 0;
            *b++ = 0;
            if (!wcscmp(w, L"M")) {                 /* a was moved in from b: back, if it got there */
                if (GetFileAttributesW(a) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(b) == INVALID_FILE_ATTRIBUTES
                        && !move_patiently(a, b, 0))
                    bad = 1;
            } else if (!wcscmp(w, L"C") || !wcscmp(w, L"N")) {  /* a was copied in, or made by the build */
                remove_any(a);
            } else if (!wcscmp(w, L"B")) {          /* a was moved aside to b: back */
                if (GetFileAttributesW(b) != INVALID_FILE_ATTRIBUTES) {
                    remove_any(a);
                    if (!move_patiently(b, a, 0))
                        bad = 1;
                }
            }
        }
        free(lines[i]);
    }
    free(lines);
    if (!bad)
        DeleteFileW(jp);
    blog(bad ? L"update: undone, but not every file could be put back (see apply.journal)" : L"update: undone; the install is the one from before");
    return !bad;
}

static int update_fail_at(const WCHAR *where)
{
    WCHAR e[32];
    if (GetEnvironmentVariableW(L"BTVS_UPDATE_FAIL_AT", e, 32) && !_wcsicmp(e, L"crash") && !wcscmp(where, L"swap"))
        ExitProcess(3);                 /* (test) as if the PC went off with the files half in place */
    if (GetEnvironmentVariableW(L"BTVS_UPDATE_FAIL_AT", e, 32) && !_wcsicmp(e, where)) {
        swprintf_s(s_upd_msg, 1024, L"(test) made to fail at \"%s\"", where);
        return 1;
    }
    return 0;
}

static int game_exe_busy(const WCHAR *dir)
{
    WCHAR exe[MAX_PATH];
    HANDLE f;
    join(exe, dir, GAME_EXE);
    if (!file_exists(exe))
        return 0;
    f = CreateFileW(exe, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle(f);
    return 0;
}

/* Names an update never brings or replaces, whatever a zip holds. */
static int update_never(const WCHAR *name)
{
    static const WCHAR *no[] = { SETTINGS_INI, L"launcher.ini", SAVE_DIR, REPORTS_DIR, INSTALL_INI, UPDATE_INI, UPDATE_DIR,
                                 L"btvs_icon.ico", L"install_log.txt", L"ShaderCache", L"crashes", GAME_DLL, GAME_DLL_OK,
                                 L"btvs_game.pdb", L"translated", WORK_DIR };
    int i;
    for (i = 0; i < (int)_countof(no); i++)
        if (!_wcsicmp(name, no[i]))
            return 1;
    return !_wcsnicmp(name, L"btvs_log", 8) || !_wcsicmp(name, L"Default.xbe");
}

/* The whole apply. This launcher runs from the unpacked release (s_launcher_dir).
 * 1 ok. 0: undone, s_upd_msg says why. */
static int update_apply_run(const WCHAR *release, const WCHAR *game)
{
    static const WCHAR *programs[] = { GAME_EXE, L"buffy_tvs.pdb", L"Read Me.txt", LAUNCHER_EXE,
                                       L"SDL3.dll", L"SDL3-LICENSE.txt", L"vcruntime140.dll", L"vcruntime140_1.dll", L"msvcp140.dll" };
    static const WCHAR *built[] = { GAME_DLL, L"btvs_game.pdb", GAME_DLL_OK };
    WCHAR upd[MAX_PATH], back_r[MAX_PATH], back_g[MAX_PATH], src_root[MAX_PATH], pat[MAX_PATH], a[MAX_PATH], b[MAX_PATH], c[MAX_PATH];
    WCHAR jp[MAX_PATH], logp[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int ok = 1, pass, i, separate, own_log = 0;

    s_upd_msg[0] = 0;
    wcscpy_s(src_root, MAX_PATH, s_launcher_dir);
    separate = game[0] && _wcsicmp(game, release) && has_program(game);
    join(upd, release, UPDATE_DIR);
    join(back_r, upd, L"backup");
    join(back_g, game, L"update_backup");
    join(jp, upd, L"apply.journal");
    join(logp, upd, L"update_log.txt");
    if (!s_blog) {
        own_log = !_wfopen_s(&s_blog, logp, L"ab") && s_blog;
        if (!own_log)
            s_blog = NULL;
    }
    blog(L"---- update to %s: release folder %s, game folder %s ----", WIDEN(BTVS_VERSION), release, game[0] ? game : L"(none)");
    if (file_exists(jp))
        update_rollback(release);       /* an apply that never finished */
    if (game_exe_busy(release) || (game[0] && game_exe_busy(game))) {
        wcscpy_s(s_upd_msg, 1024, L"Close the game first, then update.");
        ok = 0;
        goto out;
    }
    remove_tree(back_r);
    mkdirs(back_r);
    if (separate) {
        remove_tree(back_g);
        mkdirs(back_g);
    }
    if (_wfopen_s(&s_journal, jp, L"wb"))
        s_journal = NULL;
    if (!s_journal) {
        wcscpy_s(s_upd_msg, 1024, L"Could not write in the launcher's update folder.");
        ok = 0;
        goto out;
    }
    post_progress(610, L"Putting version %s in place...", WIDEN(BTVS_VERSION));

    /* 1. the release folder: everything the new release has, the launcher last */
    for (pass = 0; ok && pass < 2; pass++) {
        join(pat, src_root, L"*");
        h = FindFirstFileW(pat, &fd);
        if (h == INVALID_HANDLE_VALUE)
            break;
        do {
            int is_launcher = !_wcsicmp(fd.cFileName, LAUNCHER_EXE);
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || is_launcher != pass || update_never(fd.cFileName))
                continue;
            join(a, src_root, fd.cFileName);
            join(b, release, fd.cFileName);
            join(c, back_r, fd.cFileName);
            ok = upd_aside(b, c) && upd_place(a, b, 1);
        } while (ok && FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (ok) {
        wcscpy_s(s_launcher_dir, MAX_PATH, release);      /* this program and its kit are there now */
        ok = !update_fail_at(L"swap");
    }

    /* 2. the game folder, when it is another one: the programs */
    for (i = 0; ok && separate && i < (int)_countof(programs); i++) {
        join(a, release, programs[i]);
        join(b, game, programs[i]);
        join(c, back_g, programs[i]);
        if (file_exists(a))
            ok = upd_aside(b, c) && upd_place(a, b, 0);
    }

    /* 3. what was built for the old exe, when the new one needs it built again */
    if (ok && game[0] && has_program(game)) {
        const WCHAR *bk = separate ? back_g : back_r;
        if (!separate)
            mkdirs(back_r);
        if (game_code_missing(game)) {
            for (i = 0; ok && i < (int)_countof(built); i++) {
                join(b, game, built[i]);
                swprintf_s(c, MAX_PATH, L"%s\\built_%s", bk, built[i]);
                ok = upd_aside(b, c);
                journal(L"N", b, NULL);
            }
            if (ok) {
                FILE *keep = s_blog;        /* build_game opens the install log itself */
                s_blog = NULL;
                ok = !update_fail_at(L"build") && build_game(game);
                s_blog = keep;
                if (!ok && !s_upd_msg[0])
                    wcsncpy_s(s_upd_msg, 1024, s_install_msg, _TRUNCATE);
                blog(ok ? L"the game's code is built for the new exe" : L"building the game's code failed: %s", s_upd_msg);
            }
        } else {
            blog(L"the game's code fits the new exe as it is: not built again");
            post_progress(B_P_LINK, L"Preparing the shaders...");
            build_shaders(game);
        }
        if (ok) {
            join(b, game, L"btvs_icon.ico");
            swprintf_s(c, MAX_PATH, L"%s\\built_btvs_icon.ico", bk);
            upd_aside(b, c);
            journal(L"N", b, NULL);
            disc_icon(game);                /* the exe is a new file: its icon again */
        }
    }
    if (ok)
        ok = !update_fail_at(L"end");

out:
    if (ok) {
        if (s_journal)
            fclose(s_journal);
        s_journal = NULL;
        DeleteFileW(jp);
        remove_tree(back_r);
        if (separate)
            remove_tree(back_g);
        join(a, upd, L"unpack");
        remove_tree(a);
        blog(L"update: done, version %s", WIDEN(BTVS_VERSION));
        post_progress(1000, L"Updated to version %s.", WIDEN(BTVS_VERSION));
    } else {
        WCHAR why[1024];
        wcscpy_s(why, 1024, s_upd_msg);
        blog(L"update: failed: %s", why);
        post_progress(990, L"The update did not work. Putting the version from before back...");
        if (!update_rollback(release))
            wcscat_s(why, 1024, L" Not every file could be put back: see update\\update_log.txt.");
        wcscpy_s(s_launcher_dir, MAX_PATH, src_root);
        wcscpy_s(s_upd_msg, 1024, why);
        remove_tree(back_r);
        if (separate)
            remove_tree(back_g);
    }
    {   /* for the launcher that starts next */
        WCHAR res[MAX_PATH], line[1100];
        join(res, upd, L"last_result.txt");
        swprintf_s(line, 1100, ok ? L"ok %s" : L"failed %s", ok ? WIDEN(BTVS_VERSION) : s_upd_msg);
        write_small(res, line);
    }
    if (own_log) {
        fclose(s_blog);
        s_blog = NULL;
    }
    return ok;
}

/* Start the unpacked release's launcher to apply itself. quiet: wait for it and return its result. */
static int update_start_applier(int quiet, DWORD *code)
{
    WCHAR exe[MAX_PATH], cmd[MAX_PATH * 3 + 96];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    const WCHAR *game = is_game_folder(s_game_dir) ? s_game_dir : L"";
    join(exe, s_upd_root, LAUNCHER_EXE);
    swprintf_s(cmd, _countof(cmd), L"\"%s\" --apply-update \"%s\" \"%s\" %lu%s", exe, s_launcher_dir, game[0] ? game : L"-",
               GetCurrentProcessId(), quiet ? L" --console" : L"");
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(exe, cmd, NULL, NULL, quiet, 0, NULL, s_upd_root, &si, &pi)) {
        swprintf_s(s_upd_msg, 1024, L"The new version's launcher could not be started (error %lu).", GetLastError());
        return 0;
    }
    CloseHandle(pi.hThread);
    if (quiet) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, code);
    }
    CloseHandle(pi.hProcess);
    return 1;
}

/* At every start: an apply that never finished is undone; what the last one left is tidied.
 * 1 when something was undone: the launcher in this folder is then another file than this
 * running program, which should hand over to it. */
static int update_startup(void)
{
    WCHAR p[MAX_PATH], upd[MAX_PATH];
    int undone = 0;
    join(upd, s_launcher_dir, UPDATE_DIR);
    if (!dir_exists(upd))
        return 0;
    join(p, upd, L"apply.journal");
    if (file_exists(p)) {
        WCHAR logp[MAX_PATH];
        join(logp, upd, L"update_log.txt");
        if (_wfopen_s(&s_blog, logp, L"ab"))
            s_blog = NULL;
        blog(L"an update did not finish (apply.journal is here): undoing it");
        update_rollback(s_launcher_dir);
        if (s_blog)
            fclose(s_blog);
        s_blog = NULL;
        join(p, upd, L"last_result.txt");
        write_small(p, L"failed The update was interrupted. The version from before was put back.");
        undone = 1;
    }
    join(p, upd, L"unpack");
    remove_tree(p);
    join(p, upd, L"backup");
    remove_tree(p);
    return undone;
}

/* The last apply's result, once: into the status line. */
static void update_show_result(void)
{
    WCHAR p[MAX_PATH], line[1100];
    swprintf_s(p, MAX_PATH, L"%s\\" UPDATE_DIR L"\\last_result.txt", s_launcher_dir);
    if (!read_small(p, line, 1100))
        return;
    DeleteFileW(p);
    if (!wcsncmp(line, L"ok ", 3))
        set_status(0, L"Updated to version %s. Your saved games and settings are as they were.", line + 3);
    else if (!wcsncmp(line, L"failed ", 7))
        set_status(1, L"The update did not finish, and the version from before was put back: %s", line + 7);
}

/* ── the window's side ─────────────────────────────────────────────────── */

static DWORD WINAPI update_fetch_thread(LPVOID unused)
{
    int ok = update_fetch(&s_upd);
    (void)unused;
    PostMessageW(s_wnd, WM_APP_UPDREADY, (WPARAM)ok, (LPARAM)wdup(s_upd_msg));
    return 0;
}

static DWORD WINAPI update_apply_thread(LPVOID unused)
{
    int ok = update_apply_run(s_apply_release, s_apply_game);
    (void)unused;
    PostMessageW(s_wnd, WM_APP_UPDDONE, (WPARAM)ok, 0);
    return 0;
}

/* The Update button: ask, then fetch. */
static void update_ask(void)
{
    WCHAR t[2600], notes[1500];
    if (!s_upd.newer || s_updating || s_installing || update_dev_build())
        return;
    if (game_running()) {
        set_status(1, L"Close the game before updating.");
        return;
    }
    wcsncpy_s(notes, 1500, s_upd.notes, _TRUNCATE);
    if (wcslen(s_upd.notes) >= 1499)
        wcscat_s(notes, 1500, L"...");
    swprintf_s(t, 2600, L"Version %s of the PC port is available (you have %s).\n\n%s%s"
                        L"Update now? It downloads %.0f MB. Your saved games, settings and reports are kept. If the new "
                        L"version needs the game built again for this PC, that takes a few minutes; if anything fails, "
                        L"the version you have now is put back.",
               s_upd.version, my_version(), notes, notes[0] ? L"\n\n" : L"", s_upd.zip_size / 1048576.0);
    if (MessageBoxW(s_wnd, t, GAME_TITLE L" - Update", MB_YESNO | MB_ICONINFORMATION) != IDYES)
        return;
    if (InterlockedCompareExchange(&s_upd_busy, 1, 0))
        return;
    s_cancel = 0;
    s_updating = 1;
    s_permille = 0;
    set_status(0, L"Downloading version %s...", s_upd.version);
    layout();
    CloseHandle(CreateThread(NULL, 0, update_fetch_thread, NULL, 0, NULL));
}
