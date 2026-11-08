#!/usr/bin/env python3
"""
tools/xbox_package.py -- the Xbox's two apps, "Pokemon Black" and "Pokemon
White", from one build of the Xbox program (host/xbox/, docs/xbox.md).

    python tools/xbox_package.py --build-dir build-xbox --sdl2-dll SDL2.dll
        --angle-dir angle/x64 --version 0.2.4 --out dist
        [--pfx cert.pfx --pfx-password PW] [--publisher "CN=eidos"]

Each app is the same program with its own package identity, name, tiles
and edition.txt (which tells the program which game's ROM it takes,
host/xbox/xbox_host.c), so the two install side by side, each with its own
folder for its ROM and saves.  makeappx and signtool come from the Windows
SDK (a Visual Studio prompt has them on its PATH).  Standard library only.
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "host", "xbox", "assets")

EDITIONS = {
    # edition: (package name, display name, tile colour)
    "Black": ("eidos.PokemonBlack", "Pokemon Black", "#111114"),
    "White": ("eidos.PokemonWhite", "Pokemon White", "#F0F0F2"),
}

TILES = ("Square44x44Logo", "Square150x150Logo", "StoreLogo", "Wide310x150Logo",
         "SplashScreen", "LargeTile")

MANIFEST = """<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
         xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
         IgnorableNamespaces="uap">
  <Identity Name="{name}" Publisher="{publisher}" Version="{version}"
            ProcessorArchitecture="x64" />
  <Properties>
    <DisplayName>{display}</DisplayName>
    <PublisherDisplayName>eidos</PublisherDisplayName>
    <Logo>Assets\\StoreLogo.png</Logo>
  </Properties>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Universal" MinVersion="10.0.17763.0"
                        MaxVersionTested="10.0.22621.0" />
    <PackageDependency Name="Microsoft.VCLibs.140.00" MinVersion="14.0.30035.0"
        Publisher="CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US" />
  </Dependencies>
  <Resources>
    <Resource Language="en-us" />
  </Resources>
  <Applications>
    <Application Id="App" Executable="eidos.exe" EntryPoint="eidos.App">
      <uap:VisualElements DisplayName="{display}"
                          Description="{display}, played in one screen (eidos)"
                          BackgroundColor="{colour}"
                          Square150x150Logo="Assets\\Square150x150Logo.png"
                          Square44x44Logo="Assets\\Square44x44Logo.png">
        <uap:DefaultTile Wide310x150Logo="Assets\\Wide310x150Logo.png"
                         Square310x310Logo="Assets\\LargeTile.png"
                         ShortName="{display}" />
        <uap:SplashScreen Image="Assets\\SplashScreen.png" BackgroundColor="{colour}" />
      </uap:VisualElements>
    </Application>
  </Applications>
  <Capabilities>
    <Capability Name="internetClient" />
    <Capability Name="internetClientServer" />
    <Capability Name="privateNetworkClientServer" />
  </Capabilities>
</Package>
"""


def tool(name):
    found = shutil.which(name)
    if found:
        return found
    # the newest Windows SDK's own copy
    for path in sorted(glob.glob(r"C:\Program Files (x86)\Windows Kits\10\bin\10.*\x64\%s.exe" % name),
                       reverse=True):
        return path
    sys.exit("xbox_package: no %s (run from a Visual Studio prompt)" % name)


def run(args):
    print("+ " + " ".join('"%s"' % a if " " in a else a for a in args), flush=True)
    subprocess.check_call(args)


def appx_version(v):
    parts = [p for p in v.split("-")[0].split(".") if p.isdigit()]
    parts = (parts + ["0", "0", "0", "0"])[:4]
    return ".".join(parts)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build-dir", required=True, help="host/xbox's build (eidos.exe, eidos_sdl2.dll)")
    ap.add_argument("--sdl2-dll", required=True, help="SDL2.dll built for UWP")
    ap.add_argument("--angle-dir", required=True, help="ANGLE for UWP, x64 (libEGL.dll, libGLESv2.dll)")
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--publisher", default="CN=eidos",
                    help="the packages' publisher: the signing certificate's subject")
    ap.add_argument("--pfx", help="sign the packages with this certificate")
    ap.add_argument("--pfx-password", default="")
    ap.add_argument("--only", choices=sorted(EDITIONS), help="one app only")
    a = ap.parse_args()

    binaries = [os.path.join(a.build_dir, "eidos.exe"),
                os.path.join(a.build_dir, "eidos_sdl2.dll"),
                a.sdl2_dll,
                os.path.join(a.angle_dir, "libEGL.dll"),
                os.path.join(a.angle_dir, "libGLESv2.dll")]
    for b in binaries:
        if not os.path.isfile(b):
            sys.exit("xbox_package: no %s" % b)
    makeappx = tool("makeappx")
    signtool = tool("signtool") if a.pfx else None
    version = appx_version(a.version)
    os.makedirs(a.out, exist_ok=True)

    for ed, (name, display, colour) in sorted(EDITIONS.items()):
        if a.only and ed != a.only:
            continue
        layout = os.path.join(a.out, "layout-" + ed.lower())
        shutil.rmtree(layout, ignore_errors=True)
        os.makedirs(os.path.join(layout, "Assets"))
        for b in binaries:
            shutil.copy2(b, layout)
        for t in TILES:
            shutil.copy2(os.path.join(ASSETS, "%s-%s.png" % (ed.lower(), t)),
                         os.path.join(layout, "Assets", t + ".png"))
        with open(os.path.join(layout, "edition.txt"), "w", newline="\n") as f:
            f.write(ed + "\n")
        with open(os.path.join(layout, "AppxManifest.xml"), "w", encoding="utf-8") as f:
            f.write(MANIFEST.format(name=name, publisher=a.publisher, version=version,
                                    display=display, colour=colour))
        appx = os.path.join(a.out, "%s_%s_x64.appx" % (display.replace(" ", ""), version))
        run([makeappx, "pack", "/o", "/d", layout, "/p", appx])
        if signtool:
            run([signtool, "sign", "/fd", "SHA256", "/a", "/f", a.pfx, "/p", a.pfx_password, appx])
        shutil.rmtree(layout)
        print("xbox_package: %s" % appx)
    return 0


if __name__ == "__main__":
    sys.exit(main())
