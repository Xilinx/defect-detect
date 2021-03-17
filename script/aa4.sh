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
gst-launch-1.0 -v mediasrcbin media-device=/dev/media0 \
! "video/x-raw, width=1280, height=800, format=GRAY8, framerate=60/1" \
! tee name=t_src t_src. ! queue \
! ivas_xfilter kernels-config=/opt/xilinx/share/defectdetection_aa4/pre-process.json \
! tee name=t_pre t_pre. ! queue \
! ivas_xfilter kernels-config=/opt/xilinx/share/defectdetection_aa4/canny-accelarator.json \
! ivas_xfilter kernels-config=/opt/xilinx/share/defectdetection_aa4/edge-tracer.json \
! ivas_xfilter kernels-config=/opt/xilinx/share/defectdetection_aa4/defect-calculation.json  \
! tee ! perf \
! kmssink  bus-id=B0010000.v_mix  plane-id=34 render-rectangle="<1280,800,1280,800>"  t_src. \
! queue ! perf \
! kmssink bus-id=B0010000.v_mix  plane-id=35 render-rectangle="<0,0,1280,800>" async=false t_pre. \
! queue ! perf \
! kmssink bus-id=B0010000.v_mix  plane-id=36 render-rectangle="<2560,0,1280,800>" async=false
