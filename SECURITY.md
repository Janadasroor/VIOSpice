# Security Policy

## Supported Versions

| Branch | Supported          |
|--------|--------------------|
| `main` | ✅ Active support  |
| Other  | ❌ Not supported   |

Security fixes are applied to the `main` branch only. Users are encouraged to always run the latest version from `main`.

## Reporting a Vulnerability

**Please do not open public GitHub issues for security vulnerabilities.**

If you discover a security vulnerability in VioraEDA, please report it responsibly:

1. **Email the maintainers** with a detailed description of the vulnerability. Include the subject line `[SECURITY] VioraEDA vulnerability report`.
2. **Include** the following information:
   - Description of the vulnerability and its potential impact
   - Steps to reproduce the issue
   - Affected components and versions
   - Any suggested fixes, if applicable
3. **Response timeline**: We will acknowledge your report within **48 hours** and aim to provide an initial assessment within **7 days**.
4. **Disclosure window**: We follow a **90-day responsible disclosure** policy. We ask that you do not publicly disclose the vulnerability until:
   - A fix has been released, **or**
   - 90 days have elapsed since the initial report, whichever comes first.
5. **Credit**: We will credit reporters in the release notes and SECURITY.md unless anonymity is requested.

## Security-Sensitive Areas

The following components handle untrusted input or perform privileged operations and require extra scrutiny during review:

### FluxScript JIT Sandbox

The FluxScript just-in-time compilation engine (`simulator/jit/`) executes user-provided scripts via LLVM. The sandbox must enforce:
- Memory access restrictions (no arbitrary pointer dereference)
- Execution time limits (prevent infinite loops)
- Restricted system call surface (no file system or network access from scripts)

### Python Console

The embedded Python console (`src/console/`) provides interactive scripting. Security considerations:
- Python code runs with the same privileges as the VioraEDA process
- Imported modules may have side effects
- The console must clearly warn users when executing untrusted scripts

### File I/O and Parsing

Netlist parsers, SPICE model importers, and project file loaders (`src/io/`, `simulator/parser/`) process untrusted input files. Risks include:
- Buffer overflows from malformed input
- Path traversal via crafted file references
- Denial of service through deeply nested or excessively large files

### WebSocket Server

The optional WebSocket server (`src/server/`) exposes VioraEDA functionality to external tools. Security considerations:
- Authentication and authorization for incoming connections
- Input validation on all received messages
- Rate limiting to prevent denial of service

## Past Security Fixes

### July 2026

**Command injection in language bindings** — The language binding layer used `popen()` to spawn child processes, which was vulnerable to shell injection through crafted arguments. The implementation was replaced with `fork()` + `execvp()`, which bypasses the shell entirely and eliminates the injection vector.

**Python code injection in `cd` magic command** — The `cd` magic command in the Python console constructed a Python expression from user input without proper sanitization, allowing arbitrary code execution. The command was rewritten to use `os.chdir()` with properly validated and quoted path arguments.
