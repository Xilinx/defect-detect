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


int log_level;
using namespace cv;
using namespace std;

#define DEFAULT_DEFECT_THRESHOLD  0.15

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
  float defect_threshold;
  unsigned int is_acc_result;
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
    kpriv->total_detection = 0;
    kpriv->total_defect = 0;

    val = json_object_get (jconfig, "debug_level");
    if (!val || !json_is_integer (val))
        log_level = LOG_LEVEL_WARNING;
    else
        log_level = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "log level %u", log_level);

    val = json_object_get (jconfig, "font_size");
    if (!val || !json_is_number (val))
        kpriv->font_size = 0.5;
    else
        kpriv->font_size = json_number_value (val);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "font size %lf", kpriv->font_size);

    val = json_object_get (jconfig, "font");
    if (!val || !json_is_integer (val))
        kpriv->font = 0;
    else
        kpriv->font = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "font type %u", kpriv->font);

    val = json_object_get(jconfig, "defect_threshold");
    if (!val || !json_is_number(val))
        kpriv->defect_threshold = DEFAULT_DEFECT_THRESHOLD;
    else
        kpriv->defect_threshold = json_number_value(val);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "defect threshold %lf", kpriv->defect_threshold);

    val = json_object_get(jconfig, "is_acc_result");
    if (!val || !json_is_integer(val))
        kpriv->is_acc_result = 1;
    else
        kpriv->is_acc_result = json_integer_value(val);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "Is accumulated result %d", kpriv->is_acc_result);

    val = json_object_get (jconfig, "y_offset");
    if (!val || !json_is_integer (val))
        kpriv->y_offset = 30;
    else
        kpriv->y_offset = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "Y Offset %u", kpriv->y_offset);

    val = json_object_get (jconfig, "x_offset");
    if (!val || !json_is_integer (val))
        kpriv->x_offset = 800;
    else
        kpriv->x_offset = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "X Offset %u", kpriv->x_offset);

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
    long unsigned int i = 0, largest_contour_pos = 0, second_largest_contour_pos = 0;
    double largest_contour_area = 0, second_largest_contour_area = 0;
    double contour_area  = 0, total_contour_area = 0;

    for(size_t i = 0; i < contours.size(); i++ ) {
      contour_area = cv::contourArea(contours[i]);
      if (largest_contour_area < contour_area) {
         largest_contour_pos = i;
         largest_contour_area = contour_area;
      } else if (second_largest_contour_area < contour_area && second_largest_contour_area < largest_contour_area) {
        second_largest_contour_pos = i;
        second_largest_contour_area = contour_area;
      }

      total_contour_area += contour_area;
    }
    frameinfo->lumaOutImg = cv::Mat::zeros(frameinfo->lumaOutImg.size(), CV_8U);
    for(i = 0; i < contours.size(); i++ ) {
      if (i == largest_contour_pos || i == second_largest_contour_pos)
        continue;
      drawContours(frameinfo->lumaOutImg, contours, i , 255, cv::FILLED);
    }
    cv::Mat full_mango_image = cv::Mat::zeros(frameinfo->lumaImg.size(), CV_8U);
    drawContours(full_mango_image, contours, largest_contour_pos, 255, cv::FILLED);
    double full_mango_pixels = cv::countNonZero(full_mango_image);
    double defect_pixels = cv::countNonZero(frameinfo->lumaOutImg);
    double defect_density = (defect_pixels/full_mango_pixels)*100;
    bool defect_decision = (defect_density > kpriv->defect_threshold);

    char text_buffer[512];
    int y_point = kpriv->y_offset;
    if (defect_decision) {
      kpriv->total_defect++;
    }
    kpriv->total_detection++;

    LOG_MESSAGE (LOG_LEVEL_DEBUG, "Defect Density: %.2lf %%", defect_density);
    sprintf(text_buffer, "Defect Density: %.2lf %%", defect_density);
    /* Draw label text on the filled rectanngle */
    putText(frameinfo->lumaOutImg, text_buffer, cv::Point(kpriv->x_offset, y_point), kpriv->font, kpriv->font_size,
            Scalar (255.0, 255.0, 255.0), 1, 1);
    y_point += 30;

    LOG_MESSAGE (LOG_LEVEL_DEBUG, "Is Defected: %s", defect_decision ? "Yes": "No");
    sprintf(text_buffer, "Is Defected: %s", defect_decision ? "Yes": "No");
    /* Draw label text on the filled rectanngle */
    putText(frameinfo->lumaOutImg, text_buffer, cv::Point(kpriv->x_offset, y_point), kpriv->font, kpriv->font_size,
            Scalar (255.0, 255.0, 255.0), 1, 1);
    y_point += 30;
    if (kpriv->is_acc_result) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "Accumulated Defects: %u", kpriv->total_defect);
      sprintf(text_buffer, "Accumulated defects: %u", kpriv->total_defect);
      /* Draw label text on the filled rectanngle */
      putText(frameinfo->lumaOutImg, text_buffer, cv::Point(kpriv->x_offset, y_point), kpriv->font, kpriv->font_size,
              Scalar (255.0, 255.0, 255.0), 1, 1);
      y_point += 30;
    }
    return 0;
  }


  int32_t xlnx_kernel_done (IVASKernel * handle)
  {
    return 0;
  }
}
