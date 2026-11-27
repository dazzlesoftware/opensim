OpenSim native single-region preview - %RELEASE%

Windows x64
Extract the Windows ZIP into a writable folder. In PowerShell run:
  .\Start-Preview.ps1
Add http://127.0.0.1:19092/ to an OpenSim-compatible Firestorm viewer.
Type quit in the server console for a clean stop. Run the same script again
to restore the same region. The package includes its native runtime DLLs.

Linux x64
The native archive targets Ubuntu 24.04 amd64. Extract it with tar, then run:
  ./start-preview.sh
Grid address: http://127.0.0.1:19093/
The Dockerfile lists the required native system libraries. For a direct Linux
archive installation, also install the font provider and font files:
  sudo apt-get install libgdiplus fontconfig fonts-liberation
libgdiplus is loaded dynamically for installed-font enumeration; checking the
server executable with ldd alone does not detect this requirement. Docker
bundles these dependencies and is the simplest Linux preview setup on Windows.

Docker Desktop / Linux containers
Keep Start-Linux-Docker.ps1 beside the compressed image archive, then run:
  .\Start-Linux-Docker.ps1
  .\Start-Linux-Docker.ps1 -Mode Stop
Docker Desktop must already be running in Linux-container mode. First start
loads the image if necessary. Grid address: http://127.0.0.1:19093/
The default container is opensim-native-packaged-preview-%RELEASE%; its volume
has the same name followed by -data. Stop preserves both. Releases use separate
containers and volumes, and do not automatically migrate an older preview.
Use -Instance another-name to keep a separate test world, and -Port 19103 to
select a different HTTP/UDP port. Reuse the same options when starting it again.
Linux hosts can instead docker load -i the image archive and use the supplied
Dockerfile/start script as the reference for port and data-volume settings.

Two starter accounts
  Preview First
  Preview Second
Initial password for both: preview-password
OPENSIM_PREVIEW_PASSWORD overrides the initial password at account creation.
Changing that environment variable later does not reset existing passwords.
These are local test accounts. Published Docker ports bind to 127.0.0.1.

Data and configuration
Native launchers create data/ plus preview.ini beside the extracted runtime.
Linux keeps its configuration in data/preview.ini through a preview.ini link.
Reuse that folder/volume to retain objects, inventory, accounts and appearance.
Windows defaults to port 19092; Linux/Docker defaults to 19093. Existing
configuration is retained. These packages contain no QA or TestBuild data and
do not migrate or modify the existing TestBuild installation. No .NET runtime
is required. Existing managed assembly names in INI module settings are
compatibility identifiers interpreted by the native server.

Verification and remaining work
See the bundled evidence and verification.json for checks run against this
specific package. Automated protocol fixtures are distinct from viewer UI tests.
Earlier 20261124 real Windows Firestorm sessions against Windows and Linux
servers verified automatic clothing, two avatars, movement, editing,
inventory drag-to-world and clean restoration. Some GUI actions were assisted
by the user. No Linux GUI Firestorm client was tested.
Fresh account creation, two-account travel, inventory/appearance isolation,
movement, wearable asset downloads, building and clean restart have also been
checked with original Robust/original-region/native-region protocol fixtures.
Those results do not establish a fresh Firestorm GUI pass for this package.

The historical intermittent RegionHandshake timeout's cause remains
unconfirmed. After an unclean
shutdown a first login may report an existing session. The original managed
server also clears that stale presence during the failed attempt; retry the
ordinary login. No database edits are needed for that recovery.
This is a scoped preview, not a complete 1:1 parity release. Historical timeout
diagnosis, exhaustive parity/edge cases and macOS remain outstanding.

Use SHA256SUMS.txt to verify delivered archives. Evidence JSON distinguishes
automated protocol checks from prior real-viewer observations.
