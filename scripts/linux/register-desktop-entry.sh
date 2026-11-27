#!/bin/sh
set -eu

target_dir=${1:?installation directory missing}
action=${2:-install}
desktop_file=/usr/share/applications/thermvane.desktop
icon_file=/usr/share/icons/hicolor/512x512/apps/thermvane.png
icon_source="$target_dir/share/icons/hicolor/512x512/apps/thermvane.png"

refresh_desktop_database()
{
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
    fi
}

refresh_icon_cache()
{
    if command -v gtk-update-icon-cache >/dev/null 2>&1 \
        && [ -d /usr/share/icons/hicolor ]; then
        gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
    fi
}

if [ "$action" = "uninstall" ]; then
    if [ -f "$desktop_file" ] \
        && grep -Fqx "Exec=$target_dir/bin/ThermVane" "$desktop_file"; then
        rm -f "$desktop_file"
        rm -f "$icon_file"
        refresh_desktop_database
        refresh_icon_cache
    fi
    exit 0
fi

mkdir -p "$(dirname "$desktop_file")" "$(dirname "$icon_file")"

temporary_desktop_file=$(mktemp "${desktop_file}.XXXXXX")
trap 'rm -f "$temporary_desktop_file"' EXIT HUP INT TERM
cat > "$temporary_desktop_file" <<EOF
[Desktop Entry]
Type=Application
Name=ThermVane
Comment=Cross-platform thermal and fan control
Exec=$target_dir/bin/ThermVane
Icon=thermvane
Terminal=false
Categories=Utility;HardwareSettings;System;
StartupNotify=true
StartupWMClass=ThermVane
EOF
chmod 644 "$temporary_desktop_file"
mv -f "$temporary_desktop_file" "$desktop_file"
trap - EXIT HUP INT TERM

if [ -f "$icon_source" ]; then
    install -m 644 "$icon_source" "$icon_file"
fi

refresh_desktop_database
refresh_icon_cache
