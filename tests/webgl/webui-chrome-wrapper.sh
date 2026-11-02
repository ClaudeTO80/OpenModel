#!/bin/bash
# Test helper: makes WebUI "find" a Chrome that is actually a headless Chromium with a
# DevTools port, so tests/webgl/gui_e2e.js can drive the real GUI.
#   export OPENMODEL_TEST_CHROMIUM=/path/to/chrome   (e.g. Playwright's Chromium)
#   mkdir -p bin && cp webui-chrome-wrapper.sh bin/google-chrome && chmod +x bin/google-chrome
#   PATH=$PWD/bin:$PATH openmodel-webui &
CH="${OPENMODEL_TEST_CHROMIUM:?set OPENMODEL_TEST_CHROMIUM to a Chromium/Chrome executable}"
if [ "$1" = "--version" ]; then echo "Google Chrome 130.0.0.0"; exit 0; fi
exec "$CH" --headless=new --no-sandbox --remote-debugging-port=9222 \
     --use-angle=swiftshader --enable-unsafe-swiftshader --ignore-gpu-blocklist "$@"
