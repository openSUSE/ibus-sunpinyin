ibus-sunpinyin
==============

ibus-sunpinyin is a wrapper around SunPinyin which enables users to use
SunPinyin with the IBus framework.

Building and installing
-----------------------

Build requirements: Meson >= 0.59, Ninja, a C++17 compiler, pkg-config,
GNU gettext (msgfmt, xgettext and msgmerge), Python 3, IBus >= 1.2 development
files (ibus-1.0), SunPinyin >= 2.0 development files (sunpinyin-2.0),
and OpenCC >= 1.0 development files (opencc).
The preferences tool also requires Python GObject introspection bindings,
GTK 3 and IBus introspection data at runtime.

    meson setup build --prefix=/usr
    meson compile -C build
    meson install -C build

For an existing build directory created with C++11, update the cached setting:

    meson configure build -Dcpp_std=c++17
    meson compile -C build

The engine and preferences launcher are installed to
LIBEXECDIR/ibus-sunpinyin (by default /usr/libexec/ibus-sunpinyin with the
prefix above). Resources go to DATADIR/ibus-sunpinyin, the IBus component
to DATADIR/ibus/component, and translations to LOCALEDIR. Use Meson's
--libexecdir, --datadir and --localedir options to change these paths.
For example, --libexecdir=lib preserves the former SCons layout.

For a staged packaging install:

    DESTDIR=/tmp/ibus-sunpinyin-package meson install -C build

DESTDIR is only used when installing; generated runtime paths always use
the configured prefix and installation directories.

Updating translations
---------------------

Languages are listed in po/LINGUAS and translatable sources in po/POTFILES.

    meson compile -C build ibus-sunpinyin-pot
    meson compile -C build ibus-sunpinyin-update-po

Traditional Chinese output
--------------------------

In Preferences > General, enable Traditional Chinese under Output Script.
Candidates, selected preedit text and committed text are converted with OpenCC's
s2t.json profile. The IBus menu also provides a Simplified/Traditional Chinese
toggle, sharing the same persistent setting with Preferences. Simplified Chinese
remains the default. The OpenCC runtime
configuration and dictionaries must be installed. If OpenCC initialization or
conversion fails, the engine logs a warning and falls back to the original text.
SunPinyin continues to select and learn the original simplified entries.

Run the conversion regression test after building:

    meson test -C build opencc-conversion --print-errorlogs
