/*
 * Copyright 2021 Xilinx, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <math.h>
#include <ivas/ivas_kernel.h>
#include <gst/ivas/gstinferencemeta.h>
#include "ivas_defectcalc.hpp"

int log_level = LOG_LEVEL_WARNING;

using namespace cv;
using namespace std;

#define MAX_DEFECT_THRESHOLD  0.15

struct overlayframe_info
{
  IVASFrame *inframe;
  IVASFrame *outframe;
  Mat lumaImg;
  Mat lumaOutImg;
};

struct ivas_xoverlaypriv
{
  float font_size;
  unsigned int font;
  unsigned int y_offset;
  unsigned int x_offset;
  unsigned int total_detection;
  unsigned int total_defect;
  struct overlayframe_info frameinfo;
};


extern "C"
{
  int32_t xlnx_kernel_init (IVASKernel * handle)
  {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");

    ivas_xoverlaypriv *kpriv =
        (ivas_xoverlaypriv *) malloc (sizeof (ivas_xoverlaypriv));
      memset (kpriv, 0, sizeof (ivas_xoverlaypriv));

    json_t *jconfig = handle->kernel_config;
    json_t *val;

    /* Initialize config params with default values */
    log_level = LOG_LEVEL_WARNING;
    kpriv->font_size = 0.5;
    kpriv->font = 0;
    kpriv->total_detection = 0;
    kpriv->total_defect = 0;
    kpriv->y_offset = 30;
    kpriv->x_offset = 800;

     val = json_object_get (jconfig, "debug_level");
    if (!val || !json_is_integer (val))
        log_level = LOG_LEVEL_WARNING;
    else
        log_level = json_integer_value (val);

      val = json_object_get (jconfig, "font_size");
    if (!val || !json_is_integer (val))
        kpriv->font_size = 0.5;
    else
        kpriv->font_size = json_integer_value (val);

      val = json_object_get (jconfig, "font");
    if (!val || !json_is_integer (val))
        kpriv->font = 0;
    else
        kpriv->font = json_integer_value (val);

      val = json_object_get (jconfig, "y_offset");
    if (!val || !json_is_integer (val))
        kpriv->y_offset = 0;
    else
        kpriv->y_offset = json_integer_value (val);

      val = json_object_get (jconfig, "x_offset");
    if (!val || !json_is_integer (val))
        kpriv->x_offset = 0;
    else
        kpriv->x_offset = json_integer_value (val);

    handle->kernel_priv = (void *) kpriv;
    return 0;
  }

  uint32_t xlnx_kernel_deinit (IVASKernel * handle)
  {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");
    ivas_xoverlaypriv *kpriv = (ivas_xoverlaypriv *) handle->kernel_priv;

    if (kpriv)
      free (kpriv);

    return 0;
  }

  uint32_t xlnx_kernel_start (IVASKernel * handle, int start,
      IVASFrame * input[MAX_NUM_OBJECT], IVASFrame * output[MAX_NUM_OBJECT])
  {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");

    ivas_xoverlaypriv *kpriv = (ivas_xoverlaypriv *) handle->kernel_priv;
    struct overlayframe_info *frameinfo = &(kpriv->frameinfo);
    frameinfo->inframe = input[0];
    frameinfo->outframe = output[0];

    char *lumaBuf = (char *) frameinfo->inframe->vaddr[0];
    char *lumaOutBuf = (char *) frameinfo->outframe->vaddr[0];
    vector<vector<cv::Point>> contours;

    frameinfo->lumaImg.create (input[0]->props.height, input[0]->props.stride, CV_8U);
    frameinfo->lumaImg.data = (unsigned char *) lumaBuf;
    frameinfo->lumaOutImg.create (input[0]->props.height, input[0]->props.stride, CV_8U);
    frameinfo->lumaOutImg.data = (unsigned char *) lumaOutBuf;

    cv::findContours(frameinfo->lumaImg, contours, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);
    long unsigned int i = 0, largest_contour = 0, second_largest_contour = 0;
    double largest_contour_area = 0, second_largest_contour_area = 0;
    double contour_area  = 0, total_contour_area = 0;

    for(size_t i = 0; i < contours.size(); i++ ) {
        contour_area = cv::contourArea(contours[i]);
        if (largest_contour_area < contour_area) {
            largest_contour = i;
            largest_contour_area = contour_area;
        } else if (second_largest_contour_area < contour_area && second_largest_contour_area < largest_contour_area) {
            second_largest_contour = i;
            second_largest_contour_area = contour_area;
        }
        
        total_contour_area += contour_area;
    }
    frameinfo->lumaOutImg = cv::Mat::zeros(frameinfo->lumaOutImg.size(), CV_8U);
    for(i = 0; i < contours.size(); i++ ) {
        if (i == largest_contour || i == second_largest_contour)
            continue;
        drawContours(frameinfo->lumaOutImg, contours, i , 255, cv::FILLED);
    }
    cv::Mat full_mango_image = cv::Mat::zeros(frameinfo->lumaImg.size(), CV_8U);
    drawContours(full_mango_image, contours, largest_contour, 255, cv::FILLED);
    double full_mango_pixels = cv::countNonZero(full_mango_image);
    double defect_threshold = MAX_DEFECT_THRESHOLD;
    double defect_pixels = cv::countNonZero(frameinfo->lumaOutImg);
    double defect_density = (defect_pixels/full_mango_pixels)*100;
    bool defect_decision = (defect_density > defect_threshold);

    char text_buffer[512];
    int y_point = kpriv->y_offset;
    if (defect_decision) {
        kpriv->total_defect++;
    }
    kpriv->total_detection++;

    sprintf(text_buffer, "Defect Density: %.2lf %%", defect_density);
    /* Draw label text on the filled rectanngle */
    putText(frameinfo->lumaOutImg, text_buffer, cv::Point(kpriv->x_offset, y_point), kpriv->font, kpriv->font_size,
            Scalar (255.0, 255.0, 255.0), 1, 1);
    y_point += 30;

    sprintf(text_buffer, "Mango Defected: %s", defect_decision ? "Yes": "No");
    /* Draw label text on the filled rectanngle */
    putText(frameinfo->lumaOutImg, text_buffer, cv::Point(kpriv->x_offset, y_point), kpriv->font, kpriv->font_size,
            Scalar (255.0, 255.0, 255.0), 1, 1);
    y_point += 30;

    sprintf(text_buffer, "Number of defects over total detection: %.2lf", ((double)kpriv->total_defect/kpriv->total_detection) * 100.0);
    /* Draw label text on the filled rectanngle */
    putText(frameinfo->lumaOutImg, text_buffer, cv::Point(kpriv->x_offset, y_point), kpriv->font, kpriv->font_size,
            Scalar (255.0, 255.0, 255.0), 1, 1);
    y_point += 30;

    return 0;
  }


  int32_t xlnx_kernel_done (IVASKernel * handle)
  {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");
    return 0;
  }
}
