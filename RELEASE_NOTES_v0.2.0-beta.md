# VioraEDA Suite v0.2.0-beta

Welcome to the **VioraEDA Suite v0.2.0-beta** release. This release delivers major upgrades across the entire EDA suite, including a high-performance custom installer, complete version synchronization, enhanced simulation tooling, and full cross-platform packaging.

---

## What's New in v0.2.0-beta

### 🚀 Modern Installer & Packaging
* **Custom Dark Installer UI**: Dedicated high-performance native setup wizard with dynamic progress monitoring, exact byte indicators, and styled title bars.
* **Unattended / Silent Installation Support**: Automated deployment via `/S` and configurable install targets via `/D=<path>`.
* **Complete Component Deployment**: Installs core binaries (`VioraEDA`, `viora` CLI, `vioavr`, `flux_runner`), simulation code models (`.cm`), bundled Qt/MinGW runtimes, templates, and the component library (`ViospiceLib`).
* **Clean System Integration**: Desktop and Start Menu shortcuts, file associations (`.flxsch`, `.flux`, `.flxpcb`, `.cir`, `.sp`), and Windows Add/Remove Programs registry registration.

### ⚡ Core & Simulation Architecture
* **Version Standardization**: Synchronized `0.2.0-beta` baseline across CMake, core GUI, CLI engine, and metadata.
* **Smart Signal JIT**: LLVM-powered behavioral modeling with sub-millisecond execution and case-insensitive pin lookups.
* **Standalone Oscilloscope & Simulation Dock**: Visual real-time analog wave probing and measurement tools.

---

## Downloads & Assets

| Asset | Description |
|---|---|
| `VioraEDA-v0.2.0-beta-windows-x86_64-Setup.exe` | Windows 64-bit Interactive & Silent Installer |
| `VioraEDA-v0.2.0-beta-windows-x86_64.zip` | Windows 64-bit Portable Package |

---
*For documentation, issue tracking, and contributions, visit the [VioraEDA Repository](https://github.com/Janadasroor/VioraEDA).*
