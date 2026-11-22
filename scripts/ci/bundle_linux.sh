#!/usr/bin/env bash
set -euo pipefail

stage="${RUNNER_TEMP:?}/vortexdm-linux"
app="$stage/bin/vortexdm"
rm -rf "$stage"
cmake --install build/desktop --prefix "$stage"
test -x "$app" || { echo "Installed executable missing: $app" >&2; exit 1; }
test -d "$stage/lib" || { echo "Runtime library directory missing: $stage/lib" >&2; exit 1; }

shopt -s nullglob
qt_libs=("$stage"/lib/libQt6*.so*)
if (( ${#qt_libs[@]} == 0 )); then
  echo "Qt shared libraries were not deployed into $stage/lib" >&2
  find "$stage" -maxdepth 4 -type f | sort | head -n 200
  exit 1
fi

vcpkg_lib="$GITHUB_WORKSPACE/build/desktop/vcpkg_installed/x64-linux-release/lib"
if [[ -d "$vcpkg_lib" ]]; then
  optional_shared=("$vcpkg_lib"/*.so "$vcpkg_lib"/*.so.*)
  if (( ${#optional_shared[@]} > 0 )); then
    cp -av "${optional_shared[@]}" "$stage/lib/"
  fi
fi

cat > "$stage/VortexDM" <<'WRAPPER'
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
[[ ! -d "$ROOT/plugins" ]] || export QT_PLUGIN_PATH="$ROOT/plugins"
[[ ! -d "$ROOT/plugins/platforms" ]] || export QT_QPA_PLATFORM_PLUGIN_PATH="$ROOT/plugins/platforms"
[[ ! -d "$ROOT/qml" ]] || export QML2_IMPORT_PATH="$ROOT/qml"
exec "$ROOT/bin/vortexdm" "$@"
WRAPPER
chmod +x "$stage/VortexDM"

LD_LIBRARY_PATH="$stage/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$app" | tee "$RUNNER_TEMP/vortexdm-ldd.txt"
if grep -q 'not found' "$RUNNER_TEMP/vortexdm-ldd.txt"; then
  echo "Unresolved shared libraries in Linux bundle" >&2
  exit 1
fi

tar -C "$stage" -czf "$GITHUB_WORKSPACE/VortexDM-linux-x64.tar.gz" .
