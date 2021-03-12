
# Development Guide

   If you want to build from source, follow these steps, otherwise skip this section.

   1) Install the SoM sdk.sh to the path you choose or default. Suppose SDKPATH.
   2) Run "./build.sh ${SDKPATH}" to build the somapp application.
   3) The build process in 2) will produce a rpm package SoMApp-1.0.1-1.aarch64.rpm under build/, upload to the board,
      and run "rmp -ivh --force ./DefectDetection_aa4-1.0.1-1.aarch64.rpm" to update install.


# File structure

The application is installed as:

Binary File: => /opt/xilinx/bin

defectdetection_aa4                   main app

configuration File: => /opt/xilinx/share/defectdetection_aa4

|||
|-|-|
| canny-accelarator.json   | Config of canny accelarator.
| defect-calculation.json  | Config of defect calculation.
| edge-tracer.json         | Config of edge tracer accelarator.
| pre-process.json         | Config of pre proess accelarator.


# Fireware Loading

The accelerated application (AA) firmware consist of bitstream, device tree overlay (dtbo) and xclbinfile. The AA firmware is loaded dynamically on user request once Linux is fully booted. The xmutil utility can be used for that purpose.
   1. To list the available AA applications, run:

         `xmutil listapps`

         You should see similar output to this:

>         Accelerator,     Type,   Active
>         kv260-aa4,       flat,   0
>         kv260-aa2,       flat,   0
>         kv260-aa1,       flat,   0

         The Active column shows which AA is currently loaded in the system. It will change to 1 after the firmware is loaded.

   2. To load the AA4 application firmware consisting of PL bitstream, device tree overlay and xclbin,

         run the following command:

         `xmutil loadapp kv260-aa4`

   3. After you are done running the application, you can unload the curently loaded AA application firmware by running:

         `xmutil unloadapp`

# How to run application:

## Prerequisites:

### Monitor

This application needs 4K monitor, so that up to 3 channels of 1280x800 video could be displayed.

Before booting the board, please connect the monitor to the board via either DP or HDMI port.

## Run AA4 application:
       defectdetection_aa4 can accept one or multi groups of options of --infile, --outfile, --width, --height, --framerate, 
       --intype and --cfgpath, to specify the input file location, output file location, width, height, framerate, 
       input type(live/file) and config file path and the displayed position in the 3 grids of the 4k monitor.

       ./defectdetection_aa4  -w 1280 -h 800 -r 60 -f 0                      // for live playback
       ./defectdetection_aa4 -i raw.yuv -o out.yuv -w 1280 -h 800 -r 60 -f 1 // for file playback


4. Command Option:
>       Usage:
>       defectdetection_aa4 [OPTION?] - Application for defect detction on SoM board of Xilinx.

>       Help Options:
>       -?, --help                        Show help options
>       --help-all                        Show all help options
>       --help-gst                        Show GStreamer Options

>       Application Options:
>       -i, --infile=file path            location of GRAY8 file as input
>       -o, --outfile=file path           location of GRAY8 file as output
>       -w, --width=1280                  resolution width of the input
>       -h, --height=800                  resolution height of the input
>       -r, --framerate=60                framerate of the input source
>       -f, --intype                      For live playback value must be 0 otherwise 1
>       -c, --cfgpath=config path         JSON file path

<p align="center"><sup>Copyright&copy; 2021 Xilinx</sup></p>
