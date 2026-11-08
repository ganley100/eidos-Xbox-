/*
 * host/xbox/xbox_host.c -- the Xbox's own part of the host (docs/xbox.md):
 * the program's entry, the ROM "dialog" and the folders.
 *
 * The Xbox build is a UWP app (Developer Mode).  One program is packaged
 * twice, as "Pokemon Black" and as "Pokemon White"; the package's
 * edition.txt, beside the program, says which one this is, and that app
 * takes only that game's ROM.  Each app has its own folder (its package's
 * LocalState), which holds the ROM the player uploads, the extracted game
 * data and the saves:
 *
 *   LocalState\<anything>.nds     the ROM (the Device Portal's file explorer)
 *   LocalState\extract\           the game data, from the first run
 *   LocalState\profile\           settings and saves
 *   LocalState\stderr.txt         the program's messages
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fileapifromapp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "host.h"
#include "picker.h"
#include "setup.h"
#include "plat_save.h"
#include "sw2.h"

int host_xbox_main(int argc, char **argv);

/* ------------------------------------------------------------------ */
/* The entry: SDL2's UWP application runs the program's main           */
/* ------------------------------------------------------------------ */

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)inst;
    (void)prev;
    (void)cmd;
    (void)show;
    return sw2_run_app(host_xbox_main);
}

/* ------------------------------------------------------------------ */
/* Folders                                                             */
/* ------------------------------------------------------------------ */

static int narrow(const wchar_t *w, char *out, size_t n)
{
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL) > 0 ? 0 : -1;
}

/* the app's own folder (LocalState), with a trailing backslash */
static int app_dir(char *out, size_t n)
{
    char *pref = SDL_GetPrefPath("eidos", "eidos");

    if (pref == NULL) {
        return -1;
    }
    snprintf(out, n, "%s", pref);
    free(pref);
    return 0;
}

int bw_os_app_home(char *out, size_t n)
{
    char dir[1024];
    size_t len;

    if (app_dir(dir, sizeof dir) != 0) {
        return -1;
    }
    len = strlen(dir);
    if (len > 0 && (dir[len - 1] == '\\' || dir[len - 1] == '/')) {
        dir[len - 1] = '\0';
    }
    return snprintf(out, n, "%s", dir) < (int)n ? 0 : -1;
}

int bw_os_exe_dir(char *out, size_t n)
{
    wchar_t w[2048];
    DWORD len = GetModuleFileNameW(NULL, w, 2048);
    char *slash;

    if (len == 0 || len >= 2048 || narrow(w, out, n) != 0) {
        return -1;
    }
    slash = strrchr(out, '\\');
    if (slash == NULL) {
        return -1;
    }
    *slash = '\0';
    return 0;
}

int bw_plat_default_save_dir(char *out, size_t n)
{
    char dir[1024];

    if (app_dir(dir, sizeof dir) != 0) {
        return -1;
    }
    return snprintf(out, n, "%sprofile\\saves", dir) < (int)n ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Which game this app is                                              */
/* ------------------------------------------------------------------ */

/* "Black", "White", or "" for an app that takes either (edition.txt
 * missing) */
const char *host_xbox_edition(void)
{
    static char ed[16];
    static int read;

    if (!read) {
        char path[1100];
        FILE *f;

        read = 1;
        if (bw_os_exe_dir(path, sizeof path - 16) == 0) {
            strcat(path, "\\edition.txt");
            if ((f = fopen(path, "r")) != NULL) {
                if (fgets(ed, sizeof ed, f) == NULL) {
                    ed[0] = '\0';
                }
                ed[strcspn(ed, "\r\n ")] = '\0';
                fclose(f);
            }
        }
    }
    return ed;
}

/* The ROM's game code (its header's bytes 0x0C to 0x0F): IRBx is Pokemon
 * Black, IRAx Pokemon White, x the region.  'B', 'W', or 0 for anything
 * else.  The "FromApp" calls reach outside the app's own folder
 * (DevelopmentFiles), as the C library's do not. */
static char rom_edition(const wchar_t *path)
{
    HANDLE f = CreateFile2FromAppW(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NULL);
    unsigned char hdr[16];
    DWORD got = 0;
    BOOL ok;

    if (f == INVALID_HANDLE_VALUE) {
        return 0;
    }
    ok = ReadFile(f, hdr, sizeof hdr, &got, NULL);
    CloseHandle(f);
    if (!ok || got != sizeof hdr || hdr[12] != 'I' || hdr[13] != 'R') {
        return 0;
    }
    return hdr[14] == 'B' ? 'B' : hdr[14] == 'A' ? 'W' : 0;
}

/* ------------------------------------------------------------------ */
/* The ROM "dialog" (install/picker.h's bw_pick_rom)                   */
/* ------------------------------------------------------------------ */

/* The folders searched for the ROM, in order: the Xbox's DevelopmentFiles
 * folder (the Device Portal's file explorer and its network share reach
 * it; the package's broadFileSystemAccess lets the app read it), then the
 * app's own folder.  The first .nds file of this app's game is the answer. */
#define XBOX_DEV_DIR L"D:\\DevelopmentFiles\\"
#define XBOX_DEV_DIR_POKEMON L"D:\\DevelopmentFiles\\Pokemon\\"

/* the first .nds of game `want` (0: any) in `wdir` (with its trailing
 * backslash) into `full`; 1 found, 0 none, and *other set when a ROM of
 * the other game is there */
static int find_rom(const wchar_t *wdir, char want, wchar_t *full, size_t n, int *other)
{
    wchar_t pattern[1100];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    _snwprintf(pattern, 1100, L"%s*.nds", wdir);
    h = FindFirstFileExFromAppW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        char got;

        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        _snwprintf(full, n, L"%s%s", wdir, fd.cFileName);
        got = rom_edition(full);
        if (want != 0 && got != want) {
            *other = 1;
            continue;
        }
        FindClose(h);
        return 1;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

int host_pick_rom(const char *title, char *out, size_t n)
{
    char dir[1024];
    wchar_t wdir[1024], full[1600], local[1100];
    const char *ed = host_xbox_edition();
    char want = ed[0] == 'B' ? 'B' : ed[0] == 'W' ? 'W' : 0;
    int other = 0;

    (void)title;
    if (n > 0) {
        out[0] = '\0';
    }
    if (app_dir(dir, sizeof dir) != 0 ||
        MultiByteToWideChar(CP_UTF8, 0, dir, -1, wdir, 1024) == 0) {
        snprintf(out, n, "This app's folder cannot be found.");
        return BW_PICK_UNAVAILABLE;
    }
    if (find_rom(XBOX_DEV_DIR_POKEMON, want, full, 1600, &other) ||
        find_rom(XBOX_DEV_DIR, want, full, 1600, &other)) {
        /* The game reads the ROM with the C library, which cannot open a
         * file outside the app's folder: a copy goes there, once (the
         * first run extracts the game data from it and is done with it). */
        WIN32_FILE_ATTRIBUTE_DATA a, b;

        _snwprintf(local, 1100, L"%srom-%hs.nds", wdir, want == 'W' ? "white" : "black");
        if (!GetFileAttributesExFromAppW(local, GetFileExInfoStandard, &b) ||
            !GetFileAttributesExFromAppW(full, GetFileExInfoStandard, &a) ||
            a.nFileSizeLow != b.nFileSizeLow || a.nFileSizeHigh != b.nFileSizeHigh) {
            fprintf(stderr, "eidos: copying the ROM from DevelopmentFiles\n");
            if (!CopyFileFromAppW(full, local, FALSE)) {
                snprintf(out, n, "The ROM in DevelopmentFiles cannot be copied to %s "
                         "(error %lu).", dir, (unsigned long)GetLastError());
                return BW_PICK_UNAVAILABLE;
            }
        }
        return narrow(local, out, n) == 0 ? BW_PICK_OK : BW_PICK_UNAVAILABLE;
    }
    if (find_rom(wdir, want, full, 1600, &other)) {
        return narrow(full, out, n) == 0 ? BW_PICK_OK : BW_PICK_UNAVAILABLE;
    }
    if (other) {
        snprintf(out, n, "This is the Pokemon %s app, and the ROM found is not "
                 "Pokemon %s. Put a Pokemon %s ROM (.nds) in DevelopmentFiles\\Pokemon.",
                 ed, ed, ed);
    } else {
        snprintf(out, n, "No .nds file in DevelopmentFiles\\Pokemon, DevelopmentFiles or %s", dir);
    }
    return BW_PICK_UNAVAILABLE;
}
