#!/bin/bash
set -euo pipefail

# Run on macOS after building and signing the AU. No installer scripts run as root.
component="${1:-build/BassRack_artefacts/Release/AU/Bass Rack.component}"
output="${2:-build/installers}"
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$component/Contents/Info.plist")
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

destination="$stage/root/Library/Audio/Plug-Ins/Components"
mkdir -p "$destination" "$output" "$stage/image"
/usr/bin/ditto "$component" "$destination/Bass Rack.component"

/usr/bin/pkgbuild --analyze --root "$stage/root" "$stage/components.plist"
# Disable relocation to enforce the documented system location.
/usr/libexec/PlistBuddy -c 'Set :0:BundleIsRelocatable false' "$stage/components.plist"

name="BassRack-$version-macOS-universal"
package="$output/$name.pkg"
/usr/bin/pkgbuild \
  --root "$stage/root" \
  --component-plist "$stage/components.plist" \
  --identifier audio.crimson.bassrack.installer \
  --version "$version" \
  --install-location / \
  --ownership recommended \
  "$package"

/usr/sbin/pkgutil --payload-files "$package" > "$output/payload-files.txt"
/usr/bin/grep -F 'Library/Audio/Plug-Ins/Components/Bass Rack.component/Contents/Info.plist' "$output/payload-files.txt"

/usr/bin/ditto "$package" "$stage/image/$name.pkg"
cat > "$stage/image/Install Bass Rack.txt" <<'INSTRUCTIONS'
Bass Rack 0.1.0 - development build

1. Quit Logic Pro.
2. Double-click the BassRack .pkg installer and follow the Installer prompts.
   It installs Bass Rack.component into /Library/Audio/Plug-Ins/Components
   for all users. Administrator authorization is required.
3. Reopen Logic. Find Audio Units > Crimson Audio > Bass Rack.
   Start with the "808 - clean sub" factory settings.

This development installer is unsigned and is not notarized by Apple.
macOS may require explicit approval in System Settings > Privacy & Security.
It is not a Developer ID signed public release.

If you previously installed a copy manually in your user's
~/Library/Audio/Plug-Ins/Components folder, remove that older Bass Rack.component
before installing, so Logic does not see two copies.

Uninstall: quit Logic and remove only
/Library/Audio/Plug-Ins/Components/Bass Rack.component.
Your saved presets and third-party plugins are not removed.
INSTRUCTIONS

/usr/bin/hdiutil create -volname "Bass Rack Installer" -srcfolder "$stage/image" \
  -format UDZO -ov "$output/$name.dmg"
/usr/bin/hdiutil verify "$output/$name.dmg"
(cd "$output" && /usr/bin/shasum -a 256 "$name.pkg" "$name.dmg" > SHA256SUMS.txt)
echo "Created $package and $output/$name.dmg"
