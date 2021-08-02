#
# Copyright 2021 Xilinx Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
# Below pipeline is to run the live playback. Only MIPI source is supported.
# media node has to be changed as per media entry.
#
#
if [ $# -eq 0 ]
  then
    echo "No arguments supplied"
    echo "Please give a v4l2 sub device in the command line i.e. sudo ar0144-sensor-calib.sh 0"
    echo "v4l-subdev of sensor can be obtained from media graph"
    exit 1
fi

subdev=${1}

# *********************** GAMMA ************************
# To set the GAMMA value as 1.5 write gamma=0x1800
# To set the GAMMA value as 2 write gamma=0x2000
# To set the GAMMA value as 2.2 write gamma=0x2333
# To set the GAMMA value as 2.5 write gamma=0x2800
v4l2-ctl -d /dev/v4l-subdev${subdev} -c gamma=0x1800

# *********************** Exposure  ********************
# To set auto exposure write 'exposure=12'
# For manual mode write 'exposure=0'
v4l2-ctl -d /dev/v4l-subdev${subdev} -c exposure=12

# **************************** GAIN ********************
# To set the gain value 5 write 'gain=0x0500'
# To set the gain value 3 write 'gain=0x0300'
v4l2-ctl -d /dev/v4l-subdev${subdev} -c gain=0x500

# *********************** Exposure Metering *****************************
# To set AE_Metering Mode as Average write 'exposure_metering=0'
# To set AE_Metering Mode as wide center write 'exposure_metering=0x1'
# To set AE_Metering Mode as narrow center write 'exposure_metering=0x2
# To set AE_Metering Mode as spot write 'exposure_metering=0x3
v4l2-ctl -d /dev/v4l-subdev${subdev} -c exposure_metering=0x2

# *********************** Satruation *******************
# To set Saturation as 0 write 'saturation=0'
# To set Saturation as 0.5 write 'saturation=0x800'
# To set Saturation as 1 write 'saturation=0x1000'
# To set Saturation as 1.5 write 'saturation=0x1800'
# To set Saturation as 2 write 'saturation=0x2000'
v4l2-ctl -d /dev/v4l-subdev${subdev} -c saturation=0x800

# *********************** Contrast *********************
# To set contrast as -5.0 write 'contrast=0xB000'
# To set contrast as -2.5 write 'contrast=0xD800'
# To set contrast as 0 write 'contrast=0x0000'
# To set contrast as 2.5 write 'contrast=0x2800'
# To set contrast as 5 write 'contrast=0x5000'
v4l2-ctl -d /dev/v4l-subdev${subdev} -c contrast=0xB000

# *********************** Brightness *******************
v4l2-ctl -d /dev/v4l-subdev${subdev} -c brightness=0xFF
