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
if [ $# -eq 0 ]
  then
    echo "Please give AR0144 media node as an input i.e. sudo ar0144-sensor-calib.sh /dev/media0"
    echo "cmd 'media-ctl -d /dev/media* -p' can be used to confirm. replace '*' with media node number"
    exit 1
fi

VCARD="/dev/dri/by-path/platform-b0010000.v_mix-card"
if [ ! -e "$VCARD" ]
then
    echo "$VCARD not found."
    echo "Please make sure that the HW accelerator firmware is loaded via xmutil loadapp kv260-defect-detect."
    exit 1
fi

MEDIA_CMD=$(media-ctl -d ${1} -p)

TRIM_ENTITY_12=${MEDIA_CMD#*"entity 12"}

TRIM_DEVICE_NODE=${TRIM_ENTITY_12#*"device node name"}

SUBDEV=($TRIM_DEVICE_NODE)

# *********************** GAMMA ************************
# To set the GAMMA value as 1.5 write gamma=0x1800
# To set the GAMMA value as 2 write gamma=0x2000
# To set the GAMMA value as 2.2 write gamma=0x2333
# To set the GAMMA value as 2.5 write gamma=0x2800
v4l2-ctl -d ${SUBDEV} -c gamma=0x1800

# *********************** Exposure  ********************
# To set auto exposure write 'exposure=12'
# For manual mode write 'exposure=0'
v4l2-ctl -d ${SUBDEV} -c exposure=12

# **************************** GAIN ********************
# To set the gain value 5 write 'gain=0x0500'
# To set the gain value 3 write 'gain=0x0300'
v4l2-ctl -d ${SUBDEV} -c gain=0x500

# *********************** Exposure Metering *****************************
# To set AE_Metering Mode as Average write 'exposure_metering=0'
# To set AE_Metering Mode as wide center write 'exposure_metering=0x1'
# To set AE_Metering Mode as narrow center write 'exposure_metering=0x2
# To set AE_Metering Mode as spot write 'exposure_metering=0x3
v4l2-ctl -d ${SUBDEV} -c exposure_metering=0x2

# *********************** Satruation *******************
# To set Saturation as 0 write 'saturation=0'
# To set Saturation as 0.5 write 'saturation=0x800'
# To set Saturation as 1 write 'saturation=0x1000'
# To set Saturation as 1.5 write 'saturation=0x1800'
# To set Saturation as 2 write 'saturation=0x2000'
v4l2-ctl -d ${SUBDEV} -c saturation=0x800

# *********************** Contrast *********************
# To set contrast as -5.0 write 'contrast=0xB000'
# To set contrast as -2.5 write 'contrast=0xD800'
# To set contrast as 0 write 'contrast=0x0000'
# To set contrast as 2.5 write 'contrast=0x2800'
# To set contrast as 5 write 'contrast=0x5000'
v4l2-ctl -d ${SUBDEV} -c contrast=0xB000

# *********************** Brightness *******************
v4l2-ctl -d ${SUBDEV} -c brightness=0xFF
