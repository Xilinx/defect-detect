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

# Below pipeline is to run the live playback. Only MIPI source is supported.
# media node has to be changed as per media entry.

if [ $# -eq 0 ]
  then
    echo "No arguments supplied"
    echo "Please give a file path in the command line i.e. sudo 01.file-defect-detect-display.sh /home/petalinux/input_video.y8"
    exit 1
fi
file=${1}


if [ -e "$file" ]
then
    echo "Input file: $file found."
else
    echo "Input file: $file Not found."
    echo "Please make sure to give proper file path and name i.e. /home/petalinux/input_video.y8"
    exit 1
fi

VCARD="/dev/dri/by-path/platform-b0010000.v_mix-card"
if [ -e "$VCARD" ]
then
    echo "$VCARD found."
    echo | modetest -D B0010000.v_mix -s 52@40:3840x2160@NV16
else
    echo "$VCARD not found."
    echo "Please make sure that the HW accelerator firmware is loaded via xmutil loadapp kv260-defect-detect."
    exit 1
fi

gst-launch-1.0 -v filesrc location=${file} blocksize=1024000 \
! "video/x-raw, width=1280, height=800, format=GRAY8, framerate=60/1" \
! tee name=t_src t_src. ! queue max-size-buffers=1 \
! ivas_xfilter kernels-config=/opt/xilinx/share/ivas/defect-detect/pre-process.json \
! tee name=t_pre t_pre. ! queue max-size-buffers=1 \
! ivas_xfilter kernels-config=/opt/xilinx/share/ivas/defect-detect/canny-accelarator.json \
! ivas_xfilter kernels-config=/opt/xilinx/share/ivas/defect-detect/edge-tracer.json \
! ivas_xfilter kernels-config=/opt/xilinx/share/ivas/defect-detect/defect-calculation.json \
! perf ! kmssink bus-id=B0010000.v_mix plane-id=34 render-rectangle="<2560,680,1280,800>" t_src. \
! queue max-size-buffers=1 ! perf ! kmssink bus-id=B0010000.v_mix plane-id=35 render-rectangle="<0, 680,1280,800>" async=false t_pre. \
! queue max-size-buffers=1 ! perf ! kmssink bus-id=B0010000.v_mix plane-id=36 render-rectangle="<1280,680,1280,800>" async=false
