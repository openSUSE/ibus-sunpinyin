ibus-sunpinyin
==============

ibus-sunpinyin is a wrapper around SunPinyin which enables users to use
SunPinyin with the IBus framework.

Building and installing
-----------------------

Build requirements: Meson >= 0.59, Ninja, a C++17 compiler, pkg-config,
GNU gettext (msgfmt, xgettext and msgmerge), Python 3, IBus >= 1.5.26 development
files (ibus-1.0), SunPinyin >= 2.0 development files (sunpinyin-2.0),
OpenCC >= 1.0 (opencc), libsoup >= 3.0 (libsoup-3.0),
and JSON-GLib (json-glib-1.0) development files.
The preferences tool also requires Python GObject introspection bindings,
GTK 3 and IBus introspection data at runtime.

    meson setup build --prefix=/usr
    meson compile -C build
    meson install -C build
