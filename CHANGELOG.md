# Changelog

## [2.9.0] - 2026-11-20

- **Version Alignment**: Synced application UI text, Atrium.Settings assembly version, and installer version to 2.9.0 so the **About** page and update checks report 2.9.0 consistently.
- **Localization & Tests**: Fixed UTF-8 (no BOM) encoding, synced missing localization keys, and kept localization checks (check-localization.ps1 and localization_tests.exe) passing.
- **Build & Packaging**: Updated build and packaging steps for portable (dist/WINMAK) and installer releases.
- **Localization Scope**: Removed/excluded the Indonesian (id) language addition from this release.
