# Installer / Signing

The source release intentionally does not ship fake certificates or fake licensing.

For production distribution:
- Windows: sign installer and VST3 binary with a trusted code-signing certificate.
- macOS: Developer ID sign and notarize VST3/AU bundles.
- Add final license endpoint and EULA.
- Produce reproducible release artifacts and checksums.
