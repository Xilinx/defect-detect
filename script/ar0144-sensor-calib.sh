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

v4l2-ctl -d /dev/v4l-subdev${subdev} -c scene_mode=0
