/* hd-pack.exe: installs the HD pack (FatalPixels textures, SeedVR2 movies).
 *
 * Started by the game in the background (like textures-hd.exe). Works on the
 * folder it sits in (the game folder):
 *   - one at a time (hd-pack.lock held open), IDLE priority;
 *   - reads the manifest hd-pack.txt from the release (HD_PACK_URL below;
 *     HDPACK_URL=<base URL> overrides it, HDPACK_SOURCE=<folder> takes the
 *     manifest and zips from a local folder, for tests); offline: stops
 *     quietly, no bar;
 *   - nothing to do if textures/hd-pack/hd-pack.txt has the same version;
 *     else the zips installed as they are (same name, size, SHA-256) are
 *     skipped: a pack that only changes the movies downloads only them;
 *   - each zip downloaded into hd-pack-tmp/<zip>.part (WinHTTP; a cut
 *     download goes on from where it stopped next time, with Range), renamed
 *     <zip> when whole, size and SHA-256 checked (a bad one is deleted);
 *   - once all are there, unpacked into hd-pack-tmp/stage/; then hp_install
 *     (all or nothing);
 *   - then the Real-ESRGAN textures are set aside in hd-pack-ancien/ (never
 *     deleted: the player empties that folder if they want);
 *   - then the verified zips are deleted by their manifest names and the
 *     empty work folders it made are removed, deepest first;
 *   - hd-pack-progress.txt says "working <percent> 100" then "done 0 100"
 *     (removed again if it fails, so the game shows no bar); log hd-pack.log.
 * Deletion safety: see hdpack_core.h. Build (MSYS2 or the cloud's mingw):
 *   x86_64-w64-mingw32-gcc -O2 -municode -I tools -o hd-pack.exe tools/hd-pack.c -lbcrypt -lwinhttp
 * Tests: tests/hdpack/run.sh (and t_download.py), in a Linux cloud session
 * under Wine only. */
#include "hdpack_core.h"
#include "../runtime/hdpack.h"     /* hdpack_mutex_name: the game sees this one is at work */
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <winhttp.h>

/* a new pack = a new release and a new address in a new version of the game */
#define HD_PACK_URL L"https://github.com/romusan495/Dragon-Quest-IX-Modern/releases/download/hd-pack-2/"

#define MAX_ZIPS 64
#define MANIFEST_MAX (64 * 1024)

static wchar_t G[HP_WMAX];          /* the game folder */
static wchar_t PROGRESS[HP_WMAX];

static int progress(const char* state, int pct) {
    char b[64];
    int n = snprintf(b, sizeof b, "%s %d 100", state, pct);
    return hp_write_own(G, PROGRESS, b, (size_t)n, 0);
}
/* a small text file, at most max-1 bytes; -1 if missing or bigger */
static int read_small(const wchar_t* path, char* out, DWORD max) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    DWORD got = 0;
    BOOL ok = ReadFile(f, out, max - 1, &got, NULL);
    char extra; DWORD more = 0;
    if (ok && got == max - 1) ReadFile(f, &extra, 1, &more, NULL);
    CloseHandle(f);
    if (!ok || more) return -1;
    out[got] = 0;
    if (strlen(out) != got) return -1;                         /* a NUL inside: not a text manifest */
    return (int)got;
}
static int size_of(const wchar_t* path, long long* size) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d)) return -1;
    *size = (long long)d.nFileSizeHigh << 32 | d.nFileSizeLow;
    return 0;
}
/* ---------------------------------------------------------- download */
static HINTERNET g_http;
static wchar_t g_host[256], g_base[HP_WMAX];   /* g_base: URL path, ends with '/' */
static int g_port, g_secure;
static long long g_total, g_before;            /* all zip bytes; bytes of the zips already done */
static ULONGLONG g_last_bar;
static int g_last_pct = -1;

static void bar(long long have) {                /* at most twice a second */
    ULONGLONG now = GetTickCount64();
    int pct = g_total > 0 ? (int)((g_before + have) * 85 / g_total) : 0;
    if (pct == g_last_pct || now - g_last_bar < 500) return;
    g_last_bar = now; g_last_pct = pct;
    progress("working", pct);
}
/* the base URL of the manifest and zips: scheme, host, port, path */
static int set_base(const wchar_t* url) {
    URL_COMPONENTS u; memset(&u, 0, sizeof u); u.dwStructSize = sizeof u;
    wchar_t host[256], path[HP_WMAX];
    u.lpszHostName = host; u.dwHostNameLength = 256;
    u.lpszUrlPath = path; u.dwUrlPathLength = HP_WMAX - 2;
    if (!WinHttpCrackUrl(url, 0, 0, &u)) return -1;
    if (u.nScheme != INTERNET_SCHEME_HTTPS && u.nScheme != INTERNET_SCHEME_HTTP) return -1;
    if (!host[0]) return -1;
    wcscpy(g_host, host);
    g_port = u.nPort; g_secure = u.nScheme == INTERNET_SCHEME_HTTPS;
    if (!path[0]) wcscpy(path, L"/");
    size_t n = wcslen(path);
    if (path[n - 1] != L'/') { path[n] = L'/'; path[n + 1] = 0; }
    wcscpy(g_base, path);
    return 0;
}
/* one GET; *req open with the status in *status, 0 ok */
static int http_get(const wchar_t* host, int port, int secure, const wchar_t* path, long long from,
                    HINTERNET* conn, HINTERNET* req, DWORD* status) {
    *conn = *req = NULL;
    if (!g_http) {
        /* the system's proxy settings (Windows 8.1+), else WinHTTP's own */
        g_http = WinHttpOpen(L"dqix-enhanced-hd-pack", 4 /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY */, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!g_http) g_http = WinHttpOpen(L"dqix-enhanced-hd-pack", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!g_http) { hp_log("  WinHTTP can't start (error %lu)\n", GetLastError()); return -1; }
        if (!WinHttpSetTimeouts(g_http, 8000, 8000, 15000, 30000)) hp_log("  WinHTTP timeouts not set (error %lu)\n", GetLastError());
    }
    *conn = WinHttpConnect(g_http, host, (INTERNET_PORT)port, 0);
    if (!*conn) return -1;
    *req = WinHttpOpenRequest(*conn, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!*req) return -1;
    wchar_t range[64] = L"";
    if (from > 0) _snwprintf(range, 64, L"Range: bytes=%lld-", from);
    if (!WinHttpSendRequest(*req, from > 0 ? range : WINHTTP_NO_ADDITIONAL_HEADERS, from > 0 ? (DWORD)-1L : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(*req, NULL)) return -1;
    DWORD sz = sizeof *status;
    if (!WinHttpQueryHeaders(*req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, status, &sz, WINHTTP_NO_HEADER_INDEX)) return -1;
    return 0;
}
static void http_close(HINTERNET conn, HINTERNET req) {
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
}
/* the manifest (small) into out; 0 ok */
static int fetch_small(const wchar_t* path, char* out, DWORD max) {
    HINTERNET c, r; DWORD st = 0, n = 0;
    int rc = -1;
    if (http_get(g_host, g_port, g_secure, path, 0, &c, &r, &st) || st != 200) goto done;
    for (;;) {
        DWORD got = 0;
        if (n >= max - 1) goto done;                           /* too big for a manifest */
        if (!WinHttpReadData(r, out + n, max - 1 - n, &got)) goto done;
        if (!got) break;
        n += got;
    }
    out[n] = 0;
    rc = strlen(out) == n ? 0 : -1;
done:
    if (rc && st == 200) hp_log("  %s: too big, cut, or not text\n", hp_u8(path));
    else if (rc && st) hp_log("  %s: HTTP %lu\n", hp_u8(path), st);
    http_close(c, r);
    return rc;
}

/* Download path into <dest>.part (resuming at `have` bytes with Range), then
 * rename it <dest> once it holds `size` bytes. The .part is opened only after
 * the server answered, never through a link (hp_open_own). 0 ok; on a cut the
 * .part keeps what came, for next time. */
static int hp_fetchW(const wchar_t* host, int port, int secure, const wchar_t* path, const wchar_t* d, long long have, long long size) {
    wchar_t part[HP_WMAX];
    if (wcslen(d) + 6 > HP_WMAX) return -1;
    _snwprintf(part, HP_WMAX, L"%ls.part", d);
    if (!hp_safe_path(G, d) || !hp_safe_path(G, part)) { hp_log("  refused (unsafe path): %s\n", hp_u8(part)); return -1; }
    if (have < 0 || have > size) {                             /* longer than the zip: not something to go on from */
        hp_log("  %s: .part longer than the zip, starting over\n", hp_u8(part));
        if (hp_delete_own(G, part)) return -1;
        have = 0;
    }
    HINTERNET c = NULL, r = NULL; HANDLE f = INVALID_HANDLE_VALUE;
    static char buf[1 << 16];
    int rc = -1;
    DWORD st = 0;
    if (have < size) {
        if (http_get(host, port, secure, path, have, &c, &r, &st)) { hp_log("  %s: no answer (error %lu)\n", hp_u8(path), GetLastError()); goto done; }
        if (st == 206) {
            /* exactly "bytes <have>-<size-1>/<size>": the rest of this very file */
            wchar_t cr[128]; DWORD sz = sizeof cr; wchar_t want[96];
            _snwprintf(want, 96, L"bytes %lld-%lld/%lld", have, size - 1, size);
            if (!WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX, cr, &sz, WINHTTP_NO_HEADER_INDEX)
                || wcscmp(cr, want)) { hp_log("  %s: the server sent another part, the .part is kept\n", hp_u8(path)); goto done; }
        } else if (st == 200) {
            /* the whole file again: only if it is this zip's size (a captive
             * portal's page must not wipe what came already) */
            wchar_t cl[32]; DWORD sz = sizeof cl;
            if (!WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, cl, &sz, WINHTTP_NO_HEADER_INDEX)
                || _wtoi64(cl) != size) { hp_log("  %s: HTTP 200 of another size, not the zip; the .part is kept\n", hp_u8(path)); goto done; }
            if (have) hp_log("  %s: the server sent it all again, starting over\n", hp_u8(path));
            have = -1;                                         /* truncate the .part once open */
        } else if (st == 416) {
            hp_log("  %s: HTTP 416, the .part is dropped\n", hp_u8(path));
            if (hp_delete_own(G, part)) hp_log("  %s could not be dropped\n", hp_u8(part));
            goto done;
        } else { hp_log("  %s: HTTP %lu\n", hp_u8(path), st); goto done; }
    }
    f = hp_open_own(G, part, 1);
    if (f == INVALID_HANDLE_VALUE) { hp_log("  can't open %s (error %lu)\n", hp_u8(part), GetLastError()); goto done; }
    LARGE_INTEGER cur, zero; zero.QuadPart = 0;
    if (have == -1) {
        if (!SetFilePointerEx(f, zero, NULL, FILE_BEGIN) || !SetEndOfFile(f)) { hp_log("  can't empty %s (error %lu)\n", hp_u8(part), GetLastError()); goto done; }
        have = 0;
    }
    if (!GetFileSizeEx(f, &cur) || cur.QuadPart != have) { hp_log("  %s changed size meanwhile\n", hp_u8(part)); goto done; }
    if (!SetFilePointerEx(f, zero, NULL, FILE_END)) { hp_log("  can't seek %s (error %lu)\n", hp_u8(part), GetLastError()); goto done; }
    while (r && have < size) {
        DWORD got = 0, put = 0;
        if (!WinHttpReadData(r, buf, sizeof buf, &got)) { hp_log("  %s: cut at %lld of %lld bytes (error %lu), goes on next time\n", hp_u8(path), have, size, GetLastError()); goto done; }
        if (!got) break;
        if (have + got > size) {
            hp_log("  %s: more bytes than the manifest says, the .part is emptied\n", hp_u8(path));
            if (!SetFilePointerEx(f, zero, NULL, FILE_BEGIN) || !SetEndOfFile(f)) hp_log("  can't empty %s (error %lu)\n", hp_u8(part), GetLastError());
            goto done;
        }
        if (!WriteFile(f, buf, got, &put, NULL) || put != got) { hp_log("  can't write %s (error %lu)\n", hp_u8(part), GetLastError()); goto done; }
        have += got;
        bar(have);
    }
    if (have != size) { hp_log("  %s: cut at %lld of %lld bytes, goes on next time\n", hp_u8(path), have, size); goto done; }
    CloseHandle(f); f = INVALID_HANDLE_VALUE;
    if (hp_move(G, part, d)) goto done;
    rc = 0;
done:
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    http_close(c, r);
    return rc;
}
static int hp_fetch(const wchar_t* host, int port, int secure, const wchar_t* path, const char* dest, long long have, long long size) {
    wchar_t d[HP_WMAX];
    if (hp_wide(dest, d)) return -1;
    return hp_fetchW(host, port, secure, path, d, have, size);
}

/* the zip in tmp is the manifest's one (size and SHA-256) */
static int zip_ok(const wchar_t* path, const HpZip* z) {
    long long size; char sha[65];
    if (!hp_isfile(path) || size_of(path, &size) || size != z->size) return 0;
    if (hp_sha256_fileW(path, sha)) return 0;
    return !strcmp(sha, z->sha);
}

/* hd-pack-textures-N.zip (tools/make_hd_pack.py): it unpacks into textures/ */
static int is_textures_zip(const char* name) { return !strncmp(name, "hd-pack-textures-", 17); }

static void open_log(void) {
    if (hp_logfile) return;
    wchar_t logp[HP_WMAX];
    if (!hp_join(logp, G, L"hd-pack.log")) {
        HANDLE h = hp_open_own(G, logp, 1);                    /* not through a link, not hard-linked */
        if (h != INVALID_HANDLE_VALUE) {
            int fd = _open_osfhandle((intptr_t)h, _O_APPEND | _O_BINARY);
            if (fd < 0) CloseHandle(h);
            else if (!(hp_logfile = _fdopen(fd, "ab"))) _close(fd);
        }
    }
    SYSTEMTIME now; GetLocalTime(&now);
    hp_log("\n%04d-%02d-%02d %02d:%02d:%02d\n", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
}
/* a Real-ESRGAN folder still to set aside (a plain folder, links don't count) */
static int aside_pending(void) {
    wchar_t p[HP_WMAX];
    if (!hp_join(p, G, L"textures\\hd") && hp_isdir(p)) return 1;
    if (!hp_join(p, G, L"textures-methode5") && hp_isdir(p)) return 1;
    if (hp_join(p, G, L"textures-avant-*")) return 0;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(p, &fd); int found = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do { if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) found = 1; } while (!found && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}
static void set_aside(void) {
    int left = hp_set_asideW(G);
    if (hp_set_aside_moved) hp_log("  %d Real-ESRGAN folder(s) set aside in hd-pack-ancien (empty that folder to free the space)\n", hp_set_aside_moved);
    if (left) hp_log("  %d folder(s) could not be set aside (see above), tried again next time\n", left);
}

int wmain(void) {
    /* the game folder: where this exe is */
    DWORD n = GetModuleFileNameW(NULL, G, HP_WMAX);
    if (!n || n >= HP_WMAX) return 1;
    wchar_t* s = wcsrchr(G, L'\\');
    if (!s) return 1;
    *s = 0;
    if (!SetCurrentDirectoryW(G)) return 1;
    if (hp_join(PROGRESS, G, L"hd-pack-progress.txt")) return 1;

    /* one at a time: a file held open (no sharing), let go when this ends even if killed */
    wchar_t lock[HP_WMAX];
    if (hp_join(lock, G, L"hd-pack.lock") || !hp_safe_path(G, lock)) {
        open_log(); hp_log("  game folder refused (a link, or an odd path): %s; nothing done\n", hp_u8(G)); return 1;
    }
    /* (not through a link, nothing truncated; the file itself is never
     * deleted, even on close: an empty hd-pack.lock stays, held only while open) */
    HANDLE lh = hp_open_own(G, lock, 0);
    if (lh == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_SHARING_VIOLATION) { open_log(); hp_log("  another hd-pack.exe is at work: this one stops\n"); return 0; }
        open_log(); hp_log("  can't make the lock %s (error %lu); nothing done\n", hp_u8(lock), GetLastError()); return 1;
    }
    /* the game comes first: idle CPU, and low disk and memory priority too */
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
    SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_BEGIN);
    {   /* the game shows the bar only while this mutex is there (gone when this ends, even killed) */
        wchar_t mname[128];
        hdpack_mutex_name(G, mname, 128);
        if (!CreateMutexW(NULL, FALSE, mname)) { open_log(); hp_log("  no mutex %s (error %lu): the game shows no bar\n", hp_u8(mname), GetLastError()); }
    }

    /* a run killed while it worked left the bar at "working": not any more */
    {
        char pr[64];
        if (read_small(PROGRESS, pr, sizeof pr) >= 0 && !strncmp(pr, "working", 7) && hp_delete_own(G, PROGRESS)) {
            open_log(); hp_log("  an old progress file says working and can't be removed\n");
        }
    }

    /* the manifest: a local folder (tests), or the release */
    wchar_t src[HP_WMAX] = L"", url[HP_WMAX] = HD_PACK_URL, path[HP_WMAX];
    DWORD sl = GetEnvironmentVariableW(L"HDPACK_SOURCE", src, HP_WMAX);
    int local = sl > 0 && sl < HP_WMAX;
    if (!local) src[0] = 0;
    DWORD ul = GetEnvironmentVariableW(L"HDPACK_URL", url, HP_WMAX);
    if (!ul || ul >= HP_WMAX) wcscpy(url, HD_PACK_URL);
    static char manifest[MANIFEST_MAX];
    static HpZip zips[MAX_ZIPS];
    int version = -1, nz;
    if (local) {
        if (hp_join(path, src, L"hd-pack.txt") || read_small(path, manifest, sizeof manifest) < 0) {
            open_log(); hp_log("  no manifest at %s; nothing done\n", hp_u8(path)); CloseHandle(lh); return 1;
        }
    } else {
        if (set_base(url) || wcslen(g_base) + 12 >= HP_WMAX) { open_log(); hp_log("  bad pack address %s; nothing done\n", hp_u8(url)); CloseHandle(lh); return 1; }
        _snwprintf(path, HP_WMAX, L"%lshd-pack.txt", g_base);
        /* offline, or no release yet: quietly, next time (said on stderr only,
         * so the log doesn't grow by a line at every launch without Internet) */
        if (fetch_small(path, manifest, sizeof manifest)) {
            CloseHandle(lh); return 0;
        }
    }
    if ((nz = hp_parse_manifest(manifest, &version, zips, MAX_ZIPS)) <= 0) {
        open_log(); hp_log("  the manifest from %s is not one; nothing done\n", hp_u8(local ? src : url));
        CloseHandle(lh); return 1;
    }

    /* installed already? Then nothing is touched, except what an earlier run
     * could not finish: a bar left saying "working", folders not yet set aside */
    /* ...else the zips already installed as they are (same name, size and
     * SHA-256) are neither fetched nor unpacked: a new pack that changes only
     * the movies downloads only them. The textures zips go together (the
     * install replaces textures\hd-pack whole): kept only if all are the same. */
    static int kept[MAX_ZIPS];
    {
        static char have[MANIFEST_MAX]; static HpZip hz[MAX_ZIPS]; int hv = -1, nh = -1;
        if (!hp_join(path, G, L"textures\\hd-pack\\hd-pack.txt") && read_small(path, have, sizeof have) >= 0)
            nh = hp_parse_manifest(have, &hv, hz, MAX_ZIPS);
        if (nh >= 0 && hv == version) {
            if (hp_realesrgan_packW(G) && aside_pending()) { open_log(); set_aside(); }
            CloseHandle(lh); return 0;
        }
        int tex_new = 0, tex_had = 0, tex_same = 0;
        for (int k = 0; k < nh; k++) tex_had += is_textures_zip(hz[k].name);
        for (int i = 0; i < nz; i++) {
            kept[i] = 0;
            for (int k = 0; k < nh && !kept[i]; k++)
                kept[i] = !strcmp(zips[i].name, hz[k].name) && zips[i].size == hz[k].size && !strcmp(zips[i].sha, hz[k].sha);
            if (is_textures_zip(zips[i].name)) { tex_new++; tex_same += kept[i]; }
        }
        if (tex_same != tex_new || tex_new != tex_had)
            for (int i = 0; i < nz; i++) if (is_textures_zip(zips[i].name)) kept[i] = 0;
    }

    open_log();
    hp_log("hd-pack %d: %d zip(s) from %s\n", version, nz, hp_u8(local ? src : url));
    progress("working", 0);

    int ok = 0;
    wchar_t tmp[HP_WMAX], stage[HP_WMAX], moved[HP_WMAX];
    if (hp_join(tmp, G, L"hd-pack-tmp") || hp_join(stage, tmp, L"stage")) goto fail;
    if (hp_mkdir(G, tmp)) { hp_log("  no work folder\n"); goto fail; }

    /* 1. every zip here and checked (downloads first: nothing is unpacked
     *    before all of them are whole) */
    g_total = 0;
    for (int i = 0; i < nz; i++) if (!kept[i]) g_total += zips[i].size;
    g_before = 0;
    for (int i = 0; i < nz; i++) {
        wchar_t zname[128], from[HP_WMAX], zpath[HP_WMAX];
        if (kept[i]) { hp_log("  %s: already installed\n", zips[i].name); continue; }
        if (hp_wide(zips[i].name, zname) || hp_join(zpath, tmp, zname)) { hp_log("  path too long\n"); goto fail; }
        if (!hp_safe_path(G, zpath)) { hp_log("  refused (unsafe path): %s\n", hp_u8(zpath)); goto fail; }
        if (hp_exists(zpath) && !zip_ok(zpath, &zips[i])) {   /* ours from an earlier run, not the right one */
            if (hp_delete_own(G, zpath)) goto fail;
        }
        if (!hp_exists(zpath)) {
            if (local) {
                if (hp_join(from, src, zname)) goto fail;
                if (!CopyFileW(from, zpath, TRUE)) { hp_log("  can't copy %s (error %lu)\n", hp_u8(from), GetLastError()); goto fail; }
            } else {
                wchar_t part[HP_WMAX]; long long have = 0;
                if (wcslen(zpath) + 6 > HP_WMAX || wcslen(g_base) + wcslen(zname) >= HP_WMAX) { hp_log("  path too long\n"); goto fail; }
                _snwprintf(part, HP_WMAX, L"%ls.part", zpath);
                if (hp_isfile(part) && size_of(part, &have)) { hp_log("  can't read the size of %s\n", hp_u8(part)); goto fail; }
                if (have) hp_log("  %s: going on from %lld of %lld bytes\n", zips[i].name, have, zips[i].size);
                _snwprintf(from, HP_WMAX, L"%ls%ls", g_base, zname);
                if (hp_fetchW(g_host, g_port, g_secure, from, zpath, have, zips[i].size)) goto fail;
            }
            if (!zip_ok(zpath, &zips[i])) {
                hp_log("  %s: size or SHA-256 differs from the manifest, deleted\n", zips[i].name);
                hp_delete_own(G, zpath);
                goto fail;
            }
            hp_log("  %s: here and checked\n", zips[i].name);
        } else hp_log("  %s: already here and checked\n", zips[i].name);
        g_before += zips[i].size;
        bar(0);
    }
    /* 2. unpacked, into a stage made now (one left by a killed run: removed if
     * empty, else set aside, never used) */
    if (hp_exists(stage)) {
        if (!hp_isdir(stage)) { hp_log("  %s is not a folder\n", hp_u8(stage)); goto fail; }
        if (hp_rmdir_own(G, stage)) {
            if (hp_aside(G, stage, L"stage-interrompu", moved)) goto fail;
            hp_log("  unfinished stage of an earlier run set aside: %s\n", hp_u8(moved));
        }
    }
    if (hp_mkdir(G, stage)) { hp_log("  can't make %s\n", hp_u8(stage)); goto fail; }
    for (int i = 0; i < nz; i++) {
        wchar_t zname[128], zpath[HP_WMAX];
        if (kept[i]) continue;
        if (hp_wide(zips[i].name, zname) || hp_join(zpath, tmp, zname)) goto fail;
        if (hp_unzip_storedW(zpath, stage)) { hp_log("  %s: can't unpack\n", zips[i].name); goto fail; }
        progress("working", 85 + (i + 1) * 10 / nz);
    }

    wchar_t smark[HP_WMAX];
    if (hp_join(smark, stage, L"hd-pack.txt") || hp_write_own(G, smark, manifest, strlen(manifest), 1)) goto fail;
    if (hp_list_add(&hp_written, smark)) goto fail;
    int inst = hp_installW(G, stage);
    if (inst == -2) { hp_log("  install failed and could not all be undone: see UNDO FAILED above\n"); ok = -1; goto fail; }
    if (inst) goto fail;
    ok = 1;
    progress("working", 95);

    if (hp_realesrgan_packW(G)) set_aside();

    /* tidy up: the verified zips by their names, then the empty folders made */
    for (int i = 0; i < nz; i++) {
        wchar_t zname[128], zpath[HP_WMAX];
        if (!hp_wide(zips[i].name, zname) && !hp_join(zpath, tmp, zname)) hp_delete_own(G, zpath);
    }
    hp_rmdirs_since(G, 0);
    hp_rmdir_own(G, stage);                                    /* ours from an earlier run, if empty */
    hp_rmdir_own(G, tmp);
    if (hp_exists(tmp)) hp_log("  %s is not empty: left in place\n", hp_u8(tmp));
    if (progress("done", 0)) hp_log("  the progress file could not say done\n");
    hp_log("  done\n");
    CloseHandle(lh);
    return 0;

fail:
    /* the files this run unpacked, by exact name; verified zips stay for next time */
    /* (after a failed undo, only the files still in the stage are deleted:
     * the ones left in place are where the log says) */
    for (int i = hp_written.n - 1; i >= 0; i--) hp_delete_own(G, hp_written.v[i]);
    hp_rmdirs_since(G, 0);
    if (!ok) hp_log("  failed: nothing installed, nothing set aside\n");
    if (hp_delete_own(G, PROGRESS) && progress("failed", 0) == 0)   /* no bar left saying "working" */
        hp_log("  the progress file could not be removed: it says failed\n");
    CloseHandle(lh);
    return 1;
}
