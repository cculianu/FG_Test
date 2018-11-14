# FG_Test
FrameGrabber Test Application for Leonadro/Chen Project. As of right now this is a demo and Framegrabber code, while compiled into the app, is disabled.


## Build Instructions

### **Windows**

#### Prerequisites

1. **Windows 7 64-bit or above** (Windows 10 works too).
2. **Microsoft Visual C++ 2017 or above**.  This can be either the Professional Edition or the Community Edition available here: https://visualstudio.microsoft.com/downloads/
3. **Qt 5 Version 5.11 or above**. You can download it here: https://www.qt.io/  (Get the free Open Source edition).
4. Make sure to install everything for Qt -- including **Qt Creator**.  You will need to make sure **Qt5SerialPort** is also installed (these two are usually on by default but it is worth mentioning).
5. *(Optional) Install the Sapera SDk if you want Camera support. Otherwise synthesized data will be used for testing.*

#### Building

1. Open up **Qt Creator**, and select the **"FG_Test.pro"** project file in this source distribution.
2. It will ask you to "configure" the project.  Assuming your Qt and MSVC installs are ok, everything should just work. You can hit "ok". **IMPORTANT** Only **64-bit builds** are supported at this time.  Select the 64-bit version of everything in the configuration screen.
3. You can hit the Green **"Run"** icon on the left, and the app will build and run.  Note that the **Release** version is the one that is fastest as on Windows **Debug** builds of applications tend to run very slowly.


### **macOS**

#### Prerequisites

1. **macOS version 10.13 (High Sierra) or above** (10.14 Mojave should work as well but you may get build warnings which you can ignore).
2. **Xcode 10.1 or above.** Get it for free from the macOS App Store here: https://itunes.apple.com/us/app/xcode/id497799835?mt=12
3. Once Xcode is installed, make sure you have the Xcode command-line tools installed. If you aren't sure if they are installed you can open up a terminal and execute: **`xcode-select --install`**
4. **Qt 5.11 or above.** Get it from https://qt.io/ (Make sure to install everything for your platform including Qt5SerialPort and Qt Creator).

#### Building

1. Open up Qt Creator, and select the **"FG_Test.pro"** project file in this source distribution.
2. It will ask you to "configure" the project.  Assuming your Qt and Xcode installs are ok, everything should just work. You can hit "ok". **IMPORTANT** Only **64-bit builds** are supported at this time.  Select the 64-bit version of everything in the configuration screen.
3. You can hit the Green "Run" icon on the left, and the app will build and run.  *Note that the macOS version is for testing only and as of now cannot communicate with framegraber boards and camera hardware.*


### **Linux**

#### Prerequisites

1. You should be on **Ubuntu 18.10 (Cosmic Cuttlefish)** or similar.  If not Ubuntu, make sure your distro offers FFmpeg 4.0+ as well as Qt 5.10 or above.
2. **GCC 7+** or similar compiler (llvm, clang, are ok too as long as they are recent). Most recent distros are at least on this version.
2. **Qt 5.10 or above**. Make sure you have the **Qt5SerialPort** module also installed.
3. **FFmpeg 4.0.2 or above**.  Please be sure to install the following libs, including their development (dev) versions:
  - **libavcodec libavdevice libavfilter libavformat libavutil libpostproc libswresample libswscale**

#### Building

Follow the instructions above for **macOS**.

