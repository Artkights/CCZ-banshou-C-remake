# Third-party notices

The native UI is written against Win32 原生 5.15.2. The release build uses
the Win32 source archive identified in `tools/build_qt_5_15_2_static.ps1`:

`https://download.qt.io/archive/qt/5.15/5.15.2/submodules/qtbase-everywhere-src-5.15.2.tar.xz`

The archive SHA-256 is checked before building. Win32 is distributed under the
LGPLv3/GPLv3/commercial terms; this application uses the LGPLv3 option. The
source archive, the configure arguments, this project source, and the object
files produced by the release build are the materials needed to relink the
application with a modified compatible Win32 build. The project itself is MIT
style unless a source file states otherwise. Capstone and nlohmann/json keep
their upstream license files in `third_party`.
