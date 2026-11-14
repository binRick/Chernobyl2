#!/usr/bin/env bash
# Archive, export and upload Chernobyl 2 to App Store Connect (TestFlight).
# As ../ActuallyOnline/scripts/ios-appstore.sh.
#
#   DRY_RUN=1 scripts/ios-appstore.sh        build build/ios-store/AppStore/Chernobyl2.ipa only
#   ASC_KEY_ID=... ASC_ISSUER_ID=... BUILD=2 scripts/ios-appstore.sh
#
# The marketing version is MARKETING_VERSION in ios/project.yml. BUILD (default 1) must go up with
# every upload. The key is ~/.appstoreconnect/private_keys/AuthKey_<ASC_KEY_ID>.p8.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD="${BUILD:-1}"
ARCHIVE=build/ios-store/Chernobyl2.xcarchive
OUT=build/ios-store/AppStore

if [[ "${DRY_RUN:-0}" != "1" && ( -z "${ASC_KEY_ID:-}" || -z "${ASC_ISSUER_ID:-}" ) ]]; then
    echo "Set ASC_KEY_ID and ASC_ISSUER_ID (key in ~/.appstoreconnect/private_keys), or DRY_RUN=1." >&2
    exit 1
fi

echo "==> Packing the phone's assets (the browser build's set, and the intro)"
python3 tools/web/pack_web.py | tail -2

echo "==> Archiving build $BUILD"
( cd ios && xcodegen generate --quiet )
rm -rf "$ARCHIVE" "$OUT"
xcodebuild archive -project ios/Chernobyl2.xcodeproj -scheme Chernobyl2 -configuration Release \
    -destination 'generic/platform=iOS' -archivePath "$ARCHIVE" -derivedDataPath build/ios-store \
    -allowProvisioningUpdates CURRENT_PROJECT_VERSION="$BUILD" -quiet

echo "==> Exporting"
xcodebuild -exportArchive -archivePath "$ARCHIVE" -exportPath "$OUT" \
    -exportOptionsPlist ios/ExportOptions-AppStore.plist -allowProvisioningUpdates | tail -2
IPA="$(ls "$OUT"/*.ipa | head -1)"      # named after the product ("Chernobyl 2.ipa")
echo "    $IPA"

[[ "${DRY_RUN:-0}" == "1" ]] && exit 0

AUTH=(--apiKey "$ASC_KEY_ID" --apiIssuer "$ASC_ISSUER_ID")
echo "==> Validating"
xcrun altool --validate-app -f "$IPA" -t ios "${AUTH[@]}" 2>&1 | tail -20
echo "==> Uploading"
xcrun altool --upload-app -f "$IPA" -t ios "${AUTH[@]}" 2>&1 | tail -20
