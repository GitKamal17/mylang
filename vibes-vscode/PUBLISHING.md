# Publishing MyLang

## What is ready

`npm ci && npm run package` builds a fresh compiler from source and packages a
native VSIX for the build host. No compiler download occurs on users' machines.
CI builds darwin-arm64, darwin-x64, linux-x64, and linux-arm64 packages and tests
the compiler extracted from each VSIX in an isolated directory.

The macOS packages target macOS 14+. Linux CI packages are built on Ubuntu 22.04
(glibc 2.35); use a compatible or newer glibc distribution. Alpine/musl and native
Windows packages are not supplied. Windows users install the Linux extension in WSL.

A tagged release (`v*`) creates a draft GitHub release with the platform VSIXs
and standalone compiler archives. Review the draft before publishing it.
Do not upload a macOS binary under a Linux target or publish a universal VSIX.

## One-time publisher setup (owner action)

1. Create or sign into a publisher at https://marketplace.visualstudio.com/manage .
2. Verify that you own the `mylang` publisher ID currently in package.json. That ID
   is a placeholder until ownership is confirmed. If unavailable, change it in
   package.json and the recommendation written by project.js, then rebuild.
3. Review the public README, MIT license, supported platforms, and extension name.
4. Configure publishing credentials using the official VS Code publishing guide:
   https://code.visualstudio.com/api/working-with-extensions/publishing-extension .
   Never put credentials in this repository or the extension package.

## Publish a tested version

Merge the reviewed PR first. Bump the extension version in package.json and
package-lock.json for each new release. Run the platform CI workflow and download
all four artifacts. Marketplace allows platform packages to share a version.

Upload each VSIX through your publisher management page, or use authenticated
`vsce publish --packagePath <platform-file.vsix>` from a release workstation.
For automation, configure a Marketplace Trusted Publishing policy and use
`vsce publish --oidc --packagePath <platform-file.vsix>` in the authorized GitHub
workflow with `id-token: write`. No automatic Marketplace publishing is enabled
in this repository: account ownership must be established first.

Before public publication, test installation from VSIX on clean supported machines:
Create Project → Hello World → Run; repeat with a game and Raylib installed.
Verify missing-Clang instructions, missing-Raylib instructions, workspace trust,
and an explicit compiler override. VS Code should choose the matching platform
package automatically when installed from Marketplace.

## Dependencies users still need

Clang plus operating-system development headers are needed for Run/Build.
Raylib development headers and libraries are needed only for games.
Check/emit use only the bundled compiler. The extension detects missing tools
and explains installation; it does not install system software or require sudo.
