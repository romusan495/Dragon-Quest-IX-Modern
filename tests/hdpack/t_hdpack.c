/* Tests of tools/hdpack_core.h and hd-pack.exe. Run by tests/hdpack/run.sh
 * only (Linux cloud session, Wine), never on a Windows PC:
 *
 *   t_hdpack.exe <temp folder made by run.sh> <hd-pack.exe>
 *
 * Every file it makes, moves or deletes is under <temp folder>\w. The links
 * it uses were made by run.sh under <temp folder>\fx and point inside it. */
#include "hdpack_core.h"
#include <winioctl.h>

static wchar_t T[HP_WMAX], W[HP_WMAX], EXE[HP_WMAX];
static int g_pass, g_fail, g_skip;

#define CHECK(name, cond) do { if (cond) { g_pass++; printf("PASS %s\n", name); } \
    else { g_fail++; printf("FAIL %s  (%s:%d: %s)\n", name, __FILE__, __LINE__, #cond); } } while (0)
static void skip(const char* name, const char* why) { g_skip++; printf("SKIP %s: %s\n", name, why); }

/* ---- small helpers (test side) ---- */
static const wchar_t* P(const wchar_t* fmt, ...) {           /* path from a format, 8 rotating buffers */
    static wchar_t buf[8][HP_WMAX]; static int k;
    wchar_t* o = buf[k++ & 7];
    va_list a; va_start(a, fmt); _vsnwprintf(o, HP_WMAX - 1, fmt, a); va_end(a);
    o[HP_WMAX - 1] = 0;
    return o;
}
static const char* U(const wchar_t* w) {                      /* wide -> UTF-8, 8 rotating buffers */
    static char buf[8][HP_WMAX * 3]; static int k;
    char* o = buf[k++ & 7];
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, o, HP_WMAX * 3, NULL, NULL)) o[0] = 0;
    return o;
}
static void mk(const wchar_t* d) {                            /* make a folder and its parents */
    wchar_t t[HP_WMAX]; wcscpy(t, d);
    for (wchar_t* s = t + 3; *s; s++) if (*s == L'\\') { *s = 0; CreateDirectoryW(t, NULL); *s = L'\\'; }
    CreateDirectoryW(t, NULL);
}
static void put(const wchar_t* f, const char* data, size_t n) {
    HANDLE h = CreateFileW(f, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("  (test setup: cannot write %s)\n", U(f)); return; }
    DWORD w; WriteFile(h, data, (DWORD)n, &w, NULL); CloseHandle(h);
}
static void puts_(const wchar_t* f, const char* s) { put(f, s, strlen(s)); }
static int get(const wchar_t* f, char* out, int max) {        /* bytes read, -1 if none */
    HANDLE h = CreateFileW(f, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD r = 0; ReadFile(h, out, (DWORD)max - 1, &r, NULL); CloseHandle(h);
    out[r] = 0; return (int)r;
}
static int is(const wchar_t* f, const char* s) { char b[4096]; return get(f, b, sizeof b) >= 0 && !strcmp(b, s); }
static int exists(const wchar_t* f) { return GetFileAttributesW(f) != INVALID_FILE_ATTRIBUTES; }
static int isdir(const wchar_t* f) { DWORD a = GetFileAttributesW(f); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static int isreparse(const wchar_t* f) { DWORD a = 0; return hp_entry_attr(f, &a) == 0 && (a & FILE_ATTRIBUTE_REPARSE_POINT); }   /* the link itself, even dangling */
static int count(const wchar_t* dir) {                        /* entries, . and .. left out */
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(P(L"%ls\\*", dir), &fd); int n = 0;
    if (h == INVALID_HANDLE_VALUE) return -1;
    do { if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) n++; } while (FindNextFileW(h, &fd));
    FindClose(h); return n;
}
static int fill(const wchar_t* f, int byte, int n) {          /* file holds n times byte */
    char b[4096]; int r = get(f, b, sizeof b);
    if (r != n) return 0;
    for (int i = 0; i < n; i++) if ((unsigned char)b[i] != byte) return 0;
    return 1;
}
static DWORD run_exe(const wchar_t* exe, const wchar_t* source) {   /* exit code, or 999 */
    if (source) SetEnvironmentVariableW(L"HDPACK_SOURCE", source); else SetEnvironmentVariableW(L"HDPACK_SOURCE", NULL);
    STARTUPINFOW si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof si); si.cb = sizeof si;
    wchar_t cmd[HP_WMAX]; _snwprintf(cmd, HP_WMAX, L"\"%ls\"", exe);
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 999;
    DWORD code = 999;
    if (WaitForSingleObject(pi.hProcess, 120000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 998);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    SetEnvironmentVariableW(L"HDPACK_SOURCE", NULL);
    return code;
}
static void new_game(const wchar_t* g) {                      /* a game folder with hd-pack.exe in it */
    mk(g);
    CopyFileW(EXE, P(L"%ls\\hd-pack.exe", g), FALSE);
}

/* ---- tests ---- */
static void t_manifest(void) {
    HpZip z[4]; int v = -7;
    const char* good = "hd-pack 1\nzip hd-pack-textures-1.zip 1234 ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n"
                       "zip hd-pack-movies.zip 99 BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD\n";
    int n = hp_parse_manifest(good, &v, z, 4);
    CHECK("manifest: good", n == 2 && v == 1 && !strcmp(z[0].name, "hd-pack-textures-1.zip") && z[0].size == 1234
          && !strcmp(z[1].sha, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") && z[1].size == 99);
    const char* crlf = "hd-pack 3\r\nzip a.zip 5 ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\r\n\r\n";
    n = hp_parse_manifest(crlf, &v, z, 4);
    CHECK("manifest: CRLF", n == 1 && v == 3 && !strcmp(z[0].name, "a.zip") && z[0].size == 5);
    CHECK("manifest: bad header", hp_parse_manifest("hdpack 1\n", &v, z, 4) == -1);
    CHECK("manifest: header without number", hp_parse_manifest("hd-pack x\n", &v, z, 4) == -1);
    CHECK("manifest: empty", hp_parse_manifest("", &v, z, 4) == -1);
    CHECK("manifest: zip name with a path", hp_parse_manifest("hd-pack 1\nzip ../a.zip 5 ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n", &v, z, 4) == -1);
    CHECK("manifest: zip name with a backslash", hp_parse_manifest("hd-pack 1\nzip x\\a.zip 5 ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n", &v, z, 4) == -1);
    CHECK("manifest: short sha", hp_parse_manifest("hd-pack 1\nzip a.zip 5 ba7816bf\n", &v, z, 4) == -1);
    CHECK("manifest: bad size", hp_parse_manifest("hd-pack 1\nzip a.zip -5 ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n", &v, z, 4) == -1);
    CHECK("manifest: unknown line", hp_parse_manifest("hd-pack 1\nrm -rf a\n", &v, z, 4) == -1);
    CHECK("manifest: the same zip twice", hp_parse_manifest("hd-pack 1\nzip a.zip 5 ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n"
          "zip A.zip 5 ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n", &v, z, 4) == -1);
    CHECK("manifest: too many zips", hp_parse_manifest(good, &v, z, 1) == -1);
}

static void t_sha(void) {
    wchar_t f[HP_WMAX]; wcscpy(f, P(L"%ls\\abc.txt", W));
    puts_(f, "abc");
    char out[65] = "";
    int r = hp_sha256_file(U(f), out);
    CHECK("sha256 of \"abc\"", r == 0 && !strcmp(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK("sha256 of a missing file fails", hp_sha256_file(U(P(L"%ls\\none.txt", W)), out) != 0);
}

static void t_unzip(void) {
    wchar_t d[HP_WMAX]; wcscpy(d, P(L"%ls\\unz", W)); mk(d);
    int r = hp_unzip_stored(U(P(L"%ls\\pack\\hd-pack-movies.zip", T)), U(d));
    CHECK("unzip: movies zip made by Task 3", r == 0 && fill(P(L"%ls\\movies\\a.1.mpg", d), 'M', 300) && fill(P(L"%ls\\movies\\a.sig", d), 'S', 20));
    r = hp_unzip_stored(U(P(L"%ls\\pack\\hd-pack-textures-1.zip", T)), U(d))
      | hp_unzip_stored(U(P(L"%ls\\pack\\hd-pack-textures-2.zip", T)), U(d));
    int ok = r == 0;
    for (int i = 0; i < 5; i++) ok = ok && fill(P(L"%ls\\textures\\%016x.png", d, 0xabc0 + i), i + 1, 600);
    CHECK("unzip: texture zips made by Task 3", ok);
    CHECK("unzip: refuses to overwrite a file already there", hp_unzip_stored(U(P(L"%ls\\pack\\hd-pack-movies.zip", T)), U(d)) != 0
          && fill(P(L"%ls\\movies\\a.1.mpg", d), 'M', 300));
    static const wchar_t* evil[] = { L"dotdot", L"dotdot2", L"backslash", L"absolute", L"drive", L"deflate", L"device", L"trailingdot" };
    for (int i = 0; i < 8; i++) {
        wchar_t e[HP_WMAX]; wcscpy(e, P(L"%ls\\evil-%ls\\in", W, evil[i])); mk(e);
        r = hp_unzip_stored(U(P(L"%ls\\evil\\%ls.zip", T, evil[i])), U(e));
        char name[80]; snprintf(name, sizeof name, "unzip: refuses %s", U(evil[i]));
        CHECK(name, r != 0 && !exists(P(L"%ls\\evil-%ls\\evil.txt", W, evil[i])) && !exists(P(L"%ls\\evil.txt", W))
              && !exists(L"C:\\evil.txt") && count(e) == 0);
    }
    CHECK("unzip: a missing zip fails", hp_unzip_stored(U(P(L"%ls\\nothing.zip", T)), U(d)) != 0);
}

/* a real junction, made only inside the test's own folder; Wine 9 cannot */
typedef struct { DWORD Tag; WORD Len, Res, SubOff, SubLen, PrOff, PrLen; WCHAR Buf[1]; } TJunction;
static int make_junction(const wchar_t* link, const wchar_t* target) {
    if (!CreateDirectoryW(link, NULL)) return 0;
    HANDLE h = CreateFileW(link, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) { RemoveDirectoryW(link); return 0; }
    wchar_t sub[HP_WMAX]; _snwprintf(sub, HP_WMAX, L"\\??\\%ls", target);
    size_t sl = wcslen(sub) * 2, pl = wcslen(target) * 2;
    static char buf[8192]; memset(buf, 0, sizeof buf);
    TJunction* j = (TJunction*)buf;
    j->Tag = IO_REPARSE_TAG_MOUNT_POINT; j->SubLen = (WORD)sl; j->PrOff = (WORD)(sl + 2); j->PrLen = (WORD)pl;
    memcpy(j->Buf, sub, sl); memcpy((char*)j->Buf + sl + 2, target, pl);
    j->Len = (WORD)(8 + sl + 2 + pl + 2);
    DWORD got; BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buf, 8 + j->Len, NULL, 0, &got, NULL);
    CloseHandle(h);
    if (!ok) RemoveDirectoryW(link);                           /* the empty folder made just above */
    return ok != 0;
}

static void t_safe_path(void) {
    wchar_t g[HP_WMAX]; wcscpy(g, P(L"%ls\\sp\\game", W)); mk(g);
    wchar_t sib[HP_WMAX]; wcscpy(sib, P(L"%ls\\sp\\game2", W)); mk(sib);
    CHECK("safe_path: a path under the game folder", hp_safe_path(g, P(L"%ls\\textures\\hd\\x.png", g)) == 1);
    CHECK("safe_path: forward slashes under the game folder", hp_safe_path(g, P(L"%ls/textures/x.png", g)) == 1);
    CHECK("safe_path: the game folder itself is refused", hp_safe_path(g, g) == 0 && hp_safe_path(g, P(L"%ls\\", g)) == 0);
    CHECK("safe_path: a sibling folder is refused", hp_safe_path(g, P(L"%ls\\sp\\other\\x", W)) == 0);
    CHECK("safe_path: same prefix, other folder (game2) is refused", hp_safe_path(g, P(L"%ls\\x", sib)) == 0);
    CHECK("safe_path: .. is refused", hp_safe_path(g, P(L"%ls\\..\\game2\\x", g)) == 0);
    CHECK("safe_path: .. that stays inside is refused too", hp_safe_path(g, P(L"%ls\\a\\..\\b", g)) == 0);
    CHECK("safe_path: '.. ' and '...' are refused", hp_safe_path(g, P(L"%ls\\.. \\x", g)) == 0 && hp_safe_path(g, P(L"%ls\\...\\x", g)) == 0);
    CHECK("safe_path: a stream name is refused", hp_safe_path(g, P(L"%ls\\x.txt:s", g)) == 0);
    CHECK("safe_path: wildcards are refused", hp_safe_path(g, P(L"%ls\\*", g)) == 0);
    CHECK("safe_path: \\\\?\\ paths are refused", hp_safe_path(g, P(L"\\\\?\\%ls\\x", g)) == 0);
    CHECK("safe_path: a drive root as game folder is refused", hp_safe_path(L"C:\\", L"C:\\Windows\\x") == 0);
    CHECK("safe_path: relative path outside", hp_safe_path(g, L"x\\y") == 0);
    CHECK("safe_path: empty", hp_safe_path(g, L"") == 0 && hp_safe_path(L"", P(L"%ls\\x", g)) == 0);

    /* real junction game\j -> sibling folder, both inside this test's folder */
    wchar_t jt[HP_WMAX]; wcscpy(jt, P(L"%ls\\sp\\target", W)); mk(jt); puts_(P(L"%ls\\t.txt", jt), "target");
    if (make_junction(P(L"%ls\\j", g), jt)) {
        CHECK("safe_path: junction to a sibling folder is refused", hp_safe_path(g, P(L"%ls\\j", g)) == 0 && hp_safe_path(g, P(L"%ls\\j\\t.txt", g)) == 0);
    } else skip("safe_path: real junction", "Wine cannot create junctions (FSCTL_SET_REPARSE_POINT not supported); the symlink cases below stand in");

    /* links made by run.sh (Wine shows a Unix symlink as a reparse point) */
    wchar_t fg[HP_WMAX]; wcscpy(fg, P(L"%ls\\fx\\sp\\game", T));
    if (!isreparse(P(L"%ls\\lnk", fg))) {
        CHECK("safe_path: Wine reports run.sh's link as a reparse point (test is meaningful)", 0);
    } else {
        CHECK("safe_path: link folder to a sibling is refused", hp_safe_path(fg, P(L"%ls\\lnk", fg)) == 0);
        CHECK("safe_path: a file through the link is refused", hp_safe_path(fg, P(L"%ls\\lnk\\f.txt", fg)) == 0);
        CHECK("safe_path: a missing file under the link is refused", hp_safe_path(fg, P(L"%ls\\lnk\\new\\x", fg)) == 0);
        CHECK("safe_path: a game folder that is itself a link is refused",
              hp_safe_path(P(L"%ls\\fx\\sp\\gamelink", T), P(L"%ls\\fx\\sp\\gamelink\\x", T)) == 0);
        CHECK("safe_path: the link's target is untouched", is(P(L"%ls\\fx\\sp\\sib\\f.txt", T), "sibling\n"));
    }
}

static void t_rule(void) {
    wchar_t g[HP_WMAX]; wcscpy(g, P(L"%ls\\rule", W)); mk(P(L"%ls\\textures", g));
    puts_(P(L"%ls\\textures\\hd-style.txt", g), "ia-seule 1\n");
    CHECK("rule: 'ia-seule 1' is Real-ESRGAN", hp_realesrgan_pack(U(g)) == 1);
    puts_(P(L"%ls\\textures\\hd-style.txt", g), "ia-seule 1 service-auto 1\n");
    CHECK("rule: 'service-' is not", hp_realesrgan_pack(U(g)) == 0);
    DeleteFileW(P(L"%ls\\textures\\hd-style.txt", g));
    CHECK("rule: no file, no tool: not", hp_realesrgan_pack(U(g)) == 0);
    mk(P(L"%ls\\textures-hd-outil", g));
    CHECK("rule: no file + textures-hd-outil: Real-ESRGAN", hp_realesrgan_pack(U(g)) == 1);
    puts_(P(L"%ls\\textures\\hd-style.txt", g), "");
    CHECK("rule: an empty file (being rewritten) is a doubt: not", hp_realesrgan_pack(U(g)) == 0);
    puts_(P(L"%ls\\textures\\hd-style.txt", g), "service-auto 1\n");
    HANDLE busy = CreateFileW(P(L"%ls\\textures\\hd-style.txt", g), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    CHECK("rule: a file that can't be opened is a doubt: not", hp_realesrgan_pack(U(g)) == 0);
    CloseHandle(busy);
}

static void t_set_aside(void) {
    wchar_t g[HP_WMAX]; wcscpy(g, P(L"%ls\\sa\\game", W));
    mk(P(L"%ls\\textures\\hd\\sub", g)); mk(P(L"%ls\\textures\\perso", g)); mk(P(L"%ls\\textures-methode5", g));
    mk(P(L"%ls\\textures-avant-2025", g)); mk(P(L"%ls\\hd-pack-ancien\\textures-hd", g));
    puts_(P(L"%ls\\textures\\hd\\0000000000000001.png", g), "one");
    puts_(P(L"%ls\\textures\\hd\\sub\\0000000000000002.png", g), "two");
    puts_(P(L"%ls\\textures\\perso\\0000000000000003.png", g), "mine");
    puts_(P(L"%ls\\textures-methode5\\0000000000000004.png", g), "m5");
    puts_(P(L"%ls\\textures-avant-2025\\0000000000000005.png", g), "avant");
    puts_(P(L"%ls\\textures-avant-fichier", g), "a file, not a folder");
    puts_(P(L"%ls\\hd-pack-ancien\\textures-hd\\older.png", g), "older");
    int r = hp_set_aside(U(g));
    CHECK("set_aside_moves_not_deletes: returns 0", r == 0);
    CHECK("set_aside_moves_not_deletes: textures/hd found intact (as textures-hd-2, the name was taken)",
          is(P(L"%ls\\hd-pack-ancien\\textures-hd-2\\0000000000000001.png", g), "one")
          && is(P(L"%ls\\hd-pack-ancien\\textures-hd-2\\sub\\0000000000000002.png", g), "two")
          && !exists(P(L"%ls\\textures\\hd", g)));
    CHECK("set_aside_moves_not_deletes: what was already set aside is kept", is(P(L"%ls\\hd-pack-ancien\\textures-hd\\older.png", g), "older"));
    CHECK("set_aside_moves_not_deletes: textures-methode5 and textures-avant-*",
          is(P(L"%ls\\hd-pack-ancien\\textures-methode5\\0000000000000004.png", g), "m5")
          && is(P(L"%ls\\hd-pack-ancien\\textures-avant-2025\\0000000000000005.png", g), "avant"));
    CHECK("set_aside_moves_not_deletes: player's own folder and a stray file untouched",
          is(P(L"%ls\\textures\\perso\\0000000000000003.png", g), "mine") && is(P(L"%ls\\textures-avant-fichier", g), "a file, not a folder"));
    CHECK("set_aside: nothing to set aside is fine", hp_set_aside(U(g)) == 0);

    /* run.sh's game: textures-avant-lien is a link to a sentinel, textures/hd holds one */
    wchar_t fg[HP_WMAX]; wcscpy(fg, P(L"%ls\\fx\\sa\\game", T));
    r = hp_set_aside(U(fg));
    CHECK("set_aside: a link folder is left where it is", r != 0 && isreparse(P(L"%ls\\textures-avant-lien", fg))
          && !exists(P(L"%ls\\hd-pack-ancien\\textures-avant-lien", fg)));
    CHECK("set_aside: a real folder holding a link is moved as one rename", isreparse(P(L"%ls\\hd-pack-ancien\\textures-hd\\dedans", fg))
          && !exists(P(L"%ls\\textures\\hd", fg)));
    CHECK("set_aside: the sentinel behind the links is untouched", is(P(L"%ls\\fx\\sa\\sentinel\\s.txt", T), "sentinel\n")
          && count(P(L"%ls\\fx\\sa\\sentinel", T)) == 1);
}

/* stage as hd-pack.exe leaves it before hp_install */
static void make_install_case(const wchar_t* g, const wchar_t* stage) {
    mk(P(L"%ls\\textures\\hd-pack", g)); mk(P(L"%ls\\textures\\hd", g)); mk(P(L"%ls\\movies", g));
    puts_(P(L"%ls\\textures\\hd-pack\\old.png", g), "old pack");
    puts_(P(L"%ls\\textures\\hd-pack\\hd-pack.txt", g), "hd-pack 0\n");
    puts_(P(L"%ls\\textures\\hd\\h.png", g), "real-esrgan");
    puts_(P(L"%ls\\movies\\a.mpg", g), "old a");
    puts_(P(L"%ls\\movies\\other.mpg", g), "other");
    mk(P(L"%ls\\textures", stage)); mk(P(L"%ls\\movies", stage));
    puts_(P(L"%ls\\textures\\0000000000000001.png", stage), "new");
    puts_(P(L"%ls\\movies\\a.1.mpg", stage), "new a part 1");
    puts_(P(L"%ls\\movies\\a.sig", stage), "new a sig");
    puts_(P(L"%ls\\hd-pack.txt", stage), "hd-pack 1\n");
}
static int unchanged(const wchar_t* g, const wchar_t* stage) {
    return is(P(L"%ls\\textures\\hd-pack\\old.png", g), "old pack") && is(P(L"%ls\\textures\\hd-pack\\hd-pack.txt", g), "hd-pack 0\n")
        && count(P(L"%ls\\textures\\hd-pack", g)) == 2 && is(P(L"%ls\\textures\\hd\\h.png", g), "real-esrgan")
        && is(P(L"%ls\\movies\\a.mpg", g), "old a") && is(P(L"%ls\\movies\\other.mpg", g), "other")
        && !exists(P(L"%ls\\movies\\a.1.mpg", g))
        && is(P(L"%ls\\textures\\0000000000000001.png", stage), "new") && is(P(L"%ls\\movies\\a.1.mpg", stage), "new a part 1")
        && is(P(L"%ls\\hd-pack.txt", stage), "hd-pack 1\n");
}

static void t_install(void) {
    wchar_t g[HP_WMAX]; wcscpy(g, P(L"%ls\\in-ok\\game", W)); wchar_t st[HP_WMAX]; wcscpy(st, P(L"%ls\\hd-pack-tmp\\stage", g));
    make_install_case(g, st);
    int r = hp_install(U(g), U(st));
    CHECK("install: returns 0", r == 0);
    CHECK("install: textures in textures/hd-pack, marker last", is(P(L"%ls\\textures\\hd-pack\\0000000000000001.png", g), "new")
          && is(P(L"%ls\\textures\\hd-pack\\hd-pack.txt", g), "hd-pack 1\n") && count(P(L"%ls\\textures\\hd-pack", g)) == 2);
    CHECK("install: the old pack is set aside, not deleted", is(P(L"%ls\\hd-pack-ancien\\hd-pack\\old.png", g), "old pack"));
    CHECK("install: movies in, the old movie of that name kept in movies-avant-hd-pack",
          is(P(L"%ls\\movies\\a.1.mpg", g), "new a part 1") && is(P(L"%ls\\movies\\a.sig", g), "new a sig")
          && !exists(P(L"%ls\\movies\\a.mpg", g)) && is(P(L"%ls\\movies-avant-hd-pack\\a.mpg", g), "old a"));
    CHECK("install: other movies and textures/hd untouched", is(P(L"%ls\\movies\\other.mpg", g), "other") && is(P(L"%ls\\textures\\hd\\h.png", g), "real-esrgan"));
    CHECK("install: stage emptied (folders left for the caller)", count(st) == 1 && count(P(L"%ls\\movies", st)) == 0);

    /* install_fails_cleanly: a stage movie can't go in (a folder holds its name) */
    wcscpy(g, P(L"%ls\\in-fail\\game", W)); wcscpy(st, P(L"%ls\\hd-pack-tmp\\stage", g));
    make_install_case(g, st);
    mk(P(L"%ls\\movies\\a.sig", g));
    r = hp_install(U(g), U(st));
    CHECK("install_fails_cleanly: a movie that can't be moved", r != 0 && unchanged(g, st) && isdir(P(L"%ls\\movies\\a.sig", g)));
    CHECK("install_fails_cleanly: nothing set aside", !exists(P(L"%ls\\hd-pack-ancien", g)) && !exists(P(L"%ls\\movies-avant-hd-pack\\a.mpg", g)));

    /* install_fails_cleanly after textures went in: a movie held open (the game playing it) */
    wcscpy(g, P(L"%ls\\in-busy\\game", W)); wcscpy(st, P(L"%ls\\hd-pack-tmp\\stage", g));
    make_install_case(g, st);
    HANDLE h = CreateFileW(P(L"%ls\\movies\\a.mpg", g), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    int wine_locks = !MoveFileExW(P(L"%ls\\movies\\a.mpg", g), P(L"%ls\\movies\\a-probe.mpg", g), 0);
    if (!wine_locks) MoveFileExW(P(L"%ls\\movies\\a-probe.mpg", g), P(L"%ls\\movies\\a.mpg", g), 0);
    if (wine_locks) {
        r = hp_install(U(g), U(st));
        CloseHandle(h);
        CHECK("install_fails_cleanly: a busy movie, everything rolled back", r != 0 && unchanged(g, st));
        CHECK("install_fails_cleanly: the empty folders made on the way are removed", count(P(L"%ls\\hd-pack-ancien", g)) <= 0);
    } else { CloseHandle(h); skip("install_fails_cleanly: busy movie", "Wine lets an open file without FILE_SHARE_DELETE be renamed"); }

    /* install_fails_cleanly at the very last move (the marker's place is taken):
     * every move undone, old pack and old movie back */
    wcscpy(g, P(L"%ls\\in-late\\game", W)); wcscpy(st, P(L"%ls\\hd-pack-tmp\\stage", g));
    make_install_case(g, st);
    puts_(P(L"%ls\\textures\\hd-pack.txt", st), "in the way");
    r = hp_install(U(g), U(st));
    CHECK("install_fails_cleanly: last move fails, all undone", r == -1 && unchanged(g, st)
          && is(P(L"%ls\\textures\\hd-pack.txt", st), "in the way") && is(P(L"%ls\\movies\\a.sig", st), "new a sig")
          && !exists(P(L"%ls\\hd-pack-ancien", g)) && !exists(P(L"%ls\\movies-avant-hd-pack", g)));

    /* install_keeps_textures: a stage without textures (the pack's textures
     * zips were already installed): textures/hd-pack stays, only the movies
     * and the marker change; the old marker goes to hd-pack-ancien */
    wcscpy(g, P(L"%ls\\in-keep\\game", W)); wcscpy(st, P(L"%ls\\hd-pack-tmp\\stage", g));
    make_install_case(g, st);
    DeleteFileW(P(L"%ls\\textures\\0000000000000001.png", st)); RemoveDirectoryW(P(L"%ls\\textures", st));
    r = hp_install(U(g), U(st));
    CHECK("install_keeps_textures: returns 0", r == 0);
    CHECK("install_keeps_textures: textures/hd-pack kept, new marker", is(P(L"%ls\\textures\\hd-pack\\old.png", g), "old pack")
          && is(P(L"%ls\\textures\\hd-pack\\hd-pack.txt", g), "hd-pack 1\n") && count(P(L"%ls\\textures\\hd-pack", g)) == 2
          && !exists(P(L"%ls\\hd-pack-ancien\\hd-pack", g)));
    CHECK("install_keeps_textures: the old marker set aside, not deleted", is(P(L"%ls\\hd-pack-ancien\\hd-pack.txt", g), "hd-pack 0\n"));
    CHECK("install_keeps_textures: movies in, the old one kept aside", is(P(L"%ls\\movies\\a.1.mpg", g), "new a part 1")
          && is(P(L"%ls\\movies-avant-hd-pack\\a.mpg", g), "old a") && is(P(L"%ls\\movies\\other.mpg", g), "other"));

    /* install_keeps_textures fails cleanly after the movies went in (hd-pack-ancien
     * is a file: the old marker can't be set aside): every move undone */
    wcscpy(g, P(L"%ls\\in-keep-fail\\game", W)); wcscpy(st, P(L"%ls\\hd-pack-tmp\\stage", g));
    make_install_case(g, st);
    DeleteFileW(P(L"%ls\\textures\\0000000000000001.png", st)); RemoveDirectoryW(P(L"%ls\\textures", st));
    puts_(P(L"%ls\\hd-pack-ancien", g), "a file");
    r = hp_install(U(g), U(st));
    CHECK("install_keeps_textures: a late failure undoes all, old marker in place", r == -1
          && is(P(L"%ls\\textures\\hd-pack\\hd-pack.txt", g), "hd-pack 0\n") && is(P(L"%ls\\textures\\hd-pack\\old.png", g), "old pack")
          && is(P(L"%ls\\movies\\a.mpg", g), "old a") && !exists(P(L"%ls\\movies\\a.1.mpg", g))
          && is(P(L"%ls\\movies\\a.1.mpg", st), "new a part 1") && is(P(L"%ls\\hd-pack.txt", st), "hd-pack 1\n")
          && is(P(L"%ls\\hd-pack-ancien", g), "a file") && !exists(P(L"%ls\\movies-avant-hd-pack", g)));

    /* no stage textures and no pack installed to keep: refused */
    wcscpy(g, P(L"%ls\\in-keep-none\\game", W)); wcscpy(st, P(L"%ls\\hd-pack-tmp\\stage", g));
    mk(P(L"%ls\\movies", g)); mk(P(L"%ls\\movies", st));
    puts_(P(L"%ls\\movies\\a.mpg", st), "new a"); puts_(P(L"%ls\\hd-pack.txt", st), "hd-pack 1\n");
    r = hp_install(U(g), U(st));
    CHECK("install: no stage textures and nothing installed is refused", r != 0 && is(P(L"%ls\\movies\\a.mpg", st), "new a")
          && !exists(P(L"%ls\\textures\\hd-pack", g)));

    /* something unexpected in the stage is refused */
    wcscpy(g, P(L"%ls\\in-odd\\game", W)); wcscpy(st, P(L"%ls\\hd-pack-tmp\\stage", g));
    make_install_case(g, st);
    puts_(P(L"%ls\\stray.txt", st), "stray");
    r = hp_install(U(g), U(st));
    CHECK("install: a stray stage entry is refused", r != 0 && unchanged(g, st));

    /* game/movies is a link to a sentinel folder (made by run.sh) */
    wchar_t fg[HP_WMAX]; wcscpy(fg, P(L"%ls\\fx\\ml\\game", T)); wcscpy(st, P(L"%ls\\stage", W));
    mk(P(L"%ls\\textures", st)); mk(P(L"%ls\\movies", st));
    puts_(P(L"%ls\\textures\\0000000000000001.png", st), "new"); puts_(P(L"%ls\\movies\\a.mpg", st), "new a");
    puts_(P(L"%ls\\hd-pack.txt", st), "hd-pack 1\n");
    r = hp_install(U(fg), U(st));
    CHECK("install: stage outside the game folder is refused", r != 0 && is(P(L"%ls\\movies\\a.mpg", st), "new a"));
    wchar_t st2[HP_WMAX]; wcscpy(st2, P(L"%ls\\hd-pack-tmp\\stage", fg));
    mk(P(L"%ls\\textures", st2)); mk(P(L"%ls\\movies", st2));
    puts_(P(L"%ls\\textures\\0000000000000001.png", st2), "new"); puts_(P(L"%ls\\movies\\a.mpg", st2), "new a");
    puts_(P(L"%ls\\hd-pack.txt", st2), "hd-pack 1\n");
    r = hp_install(U(fg), U(st2));
    CHECK("install: movies folder that is a link is refused, sentinel untouched", r != 0
          && is(P(L"%ls\\fx\\ml\\sentinel\\a.mpg", T), "sentinel\n") && count(P(L"%ls\\fx\\ml\\sentinel", T)) == 1
          && !exists(P(L"%ls\\textures\\hd-pack", fg)) && is(P(L"%ls\\movies\\a.mpg", st2), "new a"));

    /* textures/hd-pack is a link to a sentinel folder (made by run.sh) */
    wcscpy(fg, P(L"%ls\\fx\\tl\\game", T)); wcscpy(st2, P(L"%ls\\hd-pack-tmp\\stage", fg));
    mk(P(L"%ls\\textures", st2)); puts_(P(L"%ls\\textures\\0000000000000001.png", st2), "new");
    puts_(P(L"%ls\\hd-pack.txt", st2), "hd-pack 1\n");
    r = hp_install(U(fg), U(st2));
    CHECK("install: textures/hd-pack that is a link is refused, sentinel untouched", r != 0
          && is(P(L"%ls\\fx\\tl\\sentinel\\old.png", T), "sentinel\n") && count(P(L"%ls\\fx\\tl\\sentinel", T)) == 1
          && isreparse(P(L"%ls\\textures\\hd-pack", fg)) && !exists(P(L"%ls\\hd-pack-ancien", fg)));
}

static void t_exe(void) {
    wchar_t pack[HP_WMAX]; wcscpy(pack, P(L"%ls\\pack", T));
    /* a full run, then installed_is_noop */
    wchar_t g[HP_WMAX]; wcscpy(g, P(L"%ls\\exe\\game", W)); new_game(g);
    mk(P(L"%ls\\textures\\hd", g)); mk(P(L"%ls\\textures-hd-outil", g));
    puts_(P(L"%ls\\textures\\hd\\0000000000000009.png", g), "real-esrgan");
    DWORD rc = run_exe(P(L"%ls\\hd-pack.exe", g), pack);
    CHECK("exe: full run exits 0", rc == 0);
    CHECK("exe: pack installed, marker = manifest", count(P(L"%ls\\textures\\hd-pack", g)) == 6
          && fill(P(L"%ls\\textures\\hd-pack\\000000000000abc4.png", g), 5, 600) && fill(P(L"%ls\\movies\\a.1.mpg", g), 'M', 300));
    { char a[4096], b[4096]; CHECK("exe: marker is the manifest", get(P(L"%ls\\textures\\hd-pack\\hd-pack.txt", g), a, sizeof a) > 0
          && get(P(L"%ls\\hd-pack.txt", pack), b, sizeof b) > 0 && !strcmp(a, b)); }
    CHECK("exe: Real-ESRGAN textures set aside after install", is(P(L"%ls\\hd-pack-ancien\\textures-hd\\0000000000000009.png", g), "real-esrgan"));
    { HANDLE l2 = CreateFileW(P(L"%ls\\hd-pack.lock", g), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    CHECK("exe: progress 'done 0 100', work folder gone, lock let go", is(P(L"%ls\\hd-pack-progress.txt", g), "done 0 100")
          && !exists(P(L"%ls\\hd-pack-tmp", g)) && l2 != INVALID_HANDLE_VALUE);
    if (l2 != INVALID_HANDLE_VALUE) CloseHandle(l2); }

    DeleteFileW(P(L"%ls\\hd-pack-progress.txt", g));
    char log1[65536]; int l1 = get(P(L"%ls\\hd-pack.log", g), log1, sizeof log1);
    WIN32_FILE_ATTRIBUTE_DATA m1, m2;
    GetFileAttributesExW(P(L"%ls\\textures\\hd-pack\\hd-pack.txt", g), GetFileExInfoStandard, &m1);
    rc = run_exe(P(L"%ls\\hd-pack.exe", g), pack);
    GetFileAttributesExW(P(L"%ls\\textures\\hd-pack\\hd-pack.txt", g), GetFileExInfoStandard, &m2);
    char log2[65536]; int l2 = get(P(L"%ls\\hd-pack.log", g), log2, sizeof log2);
    CHECK("installed_is_noop: exit 0, no progress file, nothing touched", rc == 0 && !exists(P(L"%ls\\hd-pack-progress.txt", g))
          && !exists(P(L"%ls\\hd-pack-tmp", g)) && CompareFileTime(&m1.ftLastWriteTime, &m2.ftLastWriteTime) == 0
          && l1 == l2 && count(P(L"%ls\\textures\\hd-pack", g)) == 6 && count(P(L"%ls\\hd-pack-ancien", g)) == 1);

    /* second_instance_exits: the lock is held */
    wcscpy(g, P(L"%ls\\exe-locked\\game", W)); new_game(g);
    HANDLE lk = CreateFileW(P(L"%ls\\hd-pack.lock", g), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD t0 = GetTickCount();
    rc = run_exe(P(L"%ls\\hd-pack.exe", g), pack);
    DWORD dt = GetTickCount() - t0;
    CloseHandle(lk);
    { char lg[4096];
    CHECK("second_instance_exits: exits 0 at once, does nothing", lk != INVALID_HANDLE_VALUE && rc == 0 && dt < 20000
          && !exists(P(L"%ls\\textures\\hd-pack", g)) && !exists(P(L"%ls\\hd-pack-progress.txt", g)) && !exists(P(L"%ls\\hd-pack-tmp", g))
          && get(P(L"%ls\\hd-pack.log", g), lg, sizeof lg) > 0 && strstr(lg, "another hd-pack.exe is at work")); }

    /* a zip that does not match its SHA-256 */
    wcscpy(g, P(L"%ls\\exe-bad\\game", W)); new_game(g);
    mk(P(L"%ls\\textures\\hd", g)); mk(P(L"%ls\\textures-hd-outil", g)); mk(P(L"%ls\\movies", g));
    puts_(P(L"%ls\\textures\\hd\\0000000000000009.png", g), "real-esrgan");
    puts_(P(L"%ls\\movies\\a.mpg", g), "old a");
    rc = run_exe(P(L"%ls\\hd-pack.exe", g), P(L"%ls\\pack-bad", T));
    CHECK("bad zip: fails, nothing installed, nothing set aside, no bar", rc != 0 && !exists(P(L"%ls\\textures\\hd-pack", g))
          && is(P(L"%ls\\textures\\hd\\0000000000000009.png", g), "real-esrgan") && is(P(L"%ls\\movies\\a.mpg", g), "old a")
          && !exists(P(L"%ls\\hd-pack-ancien", g)) && !exists(P(L"%ls\\hd-pack-progress.txt", g)));
    CHECK("bad zip: the bad zip is not kept, the stage is cleaned", !exists(P(L"%ls\\hd-pack-tmp\\hd-pack-movies.zip", g))
          && !exists(P(L"%ls\\hd-pack-tmp\\stage", g)));
    CHECK("bad zip: then the good pack installs (verified zips reused)", run_exe(P(L"%ls\\hd-pack.exe", g), pack) == 0
          && count(P(L"%ls\\textures\\hd-pack", g)) == 6 && !exists(P(L"%ls\\hd-pack-tmp", g)));

    /* hp_install fails inside hd-pack.exe (the game plays the old movie): nothing
     * installed, Real-ESRGAN kept, no bar, the unpacked files gone */
    wcscpy(g, P(L"%ls\\exe-busy\\game", W)); new_game(g);
    mk(P(L"%ls\\textures\\hd", g)); mk(P(L"%ls\\textures-hd-outil", g)); mk(P(L"%ls\\movies", g));
    puts_(P(L"%ls\\textures\\hd\\0000000000000009.png", g), "real-esrgan");
    puts_(P(L"%ls\\movies\\a.mpg", g), "old a");
    HANDLE mv = CreateFileW(P(L"%ls\\movies\\a.mpg", g), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    rc = run_exe(P(L"%ls\\hd-pack.exe", g), pack);
    CloseHandle(mv);
    CHECK("exe install fails: nothing installed, Real-ESRGAN kept, no bar", rc != 0 && !exists(P(L"%ls\\textures\\hd-pack", g))
          && is(P(L"%ls\\textures\\hd\\0000000000000009.png", g), "real-esrgan") && is(P(L"%ls\\movies\\a.mpg", g), "old a")
          && !exists(P(L"%ls\\movies\\a.1.mpg", g)) && !exists(P(L"%ls\\hd-pack-progress.txt", g))
          && !exists(P(L"%ls\\hd-pack-tmp\\stage", g)) && count(P(L"%ls\\hd-pack-ancien", g)) <= 0);

    /* installed, but a killed run left the bar at "working" and textures/hd in place */
    wcscpy(g, P(L"%ls\\exe\\game", W));
    puts_(P(L"%ls\\hd-pack-progress.txt", g), "working 95 100");
    mk(P(L"%ls\\textures\\hd", g)); puts_(P(L"%ls\\textures\\hd\\000000000000000a.png", g), "again");
    rc = run_exe(P(L"%ls\\hd-pack.exe", g), pack);
    CHECK("installed + unfinished run: the stale bar is gone, set-aside done now", rc == 0 && !exists(P(L"%ls\\hd-pack-progress.txt", g))
          && is(P(L"%ls\\hd-pack-ancien\\textures-hd-2\\000000000000000a.png", g), "again") && !exists(P(L"%ls\\textures\\hd", g)));

    /* hd-pack.lock is a link to a sentinel file (made by run.sh): refused, sentinel
     * kept (run.sh checks the link is still there: Wine shows a link to a file
     * as a plain file) */
    wcscpy(g, P(L"%ls\\fx\\lk\\game", T)); new_game(g);
    rc = run_exe(P(L"%ls\\hd-pack.exe", g), pack);
    CHECK("lock that is a link: nothing done, the file behind it kept", rc != 0
          && is(P(L"%ls\\fx\\lk\\sentinel\\lock.txt", T), "sentinel\n") && !exists(P(L"%ls\\textures\\hd-pack", g)));

    /* hd-pack-progress.txt hard-linked to a sentinel (made by run.sh): never written through */
    wcscpy(g, P(L"%ls\\fx\\hl\\game", T)); new_game(g);
    rc = run_exe(P(L"%ls\\hd-pack.exe", g), pack);
    CHECK("hard-linked progress file: the other file is not written", rc == 0 && is(P(L"%ls\\fx\\hl\\sentinel\\hl.txt", T), "sentinel\n")
          && count(P(L"%ls\\textures\\hd-pack", g)) == 6);

    /* no source: nothing to do yet (the download is Task 5) */
    wcscpy(g, P(L"%ls\\exe-nosrc\\game", W)); new_game(g);
    rc = run_exe(P(L"%ls\\hd-pack.exe", g), NULL);
    CHECK("no source: exits 0, no bar", rc == 0 && !exists(P(L"%ls\\hd-pack-progress.txt", g)) && !exists(P(L"%ls\\textures\\hd-pack", g)));

    /* a run killed while it unpacked left a stage: set aside, not used, not deleted */
    wcscpy(g, P(L"%ls\\exe-stale\\game", W)); new_game(g);
    mk(P(L"%ls\\hd-pack-tmp\\stage\\textures", g));
    puts_(P(L"%ls\\hd-pack-tmp\\stage\\textures\\00000000deadbeef.png", g), "stale");
    rc = run_exe(P(L"%ls\\hd-pack.exe", g), pack);
    CHECK("stale stage: set aside, not installed", rc == 0 && !exists(P(L"%ls\\textures\\hd-pack\\00000000deadbeef.png", g))
          && is(P(L"%ls\\hd-pack-ancien\\stage-interrompu\\textures\\00000000deadbeef.png", g), "stale")
          && count(P(L"%ls\\textures\\hd-pack", g)) == 6);
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: t_hdpack.exe <temp folder> <hd-pack.exe>\n"); return 2; }
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, T, HP_WMAX);
    MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, EXE, HP_WMAX);
    _snwprintf(W, HP_WMAX, L"%ls\\w", T);
    if (!isdir(W)) { printf("no %s (run.sh makes it)\n", U(W)); return 2; }
    t_manifest();
    t_sha();
    t_unzip();
    t_safe_path();
    t_rule();
    t_set_aside();
    t_install();
    t_exe();
    printf("t_hdpack: %d passed, %d failed, %d skipped\n", g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
