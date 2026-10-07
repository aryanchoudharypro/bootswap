# Bootswap

Bootswap is a lightweight, fast, and accessible Windows utility for managing your boot configuration data. It provides a simple graphical interface to view, reorder, and delete boot entries without needing to use the command line.

## Features

* View all configured boot entries with their descriptions and paths.
* Reorder the boot sequence to prioritize specific operating systems, and Delete unwanted or invalid boot entries.
* Keyboard navigable with the following hotkeys:
  * Alt + Up: Move the selected boot entry up in the list.
  * Alt + Down: Move the selected boot entry down in the list.
  * Delete: Delete the currently selected boot entry.

* Boot list selector: switch between the UEFI firmware boot entries and the Windows boot menu entries. On legacy BIOS systems only the Windows boot menu list applies.
* Add ISO or VHD entries (Ctrl + A), which map an image on any local drive directly to a boot option:
  * Windows setup or recovery ISO: copies sources\boot.wim and boot\boot.sdi from the ISO and creates a RAM disk entry in the Windows boot menu. Optionally copies install.wim or install.esd to the drive's sources folder so Windows Setup can find it.
  * Linux or other ISO: starts the ISO through GRUB loopback. On UEFI you supply a GRUB2 EFI file, which is copied to the EFI system partition with a generated grub.cfg, and a firmware boot entry is created. On legacy BIOS you supply GRUB4DOS (grldr and grldr.mbr in one folder) and the ISO is added to a shared GRUB4DOS menu.
  * Windows installed in a VHD or VHDX file: creates a native VHD boot entry.
* All boot configuration changes go through the Windows BCD WMI provider, and images are opened with Windows APIs, so nothing depends on the language of command line tools.
* Rename (F2), set as default entry (Ctrl + D), boot menu timeout, and backup and restore of the whole boot configuration, all from the menu bar.
* Deleting a firmware entry created for a GRUB ISO also removes its files from the EFI system partition. Files copied for Windows ISO entries are kept in the Bootswap folder on the ISO's drive and can be deleted by hand.
* Everything uses standard Windows controls, labels, menus and message boxes, so it works with screen readers and the keyboard alone.

Getting a loader (only needed for Linux or other ISOs, and Bootswap does not download anything): for UEFI, build a GRUB2 EFI file on any Linux machine with `grub-mkimage -O x86_64-efi -p "" -o grubx64.efi part_gpt part_msdos fat ntfs exfat ext2 iso9660 loopback search configfile normal echo test sleep chain`, then pick it in the Add dialog. For legacy BIOS, take `grldr` and `grldr.mbr` from a GRUB4DOS release and keep them in the same folder.

GRUB2 note: the GRUB2 EFI file must read grub.cfg from the folder it runs from (for example an image built with grub-mkimage and an empty prefix). Images with a fixed embedded prefix or embedded config will ignore the generated grub.cfg. The ISO must also use a file system GRUB can read, and the ISO itself must support loopback booting (most Ubuntu, Debian, Fedora and similar ISOs do).

Note: Modifying boot entries requires administrator privileges. The application will prompt for elevation when launched.

## Prerequisites

To compile Bootswap from source, you need to have the following tools installed on your system:

* Visual Studio Build Tools with C++17 support.
* Windows SDK.
* CMake (version 3.15 or higher). Make sure CMake is in your system path.

## Building

Once the dependencies are installed, you can build the application by running the following commands in the repository root:

```batch
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

When the build is finished, the executable will be located in the build\Release directory.

## License

This project is licensed under the MIT License.
