# Bootswap

Bootswap is a lightweight, fast, and accessible Windows utility for managing your boot configuration data. It provides a simple graphical interface to view, reorder, and delete boot entries without needing to use the command line.

## Features

* View all configured boot entries with their descriptions and paths.
* Reorder the boot sequence to prioritize specific operating systems, and Delete unwanted or invalid boot entries.
* Keyboard navigable with the following hotkeys:
  * Alt + Up: Move the selected boot entry up in the list.
  * Alt + Down: Move the selected boot entry down in the list.
  * Delete: Delete the currently selected boot entry.

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
