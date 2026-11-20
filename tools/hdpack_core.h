/* hd-pack.exe's pure parts: manifest, SHA-256, stored unzip, the path guard,
 * installing the pack and setting the Real-ESRGAN textures aside.
 * Included by tools/hd-pack.c and tests/hdpack/t_hdpack.c (Windows only).
 *
 * Deletion safety (docs/superpowers/plans/2026-11-14-hd-pack-download.md,
 * Global Constraints; after the 2026-11-14 incident):
 *  - nothing here deletes a folder that holds anything, and nothing deletes
 *    recursively: old folders are renamed into game/hd-pack-ancien/;
 *  - the only files deleted are ones this program wrote, by exact name
 *    (hp_delete_own); the only folders removed are empty ones it made
 *    (hp_rmdir_own, RemoveDirectoryW fails on a non-empty folder);
 *  - every path moved, written or deleted goes through hp_safe_path first:
 *    strictly under the game folder, no "..", no reparse point (junction,
 *    symlink, mount point) on it or on any parent up to the game folder;
 *  - wide-char (W) Windows calls only; char* paths are UTF-8.
 * Any doubt: skip, log, go on (or fail the install, which changes nothing).
 * Limits: paths are checked, then used by name, so a link made by another
 * program between the check and the call would be followed (nothing in the
 * project makes links); the small files written in place (lock, progress,
 * log) are opened without following a link and refused if hard-linked.
 * hd-pack-tmp/ and hd-pack-tmp/stage/ are this program's own work folders:
 * removed when empty even if an earlier run made them. */
#ifndef HDPACK_CORE_H
#define HDPACK_CORE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* hd-pack.c and the test each use only some of these */
#pragma GCC diagnostic ignored "-Wunused-function"

#define HP_WMAX 1024        /* longest path handled (wide chars); longer is refused */
#define HP_ANCIEN L"hd-pack-ancien"

typedef struct { char name[128]; long long size; char sha[65]; } HpZip;

/* ---------------------------------------------------------------- log */
static FILE* hp_logfile;    /* hd-pack.exe sets it; NULL = stderr */
static void hp_log(const char* fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(hp_logfile ? hp_logfile : stderr, fmt, a);
    va_end(a);
    if (hp_logfile) fflush(hp_logfile);
}
/* wide -> UTF-8 for the log, 4 rotating buffers */
static const char* hp_u8(const wchar_t* w) {
    static char buf[4][HP_WMAX * 3]; static int k;
    char* o = buf[k++ & 3];
    if (!w || !WideCharToMultiByte(CP_UTF8, 0, w, -1, o, HP_WMAX * 3, NULL, NULL)) strcpy(o, "?");
    return o;
}
/* UTF-8 -> wide; 0 ok */
static int hp_wide(const char* s, wchar_t* out) {
    if (!s) return -1;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, HP_WMAX);
    return n > 0 ? 0 : -1;
}
/* out = a + "\" + b; 0 ok, -1 too long */
static int hp_join(wchar_t* out, const wchar_t* a, const wchar_t* b) {
    size_t la = wcslen(a), lb = wcslen(b);
    if (la + 1 + lb + 1 > HP_WMAX) { out[0] = 0; return -1; }
    wmemmove(out, a, la);
    size_t o = la;
    if (o && out[o - 1] != L'\\' && out[o - 1] != L'/') out[o++] = L'\\';
    wmemmove(out + o, b, lb + 1);
    return 0;
}

/* ----------------------------------------------------------- manifest */
static int hp_is_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
/* a plain file name: [A-Za-z0-9._-], ends ".zip", not only dots */
static int hp_zip_name_ok(const char* s, size_t n) {
    if (n < 5 || n >= 128 || _strnicmp(s + n - 4, ".zip", 4)) return 0;
    if (s[0] == '.') return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}
/* "hd-pack <n>" then "zip <name> <size> <sha256>" lines (LF or CRLF, blank
 * lines allowed). Returns the zip count, or -1 (bad header, bad line, too many). */
static int hp_parse_manifest(const char* text, int* version, HpZip* zips, int max) {
    if (!text) return -1;
    int n = 0, line_no = 0;
    const char* p = text;
    while (*p) {
        const char* e = p; while (*e && *e != '\n') e++;
        size_t len = (size_t)(e - p);
        if (len && p[len - 1] == '\r') len--;
        char line[512];
        if (len >= sizeof line) return -1;
        memcpy(line, p, len); line[len] = 0;
        p = *e ? e + 1 : e;
        if (line_no++ == 0) {
            char* end; long v;
            if (strncmp(line, "hd-pack ", 8) || !line[8] || line[8] < '0' || line[8] > '9') return -1;
            v = strtol(line + 8, &end, 10);
            if (*end || v < 0 || v > 1000000) return -1;
            *version = (int)v;
            continue;
        }
        if (!len) continue;
        /* zip <name> <size> <sha> */
        if (strncmp(line, "zip ", 4)) return -1;
        char* nm = line + 4; char* sp1 = strchr(nm, ' '); if (!sp1) return -1;
        char* sz = sp1 + 1;  char* sp2 = strchr(sz, ' '); if (!sp2) return -1;
        char* sha = sp2 + 1;
        if (!hp_zip_name_ok(nm, (size_t)(sp1 - nm))) return -1;
        long long size = 0;
        if (sz == sp2) return -1;
        for (char* c = sz; c < sp2; c++) {
            if (*c < '0' || *c > '9' || size > 1000000000000LL) return -1;
            size = size * 10 + (*c - '0');
        }
        if (size <= 0 || strlen(sha) != 64) return -1;
        for (int i = 0; i < 64; i++) if (!hp_is_hex(sha[i])) return -1;
        if (n >= max) return -1;
        for (int k = 0; k < n; k++)
            if (strlen(zips[k].name) == (size_t)(sp1 - nm) && !_strnicmp(zips[k].name, nm, (size_t)(sp1 - nm))) return -1;   /* twice the same zip */
        memcpy(zips[n].name, nm, (size_t)(sp1 - nm)); zips[n].name[sp1 - nm] = 0;
        zips[n].size = size;
        for (int i = 0; i < 64; i++) zips[n].sha[i] = (char)((sha[i] >= 'A' && sha[i] <= 'F') ? sha[i] + 32 : sha[i]);
        zips[n].sha[64] = 0;
        n++;
    }
    return line_no ? n : -1;
}

/* ------------------------------------------------------------ SHA-256 */
static int hp_sha256_fileW(const wchar_t* path, char out[65]) {
    out[0] = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    BCRYPT_ALG_HANDLE alg = NULL; BCRYPT_HASH_HANDLE h = NULL;
    int rc = -1;
    static unsigned char buf[1 << 20];
    unsigned char dig[32];
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) goto done;
    if (BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) != 0) goto done;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f, buf, sizeof buf, &got, NULL)) goto done;
        if (!got) break;
        if (BCryptHashData(h, buf, got, 0) != 0) goto done;
    }
    if (BCryptFinishHash(h, dig, 32, 0) != 0) goto done;
    for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", dig[i]);
    rc = 0;
done:
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return rc;
}
static int hp_sha256_file(const char* path, char out[65]) {
    wchar_t w[HP_WMAX];
    out[0] = 0;
    if (hp_wide(path, w)) return -1;
    return hp_sha256_fileW(w, out);
}

/* --------------------------------------------------------- path guard */
/* any segment made only of dots and spaces ("..", "...", ".. "): Windows trims
 * trailing dots and spaces, so ".. " would act as ".." */
static int hp_bad_segments(const wchar_t* p) {
    const wchar_t* s = p;
    for (;;) {
        const wchar_t* e = s; int only = 1, dots = 0;
        while (*e && *e != L'\\' && *e != L'/') { if (*e == L'.') dots++; else if (*e != L' ') only = 0; e++; }
        if (e > s && only && (dots >= 2 || e - s > dots)) return 1;
        if (!*e) return 0;
        s = e + 1;
    }
}
static int hp_bad_chars(const wchar_t* p) {
    if (p[0] == L'\\' && p[1] == L'\\') return 1;              /* \\?\, \\.\, UNC: refused */
    for (const wchar_t* c = p; *c; c++) {
        if (*c < 32 || *c == L'*' || *c == L'?' || *c == L'"' || *c == L'<' || *c == L'>' || *c == L'|') return 1;
        if (*c == L':' && c != p + 1) return 1;                /* only the drive's colon: no streams */
    }
    return 0;
}
/* attributes of exactly this entry, not following a link even if its target
 * is missing; 0 = it exists (*attr set), 1 = it doesn't, -1 = can't tell */
static int hp_entry_attr(const wchar_t* p, DWORD* attr) {
    DWORD a = GetFileAttributesW(p);
    if (a != INVALID_FILE_ATTRIBUTES) { *attr = a; return 0; }
    DWORD e = GetLastError();
    if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) return -1;
    WIN32_FIND_DATAW fd;                                       /* a dangling link still shows here */
    HANDLE h = FindFirstFileW(p, &fd);
    if (h != INVALID_HANDLE_VALUE) { FindClose(h); *attr = fd.dwFileAttributes; return 0; }
    e = GetLastError();
    return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND || e == ERROR_NO_MORE_FILES) ? 1 : -1;
}
static void hp_trim_sep(wchar_t* p) {
    size_t n = wcslen(p);
    while (n > 3 && (p[n - 1] == L'\\' || p[n - 1] == L'/')) p[--n] = 0;
}
/* 1 only if path lies strictly under game, has no "..", and neither it nor
 * any parent up to game (included) is a reparse point. 0 otherwise. */
static int hp_safe_path(const wchar_t* game, const wchar_t* path) {
    wchar_t g[HP_WMAX], p[HP_WMAX];
    if (!game || !path || !game[0] || !path[0]) return 0;
    if (wcslen(game) >= HP_WMAX || wcslen(path) >= HP_WMAX) return 0;
    if (hp_bad_segments(game) || hp_bad_segments(path) || hp_bad_chars(game) || hp_bad_chars(path)) return 0;
    DWORD n = GetFullPathNameW(game, HP_WMAX, g, NULL);
    if (!n || n >= HP_WMAX) return 0;
    n = GetFullPathNameW(path, HP_WMAX, p, NULL);
    if (!n || n >= HP_WMAX) return 0;
    if (hp_bad_chars(g) || hp_bad_chars(p) || hp_bad_segments(p)) return 0;
    hp_trim_sep(g); hp_trim_sep(p);
    size_t gl = wcslen(g);
    if (gl <= 3) return 0;                                     /* a drive root is never the game folder */
    if (wcslen(p) <= gl + 1 || p[gl] != L'\\') return 0;
    if (CompareStringOrdinal(p, (int)gl, g, (int)gl, TRUE) != CSTR_EQUAL) return 0;
    for (;;) {                                                 /* the path, then each parent, game included */
        DWORD a = 0;
        int r = hp_entry_attr(p, &a);
        if (r < 0) return 0;
        if (r == 0 && (a & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
        if (wcslen(p) == gl) break;
        wchar_t* s = wcsrchr(p, L'\\');
        if (!s || (size_t)(s - p) < gl) return 0;
        *s = 0;
    }
    return 1;
}

/* ------------------------------------------------ guarded file actions */
typedef struct { wchar_t** v; int n, cap; } HpList;
static HpList hp_made_dirs;        /* folders this run made (removed only if empty, last first) */
static HpList hp_written;          /* files this run wrote (deleted by exact name on failure) */
static int hp_list_add(HpList* l, const wchar_t* s) {
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 64;
        wchar_t** v = (wchar_t**)realloc(l->v, sizeof(wchar_t*) * (size_t)cap);
        if (!v) return -1;
        l->v = v; l->cap = cap;
    }
    l->v[l->n] = _wcsdup(s);
    if (!l->v[l->n]) return -1;
    l->n++;
    return 0;
}
static void hp_list_clear(HpList* l) { for (int i = 0; i < l->n; i++) free(l->v[i]); l->n = 0; }

static int hp_isdir(const wchar_t* p) { DWORD a; return hp_entry_attr(p, &a) == 0 && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT); }
static int hp_isfile(const wchar_t* p) { DWORD a; return hp_entry_attr(p, &a) == 0 && !(a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)); }
static int hp_exists(const wchar_t* p) { DWORD a; return hp_entry_attr(p, &a) != 1; }   /* "can't tell" counts as there */

/* one folder (its parent must exist); 0 if it is there afterwards */
static int hp_mkdir(const wchar_t* root, const wchar_t* dir) {
    if (!hp_safe_path(root, dir)) { hp_log("  refused (unsafe path): make folder %s\n", hp_u8(dir)); return -1; }
    if (hp_isdir(dir)) return 0;
    if (!CreateDirectoryW(dir, NULL)) { hp_log("  can't make folder %s (error %lu)\n", hp_u8(dir), GetLastError()); return -1; }
    if (hp_list_add(&hp_made_dirs, dir)) { RemoveDirectoryW(dir); hp_log("  out of memory\n"); return -1; }   /* just made, empty */
    return 0;
}
/* rename, same volume, never replacing anything */
static int hp_move(const wchar_t* game, const wchar_t* from, const wchar_t* to) {
    if (!hp_safe_path(game, from) || !hp_safe_path(game, to)) { hp_log("  refused (unsafe path): move %s -> %s\n", hp_u8(from), hp_u8(to)); return -1; }
    if (!MoveFileExW(from, to, MOVEFILE_WRITE_THROUGH)) { hp_log("  can't move %s -> %s (error %lu)\n", hp_u8(from), hp_u8(to), GetLastError()); return -1; }
    return 0;
}
/* the game may be reading a small file of ours this very moment: try again a few times */
static int hp_busy(void) { DWORD e = GetLastError(); return e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION || e == ERROR_ACCESS_DENIED; }
/* delete one file this program wrote, by exact name; a missing file is fine */
static int hp_delete_own(const wchar_t* root, const wchar_t* file) {
    DWORD a;
    int r = hp_entry_attr(file, &a);
    if (r == 1) return 0;
    if (!hp_safe_path(root, file)) { hp_log("  refused (unsafe path): delete %s\n", hp_u8(file)); return -1; }
    if (r != 0 || (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) { hp_log("  refused (not a plain file): delete %s\n", hp_u8(file)); return -1; }
    for (int t = 0; !DeleteFileW(file); t++) {
        if (t >= 20 || !hp_busy()) { hp_log("  can't delete %s (error %lu)\n", hp_u8(file), GetLastError()); return -1; }
        Sleep(100);
    }
    return 0;
}
/* remove one empty folder this program made; a folder with anything in it stays */
static int hp_rmdir_own(const wchar_t* root, const wchar_t* dir) {
    if (!hp_exists(dir)) return 0;
    if (!hp_safe_path(root, dir) || !hp_isdir(dir)) { hp_log("  refused: remove folder %s\n", hp_u8(dir)); return -1; }
    if (!RemoveDirectoryW(dir)) {
        DWORD e = GetLastError();
        if (e != ERROR_DIR_NOT_EMPTY) hp_log("  folder left in place: %s (error %lu)\n", hp_u8(dir), e);
        return -1;
    }
    return 0;
}
/* the folders made since mark, last first, if empty and under root */
static void hp_rmdirs_since(const wchar_t* root, int mark) {
    while (hp_made_dirs.n > mark) {
        wchar_t* d = hp_made_dirs.v[--hp_made_dirs.n];
        if (hp_safe_path(root, d)) hp_rmdir_own(root, d);
        free(d);
    }
}
/* the handle is the file at exactly this path (no link followed on the way:
 * its final path, as the system resolved it, is the path asked for) */
static int hp_handle_is(HANDLE h, const wchar_t* file) {
    wchar_t want[HP_WMAX], lng[HP_WMAX], got[HP_WMAX];
    DWORD n = GetFullPathNameW(file, HP_WMAX, want, NULL);
    if (!n || n >= HP_WMAX) return 0;
    n = GetLongPathNameW(want, lng, HP_WMAX);
    if (n && n < HP_WMAX) wcscpy(want, lng);
    n = GetFinalPathNameByHandleW(h, got, HP_WMAX, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n || n >= HP_WMAX) return 0;
    const wchar_t* g = got;
    if (!wcsncmp(g, L"\\\\?\\UNC\\", 8)) return 0;
    if (!wcsncmp(g, L"\\\\?\\", 4)) g += 4;
    return CompareStringOrdinal(g, -1, want, -1, TRUE) == CSTR_EQUAL;
}
/* open a small file of ours for writing, without following a link and
 * without truncating anything first: refused (closed, nothing deleted) if it
 * is a link (also one Wine shows as a plain file: the final path is checked),
 * a folder, or has another hard link. INVALID_HANDLE_VALUE if not. */
static HANDLE hp_open_own(const wchar_t* root, const wchar_t* file, int retry_if_busy) {
    if (!hp_safe_path(root, file)) { hp_log("  refused (unsafe path): open %s\n", hp_u8(file)); return INVALID_HANDLE_VALUE; }
    HANDLE h;
    for (int t = 0; (h = CreateFileW(file, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL)) == INVALID_HANDLE_VALUE
                    && retry_if_busy && t < 20 && hp_busy(); t++) Sleep(100);
    if (h == INVALID_HANDLE_VALUE) return h;
    BY_HANDLE_FILE_INFORMATION bi;
    if (!GetFileInformationByHandle(h, &bi) || bi.nNumberOfLinks != 1
        || (bi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || !hp_handle_is(h, file)) {
        hp_log("  refused (a link, or hard-linked): %s\n", hp_u8(file));
        CloseHandle(h);
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}
/* write a whole small file (progress, marker): only where nothing but a plain
 * file of ours can be */
static int hp_write_own(const wchar_t* root, const wchar_t* file, const char* data, size_t n, int must_be_new) {
    if (!hp_safe_path(root, file)) { hp_log("  refused (unsafe path): write %s\n", hp_u8(file)); return -1; }
    if (!must_be_new && hp_exists(file) && !hp_isfile(file)) { hp_log("  refused (not a plain file): write %s\n", hp_u8(file)); return -1; }
    HANDLE h = must_be_new ? CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL) : hp_open_own(root, file, 1);
    if (h == INVALID_HANDLE_VALUE) { hp_log("  can't write %s (error %lu)\n", hp_u8(file), GetLastError()); return -1; }
    DWORD w = 0;
    BOOL ok = WriteFile(h, data, (DWORD)n, &w, NULL) && w == n && SetEndOfFile(h);
    ok = CloseHandle(h) && ok;
    if (!ok) { hp_log("  can't write %s (error %lu)\n", hp_u8(file), GetLastError()); return -1; }
    return 0;
}
/* a free name in dir: base, base-2, base-3... (*out); 0 ok */
static int hp_free_name(const wchar_t* dir, const wchar_t* base, wchar_t* out) {
    for (int i = 1; i < 1000; i++) {
        wchar_t nm[300];
        if (wcslen(base) > 250) return -1;
        const wchar_t* dot = wcschr(base, L'.');               /* a.1.mpg -> a-2.1.mpg: the extension stays */
        if (i == 1) wcscpy(nm, base);
        else if (dot) _snwprintf(nm, 300, L"%.*ls-%d%ls", (int)(dot - base), base, i, dot);
        else _snwprintf(nm, 300, L"%ls-%d", base, i);
        nm[299] = 0;
        if (hp_join(out, dir, nm)) return -1;
        if (!hp_exists(out)) return 0;
    }
    return -1;
}
/* rename src into game/hd-pack-ancien/<base>[-N]; the new place in moved_to */
static int hp_aside(const wchar_t* game, const wchar_t* src, const wchar_t* base, wchar_t* moved_to) {
    wchar_t anc[HP_WMAX];
    if (hp_join(anc, game, HP_ANCIEN) || hp_mkdir(game, anc)) return -1;
    if (hp_free_name(anc, base, moved_to)) { hp_log("  no free name in %s for %s\n", hp_u8(anc), hp_u8(base)); return -1; }
    return hp_move(game, src, moved_to);
}

/* --------------------------------------------------------------- unzip */
static unsigned hp_crc_table[256];
static unsigned hp_crc32(unsigned crc, const unsigned char* b, size_t n) {
    if (!hp_crc_table[1]) for (unsigned i = 0; i < 256; i++) { unsigned c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; hp_crc_table[i] = c; }
    crc = ~crc;
    while (n--) crc = hp_crc_table[(crc ^ *b++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
static unsigned hp_u16(const unsigned char* p) { return p[0] | p[1] << 8; }
static unsigned hp_u32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
/* entry names: [A-Za-z0-9._-] segments joined by '/', none empty or only dots */
static int hp_entry_name_ok(const char* s) {
    if (!s[0] || s[0] == '/') return 0;
    const char* seg = s;
    for (const char* c = s;; c++) {
        if (*c == '/' || !*c) {
            size_t len = (size_t)(c - seg);
            int dots = 1;
            for (const char* d = seg; d < c; d++) if (*d != '.') dots = 0;
            if (len == 0 && *c) return 0;                     /* "a//b" */
            if (len && dots) return 0;                         /* ".", "..", "..." */
            if (len && seg[len - 1] == '.') return 0;          /* Windows drops it: "x." is "x" */
            {   /* device names (con, nul.png, com1...) */
                static const char* dev[] = { "con", "prn", "aux", "nul", "com", "lpt", "conin$", "conout$" };
                size_t base = 0; while (seg + base < c && seg[base] != '.') base++;
                for (int k = 0; k < 8; k++) {
                    size_t dl = strlen(dev[k]);
                    if (!_strnicmp(seg, dev[k], dl) && (base == dl || (k >= 4 && k <= 5 && base == dl + 1 && seg[dl] >= '0' && seg[dl] <= '9'))) return 0;
                }
            }
            if (!*c) return 1;
            seg = c + 1;
            continue;
        }
        char ch = *c;
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-')) return 0;
    }
}
static int hp_read_at(HANDLE f, long long pos, void* buf, DWORD n) {
    LARGE_INTEGER li; li.QuadPart = pos;
    if (!SetFilePointerEx(f, li, NULL, FILE_BEGIN)) return -1;
    DWORD got = 0;
    return ReadFile(f, buf, n, &got, NULL) && got == n ? 0 : -1;
}
/* make the folders of rel (a/b/c.png -> dest\a, dest\a\b) */
static int hp_mkparents(const wchar_t* dest, const wchar_t* full) {
    wchar_t t[HP_WMAX]; wcscpy(t, full);
    size_t dl = wcslen(dest);
    for (wchar_t* s = t + dl + 1; *s; s++)
        if (*s == L'\\') { *s = 0; if (hp_mkdir(dest, t)) return -1; *s = L'\\'; }
    return 0;
}
/* Unpack a zip's stored (method 0) entries under dest, reading local headers
 * in order. Refuses anything else (compressed, encrypted, data descriptor,
 * zip64, odd names), never overwrites (CREATE_NEW), checks each CRC-32.
 * Every file and folder made is recorded (hp_written, hp_made_dirs). 0 ok. */
static int hp_unzip_storedW(const wchar_t* zip, const wchar_t* dest) {
    if (!hp_isdir(dest)) { hp_log("  unzip: no folder %s\n", hp_u8(dest)); return -1; }
    HANDLE f = CreateFileW(zip, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) { hp_log("  unzip: can't open %s (error %lu)\n", hp_u8(zip), GetLastError()); return -1; }
    LARGE_INTEGER fs;
    if (!GetFileSizeEx(f, &fs)) { CloseHandle(f); return -1; }
    static unsigned char buf[1 << 20];
    long long pos = 0;
    int entries = 0, rc = -1;
    for (;;) {
        unsigned char h[30];
        if (pos + 4 > fs.QuadPart || hp_read_at(f, pos, h, 4)) { hp_log("  unzip: truncated at %lld\n", pos); goto done; }
        unsigned sig = hp_u32(h);
        if (sig == 0x02014b50 || sig == 0x06054b50) break;     /* central directory: all entries read */
        if (sig != 0x04034b50 || pos + 30 > fs.QuadPart || hp_read_at(f, pos, h, 30)) { hp_log("  unzip: bad header at %lld\n", pos); goto done; }
        unsigned flags = hp_u16(h + 6), method = hp_u16(h + 8), crc = hp_u32(h + 14);
        unsigned csize = hp_u32(h + 18), usize = hp_u32(h + 22), nlen = hp_u16(h + 26), xlen = hp_u16(h + 28);
        if (flags & ~0x0800u) { hp_log("  unzip: flags %#x not handled\n", flags); goto done; }
        if (method != 0 || csize != usize || csize == 0xFFFFFFFFu) { hp_log("  unzip: entry not stored (method %u)\n", method); goto done; }
        if (nlen == 0 || nlen > 255) { hp_log("  unzip: bad name length\n"); goto done; }
        char name[256];
        if (hp_read_at(f, pos + 30, name, nlen)) goto done;
        name[nlen] = 0;
        long long data = pos + 30 + nlen + xlen;
        if (data + csize > fs.QuadPart) { hp_log("  unzip: entry past the end\n"); goto done; }
        int is_dir = name[nlen - 1] == '/';
        if (is_dir) name[nlen - 1] = 0;
        if (!hp_entry_name_ok(name)) { hp_log("  unzip: refused entry name \"%s\"\n", name); goto done; }
        wchar_t rel[HP_WMAX], full[HP_WMAX];
        if (hp_wide(name, rel)) goto done;
        for (wchar_t* c = rel; *c; c++) if (*c == L'/') *c = L'\\';
        if (hp_join(full, dest, rel) || !hp_safe_path(dest, full)) { hp_log("  unzip: refused path %s\n", hp_u8(full)); goto done; }
        if (hp_mkparents(dest, full)) goto done;
        if (is_dir) {
            if (csize) goto done;
            if (hp_mkdir(dest, full)) goto done;
        } else {
            HANDLE o = CreateFileW(full, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (o == INVALID_HANDLE_VALUE) { hp_log("  unzip: can't make %s (error %lu)\n", hp_u8(full), GetLastError()); goto done; }
            if (hp_list_add(&hp_written, full)) { CloseHandle(o); DeleteFileW(full); hp_log("  out of memory\n"); goto done; }   /* just made by CREATE_NEW */
            LARGE_INTEGER li; li.QuadPart = data;
            unsigned c = 0; long long left = csize; int ok = SetFilePointerEx(f, li, NULL, FILE_BEGIN) != 0;
            while (ok && left > 0) {
                DWORD want = left > (long long)sizeof buf ? (DWORD)sizeof buf : (DWORD)left, got = 0, put = 0;
                ok = ReadFile(f, buf, want, &got, NULL) && got == want;
                if (ok) { c = hp_crc32(c, buf, got); ok = WriteFile(o, buf, got, &put, NULL) && put == got; }
                left -= got;
            }
            ok = CloseHandle(o) && ok;
            if (!ok) { hp_log("  unzip: write failed for %s (error %lu)\n", hp_u8(full), GetLastError()); goto done; }
            if (c != crc) { hp_log("  unzip: CRC mismatch for %s\n", hp_u8(full)); goto done; }
        }
        entries++;
        pos = data + csize;
    }
    if (!entries) { hp_log("  unzip: no entries in %s\n", hp_u8(zip)); goto done; }
    rc = 0;
done:
    CloseHandle(f);
    return rc;
}
static int hp_unzip_stored(const char* zip, const char* dest) {
    wchar_t z[HP_WMAX], d[HP_WMAX];
    if (hp_wide(zip, z) || hp_wide(dest, d)) return -1;
    return hp_unzip_storedW(z, d);
}

/* ----------------------------------------------------- set-aside rule */
/* 1 if the textures of textures/hd are Real-ESRGAN ones (textures-hd's own),
 * which the pack replaces: textures/hd-style.txt without "service-", or no
 * such file but textures-hd-outil/ there. */
static int hp_realesrgan_packW(const wchar_t* game) {
    wchar_t p[HP_WMAX];
    if (hp_join(p, game, L"textures\\hd-style.txt")) return 0;
    /* any doubt (can't open or read it, empty: textures-hd.exe rewriting it) = keep everything */
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        static char b[65536 + 1]; DWORD got = 0;
        BOOL ok = ReadFile(f, b, sizeof b - 1, &got, NULL);
        CloseHandle(f);
        if (!ok || got == 0 || got == sizeof b - 1) return 0;
        b[got] = 0;
        for (DWORD i = 0; i < got; i++) if (!b[i]) b[i] = ' ';
        return strstr(b, "service-") == NULL;
    }
    DWORD e = GetLastError();
    if (e != ERROR_FILE_NOT_FOUND) return 0;
    if (hp_join(p, game, L"textures-hd-outil")) return 0;
    return hp_isdir(p);
}
static int hp_realesrgan_pack(const char* game) {
    wchar_t g[HP_WMAX];
    if (hp_wide(game, g)) return 0;
    return hp_realesrgan_packW(g);
}

/* Rename each Real-ESRGAN folder (textures/hd, textures-methode5,
 * textures-avant-*) into game/hd-pack-ancien/ (-2, -3... if the name is
 * taken). One rename per folder, nothing deleted; a folder that is a link or
 * fails the guard is left where it is. Returns how many could not be moved. */
static int hp_set_aside_moved;     /* how many folders the last hp_set_aside moved */
static int hp_set_asideW(const wchar_t* game) {
    int failed = 0;
    hp_set_aside_moved = 0;
    wchar_t src[HP_WMAX], to[HP_WMAX];
    static const wchar_t* fixed[][2] = { { L"textures\\hd", L"textures-hd" }, { L"textures-methode5", L"textures-methode5" } };
    for (int i = 0; i < 2; i++) {
        if (hp_join(src, game, fixed[i][0])) { failed++; continue; }
        DWORD a;
        int r = hp_entry_attr(src, &a);
        if (r == 1) continue;
        if (r != 0 || !(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT)) {
            hp_log("  left in place (not a plain folder): %s\n", hp_u8(src)); failed++; continue;
        }
        if (hp_aside(game, src, fixed[i][1], to)) failed++;
        else { hp_set_aside_moved++; hp_log("  set aside: %s -> %s\n", hp_u8(src), hp_u8(to)); }
    }
    /* textures-avant-*: the names first, then the moves (the folder changes) */
    wchar_t pat[HP_WMAX];
    if (hp_join(pat, game, L"textures-avant-*")) return failed + 1;
    HpList names = { 0 };
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                hp_log("  left in place (a link): %s\n", hp_u8(fd.cFileName)); failed++; continue;
            }
            if (hp_list_add(&names, fd.cFileName)) failed++;
        } while (FindNextFileW(h, &fd));
        if (GetLastError() != ERROR_NO_MORE_FILES) failed++;
        FindClose(h);
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) failed++;
    for (int i = 0; i < names.n; i++) {
        if (hp_join(src, game, names.v[i]) || hp_aside(game, src, names.v[i], to)) failed++;
        else { hp_set_aside_moved++; hp_log("  set aside: %s -> %s\n", hp_u8(src), hp_u8(to)); }
    }
    hp_list_clear(&names); free(names.v);
    return failed;
}
static int hp_set_aside(const char* game) {
    wchar_t g[HP_WMAX];
    if (hp_wide(game, g)) return -1;
    return hp_set_asideW(g);
}

/* ------------------------------------------------------------- install */
typedef struct { wchar_t from[HP_WMAX], to[HP_WMAX]; } HpMove;
/* the part of a movie name before its first dot: a.1.mpg, a.sig -> a */
static void hp_stem(const wchar_t* name, wchar_t* out, size_t max) {
    size_t i = 0;
    while (name[i] && name[i] != L'.' && i + 1 < max) { out[i] = name[i]; i++; }
    out[i] = 0;
}
/* plain file names of a folder (no folders, no links); -1 if anything else is in it */
static int hp_list_files(const wchar_t* dir, HpList* out) {
    wchar_t pat[HP_WMAX];
    if (hp_join(pat, dir, L"*")) return -1;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    int rc = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) { rc = -1; continue; }
        if (hp_list_add(out, fd.cFileName)) rc = -1;
    } while (FindNextFileW(h, &fd));
    if (GetLastError() != ERROR_NO_MORE_FILES) rc = -1;
    FindClose(h);
    return rc;
}

/* Returns 0 ok, -1 failed with everything put back, -2 failed and some move
 * could not be undone (the log says where each file is).
 * Put stage/textures in place as game/textures/hd-pack (the old one, if any,
 * set aside; no stage/textures: the installed one is kept, its old marker set
 * aside), the stage movies into game/movies (older files of the same
 * movies kept in game/movies-avant-hd-pack/), and stage/hd-pack.txt last as
 * game/textures/hd-pack/hd-pack.txt. All renames, all checked first; on any
 * error every move done is undone, so nothing is half moved. 0 ok. */
static int hp_installW(const wchar_t* game, const wchar_t* stage) {
    wchar_t stex[HP_WMAX], smov[HP_WMAX], smark[HP_WMAX], tex[HP_WMAX], dtex[HP_WMAX], dmov[HP_WMAX], bak[HP_WMAX], dmark[HP_WMAX];
    if (hp_join(stex, stage, L"textures") || hp_join(smov, stage, L"movies") || hp_join(smark, stage, L"hd-pack.txt")
        || hp_join(tex, game, L"textures") || hp_join(dtex, game, L"textures\\hd-pack") || hp_join(dmov, game, L"movies")
        || hp_join(bak, game, L"movies-avant-hd-pack") || hp_join(dmark, dtex, L"hd-pack.txt")) return -1;
    HpList newmov = { 0 }, oldmov = { 0 };
    HpMove* done = NULL; int ndone = 0, cap = 0, rc = -1, keep;
    int mark = hp_made_dirs.n;

    /* ---- checks, nothing changed yet */
    if (!hp_safe_path(game, stage) || !hp_isdir(stage)) { hp_log("  install: stage refused: %s\n", hp_u8(stage)); goto out; }
    /* no stage textures: the pack's textures zips are those already installed
     * (hd-pack.exe skipped them), so textures/hd-pack stays as it is */
    keep = hp_safe_path(game, stex) && !hp_exists(stex);
    if (keep ? !(hp_safe_path(game, dtex) && hp_isdir(dtex) && hp_isfile(dmark)) : !(hp_safe_path(game, stex) && hp_isdir(stex))) {
        hp_log("  install: no stage textures folder%s\n", keep ? ", and no installed pack to keep" : ""); goto out;
    }
    if (!hp_safe_path(game, smark) || !hp_isfile(smark)) { hp_log("  install: no stage hd-pack.txt\n"); goto out; }
    {   /* the stage holds textures/, movies/, hd-pack.txt and nothing else (nothing left behind) */
        wchar_t pat[HP_WMAX]; WIN32_FIND_DATAW fd; int bad = 0;
        if (hp_join(pat, stage, L"*")) goto out;
        HANDLE h = FindFirstFileW(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) goto out;
        do {
            const wchar_t* nm = fd.cFileName;
            if (wcscmp(nm, L".") && wcscmp(nm, L"..") && _wcsicmp(nm, L"textures") && _wcsicmp(nm, L"movies") && _wcsicmp(nm, L"hd-pack.txt")) {
                hp_log("  install: unexpected %s in the stage\n", hp_u8(nm)); bad = 1;
            }
        } while (FindNextFileW(h, &fd));
        if (GetLastError() != ERROR_NO_MORE_FILES) bad = 1;
        FindClose(h);
        if (bad) goto out;
    }
    if (!hp_safe_path(game, dtex) || (hp_exists(dtex) && !hp_isdir(dtex))) { hp_log("  install: textures\\hd-pack refused (a link or not a folder)\n"); goto out; }
    if (hp_exists(tex) && (!hp_safe_path(game, tex) || !hp_isdir(tex))) { hp_log("  install: textures refused\n"); goto out; }
    if (hp_exists(smov)) {
        if (!hp_safe_path(game, smov) || !hp_isdir(smov) || hp_list_files(smov, &newmov)) { hp_log("  install: stage movies refused\n"); goto out; }
    }
    if (newmov.n) {
        if (!hp_safe_path(game, dmov) || (hp_exists(dmov) && !hp_isdir(dmov))) { hp_log("  install: movies folder refused (a link or not a folder)\n"); goto out; }
        if (!hp_safe_path(game, bak) || (hp_exists(bak) && !hp_isdir(bak))) { hp_log("  install: movies-avant-hd-pack refused\n"); goto out; }
        if (hp_isdir(dmov)) {
            HpList all = { 0 };
            wchar_t pat[HP_WMAX]; WIN32_FIND_DATAW fd;
            if (hp_join(pat, dmov, L"*")) goto out;
            HANDLE h = FindFirstFileW(pat, &fd);
            int bad = 0;
            if (h != INVALID_HANDLE_VALUE) {
                do { if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..") && hp_list_add(&all, fd.cFileName)) bad = 1; } while (FindNextFileW(h, &fd));
                if (GetLastError() != ERROR_NO_MORE_FILES) bad = 1;
                FindClose(h);
            } else if (GetLastError() != ERROR_FILE_NOT_FOUND) bad = 1;
            if (bad) hp_log("  install: can't list the movies folder\n");
            for (int i = 0; i < all.n && !bad; i++) {            /* old files of a movie the pack brings */
                wchar_t s1[260], s2[260], p[HP_WMAX];
                hp_stem(all.v[i], s1, 260);
                int match = 0;
                for (int k = 0; k < newmov.n && !match; k++) { hp_stem(newmov.v[k], s2, 260); match = s1[0] && !_wcsicmp(s1, s2); }
                if (!match) continue;
                if (hp_join(p, dmov, all.v[i]) || !hp_safe_path(game, p) || !hp_isfile(p)) { hp_log("  install: can't set aside %s (not a plain file)\n", hp_u8(all.v[i])); bad = 1; }
                else if (hp_list_add(&oldmov, all.v[i])) bad = 1;
            }
            hp_list_clear(&all); free(all.v);
            if (bad) goto out;
        }
    }
    cap = 4 + newmov.n + oldmov.n;
    done = (HpMove*)calloc((size_t)cap, sizeof(HpMove));
    if (!done) goto out;

    /* ---- the moves, each one noted to be undone */
#define HP_DO(F, T) do { if (ndone >= cap || hp_move(game, (F), (T))) goto undo; \
        wcscpy(done[ndone].from, (F)); wcscpy(done[ndone].to, (T)); ndone++; } while (0)
    if (!keep) {
        if (!hp_isdir(tex) && hp_mkdir(game, tex)) goto undo;
        if (hp_isdir(dtex)) {
            wchar_t anc[HP_WMAX], to[HP_WMAX];
            if (hp_join(anc, game, HP_ANCIEN) || hp_mkdir(game, anc) || hp_free_name(anc, L"hd-pack", to)) goto undo;
            HP_DO(dtex, to);
            hp_log("  old pack set aside: %s\n", hp_u8(to));
        }
        HP_DO(stex, dtex);
    }
    if (newmov.n) {
        if (!hp_isdir(dmov) && hp_mkdir(game, dmov)) goto undo;
        if (oldmov.n && !hp_isdir(bak) && hp_mkdir(game, bak)) goto undo;
        for (int i = 0; i < oldmov.n; i++) {
            wchar_t from[HP_WMAX], to[HP_WMAX];
            if (hp_join(from, dmov, oldmov.v[i]) || hp_free_name(bak, oldmov.v[i], to)) goto undo;
            HP_DO(from, to);
        }
        for (int i = 0; i < newmov.n; i++) {
            wchar_t from[HP_WMAX], to[HP_WMAX];
            if (hp_join(from, smov, newmov.v[i]) || hp_join(to, dmov, newmov.v[i])) goto undo;
            if (hp_exists(to)) { hp_log("  install: %s is still there\n", hp_u8(to)); goto undo; }
            HP_DO(from, to);
        }
    }
    if (keep) {                                                /* the old marker set aside, not deleted */
        wchar_t anc[HP_WMAX], to[HP_WMAX];
        if (hp_join(anc, game, HP_ANCIEN) || hp_mkdir(game, anc) || hp_free_name(anc, L"hd-pack.txt", to)) goto undo;
        HP_DO(dmark, to);
    }
    HP_DO(smark, dmark);                                       /* last: the pack counts as installed */
#undef HP_DO
    hp_log("  installed: textures\\hd-pack%s, %d movie file(s) (%d older kept in movies-avant-hd-pack)\n", keep ? " (kept)" : "", newmov.n, oldmov.n);
    rc = 0;
    goto out;
undo:
    hp_log("  install failed: undoing %d move(s)\n", ndone);
    while (ndone > 0) {
        ndone--;
        if (hp_move(game, done[ndone].to, done[ndone].from)) {
            hp_log("  UNDO FAILED: %s is at %s\n", hp_u8(done[ndone].from), hp_u8(done[ndone].to));
            rc = -2;
        }
    }
    hp_rmdirs_since(game, mark);                               /* only empty folders made here */
out:
    free(done);
    hp_list_clear(&newmov); free(newmov.v);
    hp_list_clear(&oldmov); free(oldmov.v);
    return rc;
}
static int hp_install(const char* game, const char* stage) {
    wchar_t g[HP_WMAX], s[HP_WMAX];
    if (hp_wide(game, g) || hp_wide(stage, s)) return -1;
    return hp_installW(g, s);
}

#endif
