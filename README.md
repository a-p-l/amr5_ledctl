# ACEMAGIC AMR5 Mini PC LED Control

Tools for switching ACEMAGIC AMR5 LED modes through direct I/O port writes on Windows and Linux.

## Repository Contents

- `amr5_ledctl.py`: Linux CLI using `/dev/port`
- `amr5_ledctl.ps1`: Windows PowerShell script using `inpoutx64.dll`
- `amr5_ledctl_win.c`: Windows x64 GUI-style entrypoint using `inpoutx64.dll`
- `inpoutx64.dll`: bundled third-party x64 DLL used by `amr5_ledctl_win.c`

Supported modes: `off`, `button`, `static`, `default`, `rainbow`, `breath`, `cycle`

## Linux Usage

Run as root. `amr5_ledctl.py` supports `--devport` if `/dev/port` is exposed at a different path.

```bash
sudo python amr5_ledctl.py off
sudo python amr5_ledctl.py rainbow
```

## Windows Usage

Run as Administrator with `inpoutx64.dll` in the same directory.

```powershell
C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe -ExecutionPolicy Bypass -File .\amr5_ledctl.ps1 off
C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe -ExecutionPolicy Bypass -File .\amr5_ledctl.ps1 rainbow
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
schtasks /create /tn "Turn off LEDs (AMR5)" /tr "\"C:\path\to\amr5_ledctl_win.exe\" off" /sc onlogon /rl highest /f
schtasks /run /tn "Turn off LEDs (AMR5)"
schtasks /delete /tn "Turn off LEDs (AMR5)" /f
```

### Uninstall

To remove the driver, use an elevated terminal and run:

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
- `amr5_ledctl_win.c` and `amr5_ledctl.ps1` use `inpoutx64.dll`.
- `inpoutx64.dll` reports version `1.5.0.0`.

## References

- [inpoutx64 driver](https://www.highrez.co.uk/downloads/inpout32/default.htm) –
  Windows userspace I/O port access driver.

## License

This repository is licensed under the MIT License. See `LICENSE`.
