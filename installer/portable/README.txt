Downloader Portable v1.0.0
==========================

USAGE

1) Unzip Downloader-portable.zip into any folder (avoid paths with non-ASCII
   characters; some network stacks trip on those).
2) Either double-click Downloader.exe, or use the included start.bat.
   start.bat also pins the working directory so DLLs sitting next to
   Downloader.exe are picked up reliably.

DATA LOCATION

  - History / settings : %APPDATA%\Programming666\Downloader\
  - Default downloads  : %USERPROFILE%\Downloads\
  - Log file           : %APPDATA%\Programming666\Downloader\downloader.log

URL PROTOCOL (downloader://)

  --register-protocol and --unregister-protocol both work in portable mode.
  Registration writes only to HKCU - zero system-wide footprint.
  To undo the registration, run  Downloader.exe --unregister-protocol
  (or simply delete the folder).

UNINSTALL

  1) Close all Downloader processes.
  2) Delete the extracted folder.
  3) Optional: run Downloader.exe --unregister-protocol to revoke the protocol.

NOTE

  - Windows Firewall may pop up asking about network access on first launch -
    allow it.
  - Copying this folder onto a USB stick and running from there is the whole
    point of "portable". Settings stay in HKCU/%APPDATA% on the host machine,
    so multiple hosts can carry the same archive but each gets its own state.
