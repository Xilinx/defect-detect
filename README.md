
# Development Guide

If you want to cross compile the source in Linux PC machine, follow these steps, otherwise skip this section.

1. Refer to the `K260 SOM Starter Kit Tutorial` to build the cross-compilation SDK, and install it to the path you choose or default. Suppose it's SDKPATH.

2. Run "./build.sh ${SDKPATH}" in the source code folder of current application, to build the application. <a name="build-app"></a>

3. The build process in [2](#build-app) will produce a rpm package under build/, upload to the board, and run "rpm -ivh --force ./DefectDetect-2.0.1-1.aarch64.rpm" to install updates.

# Setting up the Board

1. Get the SD Card Image from [Boot Image Site](http://xilinx.com/) and follow the instructions in UG1089 to burn the SD card. And install the SD card to J11.

2. Hardware Setup:

    * Monitor:

      This application requires **4K** monitor, so that up to 3 channels of 1280x800 video could be displayed.

      Before booting the board, please connect the monitor to the board via either DP or HDMI port.

    * UART/JTAG interface:

      For interacting and seeing boot-time information, connect a USB debugger to the J4.

    * Network connection:

      Connect the Ethernet cable to your local network with DHCP enabled to install packages and run Jupyter Notebooks

3. Power on the board, login with username `petalinux`, and you need to setup the password for the first time bootup.

4.  Get the latest application package.

    1.  Get the list of available packages in the feed.

        `sudo xmutil      getpkgs`

    2.  Install the package with dnf install:

        `sudo dnf install packagegroup-kv260-defect-detect.noarch`

      Note: For setups without access to the internet, it is possible to download and use the packages locally. Please refer to the `K260 SOM Starter Kit Tutorial` for instructions.

5.  Dynamically load the application package.

    The firmware consist of bitstream, device tree overlay (dtbo) and xclbin file. The firmware is loaded dynamically on user request once Linux is fully booted. The xmutil utility can be used for that purpose.

    1. Shows the list and status of available acceleration platforms and Applications:

        `sudo xmutil      listapps`

    2.  Switch to a different platform for different Application:

        *  When xmutil listapps reveals that no accelerator is currently active, select the desired application:

            `sudo xmutil      loadapp kv260-defect-detect`

        *  When xmutil listapps reveals that an accellerator is already active, unload it first, then select the desired application:

            `sudo xmutil      unloadapp <NAME_OF_ACTIVE_APP>`

            `sudo xmutil      loadapp kv260-defect-detect`

# How to run the application:

## Interacting with the application
    The application is targeted to run only file based input.

    We assume input file is to support resolution=1280x800(width=1280 and height=800) and format=GRAY8(Y8)

There are two ways to interact with application, via Jyputer notebook or Command line

### Juypter notebook

Use a web-browser (e.g. Chrome, Firefox) to interact with the platform.

The Jupyter notebook URL can be found with command:

> sudo jupyter notebook list

Output example:

> Currently running servers:
>
> `http://ip:port/?token=xxxxxxxxxxxxxxxxxx`  :: /opt/xilinx/share/notebooks

### Command Line

**Note** The application needs to be run with ***sudo*** .

#### Examples:
   **Note** Only one instance of the application can run at a time.

   * For File-In and File-Out playback, run below command.
     > sudo defect-detect -i input.y8 -x raw.y8 -y pre_pros.y8 -z final.y8

   **Note** All 3 stage output will be dumped into file. It's must to give option for all 3 output file name.

   * For File-In and Display-Out playback, run below command.
     > sudo defect-detect -i input.y8

   **Note** All 3 stage outputs will be displayed on DP/HDMI. Input file path needs to be changed as per the requirement.

   * For Live-In and File-Out playback, run below command.
     > sudo defect-detect -x raw.y8 -y pre_pros.y8 -z final.y8

   **Note** All 3 stage output will be dumped into file. It's must to give option for all 3 output file name.

   * For Live-In and Display-Out playback, run below command.
     > sudo defect-detect

   **Note**  All 3 stage outputs will be displayed on DP/HDMI.

#### Command Options:

The examples show the capability of the defect-detect for specific configurations. User can get more and detailed application options as following by invoking

`   defect-detect --help`

```
   Usage:

   defect-detect [OPTION?] - Application for defect detction on SoM board of Xilinx.

   Help Options:

   -h, --help      Show help options

        --help-all                                       Show all help options
        --help-gst                                       Show GStreamer Options

   Application Options:
          -i, --infile=file path             location of input file
          -x, --rawout=file path             location of capture raw output file
          -y, --preprocessout=file path      location of pre-processed output file
          -z, --finalout=file path           location of final output file
          -w, --width=1280                   resolution width of the input
          -h, --height=800                   resolution height of the input
          -r, --framerate=60                 framerate of the input source
          -d, --demomode                     For Demo mode value must be 1, otherwise 0, default is 0
          -c, --cfgpath=config file path     JSON config file path
```

# Files structure

* The application is installed as:

    * Binary File Directory: /opt/xilinx/bin

        | Filename        | Description |
        |-----------------|-------------|
        | defect-detect   | main app    |

    * Script File Directory: /opt/xilinx/bin

      | Filename                        | Description                                                  |
      |---------------------------------|--------------------------------------------------------------|
      | `defect-detect-install.py`      | Script to copy Jupyter notebook to user directory.           |

    * Configuration file directory: /opt/xilinx/share/ivas/defect-detect

        | Filename                    | Description                               |
        |-----------------------------|-------------------------------------------|
        | cca-accelarator.json        | Config of CCA accelarator.                |
        | otsu-accelarator.json       | Config of OTSU accelarator.               |
        | preprocess-accelarator.json | Config of pre-process accelarator.        |
        | text2overlay.json           | Config of text2overlay.                   |

     * Jupyter Notebook Directory:  /opt/xilinx/share/notebooks/defect-detect/

       | Filename             | Description                              |
       |----------------------|------------------------------------------|
       | defect-detect.ipynb  | Jupyter notebook file for defect detect  |


<p align="center"><sup>Copyright&copy; 2021 Xilinx</sup></p>
