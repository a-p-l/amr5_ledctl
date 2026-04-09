# Acemagic AMR5 MiniPC LED Control

Tools for switching Acemagic AMR5 LED modes through direct I/O port writes on Windows and Linux.

## Repository Contents

- `amr5_ledctl.py`: Linux CLI using `/dev/port`
- `amr5_ledctl_win.c`: Windows x64 GUI-style entrypoint using `inpoutx64.dll`
- `inpoutx64.dll`: bundled third-party x64 DLL used by `amr5_ledctl_win.c`

Supported modes: `off`, `button`, `static`, `default`, `rainbow`, `breath`, `cycle`

## Linux Usage

Run as root. `amr5_ledctl.py` supports `--devport` if `/dev/port` is exposed at a different path.

```bash
sudo python amr5_ledctl.py off
sudo python amr5_ledctl.py rainbow --devport /dev/port
```

## Windows Usage

Must run as Administrator with `inpoutx64.dll` in the same directory.

```powershell
.\amr5_ledctl_win.exe off
.\amr5_ledctl_win.exe rainbow
```

### Build

For `amr5_ledctl_win.c`, switch Developer PowerShell to x64 first with `Launch-VsDevShell -Arch amd64 -HostArch amd64`, or use the `x64 Native Tools Command Prompt for Visual Studio`.

```powershell
Launch-VsDevShell -Arch amd64 -HostArch amd64
cl /O2 /MT amr5_ledctl_win.c /Fe:amr5_ledctl_win.exe /link /SUBSYSTEM:WINDOWS user32.lib
```

### Scheduled Task Example

```powershell
schtasks /create /tn "AMR5 LED Off" /tr "\"C:\path\to\amr5_ledctl_win.exe\" off" /sc onlogon /rl highest /f
schtasks /run /tn "AMR5 LED Off"
schtasks /delete /tn "AMR5 LED Off" /f
```

### Uninstall

To remove the driver use an elevated terminal and do this:

```powershell
sc.exe query inpoutx64
sc.exe stop inpoutx64
sc.exe delete inpoutx64
takeown /f C:\Windows\System32\drivers\inpoutx64.sys
icacls C:\Windows\System32\drivers\inpoutx64.sys /grant "%USERNAME%":F
attrib -r -s -h C:\Windows\System32\drivers\inpoutx64.sys
del /f C:\Windows\System32\drivers\inpoutx64.sys
```

## Notes

- `static` and `button` use a short delay before the final mode write on all implementations.
- `amr5_ledctl_win.c` uses `inpoutx64.dll`.
- `inpoutx64.dll` report version `1.5.0.0`. Their file metadata marks them as `Freeware`.
- This repository is MIT-licensed, but the bundled DLLs should be treated as separate third-party dependencies unless you verify and document their license terms explicitly.

## License

This repository is licensed under the MIT License. See `LICENSE`.
