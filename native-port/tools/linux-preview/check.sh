#!/usr/bin/env bash
set -euo pipefail

# Source is mounted read-only. Build products live in a dedicated Linux volume;
# only the selected evidence is copied back to the Windows workspace.
mkdir -p /build /evidence
printf '{"passed":false,"status":"checks running"}\n' > /evidence/preview-results.json
cmake -S /src/native-port -B /build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DOPENSIM_REQUIRE_TLS=ON -DOPENSIM_DIFFERENTIAL_TESTS=OFF \
    2>&1 | tee /evidence/configure.log
cmake --build /build --parallel "${OPENSIM_BUILD_JOBS:-8}" --target \
    opensim-native viewer_session_probe viewer_region_tests \
    background_task_queue_tests transport_tests region_runtime_tests \
    2>&1 | tee /evidence/build.log
sha256sum /build/opensim-native > /evidence/native-sha256.txt
sha256sum /src/native-port/src/main.cpp /src/native-port/src/region_runtime.cpp \
    /src/native-port/tests/preview_entry_network.py > /evidence/source-sha256.txt
ctest --test-dir /build --output-on-failure \
    -R '^(preview_entry_network|background_task_queue_tests|transport_tests|viewer_region_tests|region_runtime_tests)$' \
    2>&1 | tee /evidence/tests.log
cp /build/preview-entry-network/results.json /evidence/preview-results.json
cp /build/preview-entry-network/server.log /evidence/preview-server.log
