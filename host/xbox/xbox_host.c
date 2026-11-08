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
 * else. */
static char rom_edition(const wchar_t *path)
{
    FILE *f = _wfopen(path, L"rb");
    unsigned char hdr[16];
    size_t got;

    if (f == NULL) {
        return 0;
    }
    got = fread(hdr, 1, sizeof hdr, f);
    fclose(f);
    if (got != sizeof hdr || hdr[12] != 'I' || hdr[13] != 'R') {
        return 0;
    }
    return hdr[14] == 'B' ? 'B' : hdr[14] == 'A' ? 'W' : 0;
}

/* ------------------------------------------------------------------ */
/* The ROM "dialog" (install/picker.h's bw_pick_rom)                   */
/* ------------------------------------------------------------------ */

/* No file list an app can open on the Xbox: the ROM is a file the player
 * uploads to the app's folder.  The first .nds file there of this app's
 * game is the answer. */
int host_pick_rom(const char *title, char *out, size_t n)
{
    char dir[1024];
    wchar_t wdir[1024], pattern[1100], full[1600];
    WIN32_FIND_DATAW fd;
    HANDLE h;
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
    _snwprintf(pattern, 1100, L"%s*.nds", wdir);
    h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char got;

            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                continue;
            }
            _snwprintf(full, 1600, L"%s%s", wdir, fd.cFileName);
            got = rom_edition(full);
            if (want != 0 && got != want) {
                other = 1;
                continue;
            }
            if (narrow(full, out, n) == 0) {
                FindClose(h);
                return BW_PICK_OK;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (other) {
        snprintf(out, n, "This is the Pokemon %s app, and the ROM in its folder is not "
                 "Pokemon %s. Upload a Pokemon %s ROM (.nds) to %s", ed, ed, ed, dir);
    } else {
        snprintf(out, n, "No .nds file in %s", dir);
    }
    return BW_PICK_UNAVAILABLE;
}
