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

extern "C"
{
#include <ivas/ivas_kernel.h>
#include <gst/ivas/gstinferencemeta.h>
#include "ivas_text2overlay.hpp"
}

int log_level = LOG_LEVEL_WARNING;

using namespace cv;
using namespace std;

#define MAX_LABEL_LEN 1024

struct overlayframe_info
{
  IVASFrame *inframe;
  Mat lumaImg;
};

struct ivas_xoverlaypriv
{
  float font_size;
  unsigned int font;
  int y_offset;
  int x_offset;
  struct overlayframe_info frameinfo;
};


/* Compose label text based on config json */
bool
get_label_text (GstInferenceClassification * c, ivas_xoverlaypriv * kpriv,
    char *label_string)
{
  if (!c->class_label || !strlen ((char *) c->class_label))
    return false;

  sprintf (label_string, "%s", (char *) c->class_label);
  return true;
}

static gboolean
overlay_node_foreach (GNode * node, gpointer kpriv_ptr)
{
  ivas_xoverlaypriv *kpriv = (ivas_xoverlaypriv *) kpriv_ptr;
  struct overlayframe_info *frameinfo = &(kpriv->frameinfo);
  LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");

  GList *classes;
  GstInferenceClassification *classification;
  GstInferencePrediction *prediction = (GstInferencePrediction *) node->data;
  int ydiff = kpriv->y_offset;
  /* On each children, iterate through the different associated classes */
  for (classes = prediction->classifications;
      classes; classes = g_list_next (classes)) {
    classification = (GstInferenceClassification *) classes->data;
    printf ("saket class_label %s\n", classification->class_label);

    char label_string[MAX_LABEL_LEN];
    bool label_present;
    Size textsize;
    label_present = get_label_text (classification, kpriv, label_string);

    if (label_present) {
      int baseline;
      textsize = getTextSize (label_string, kpriv->font,
          kpriv->font_size, 1, &baseline);
    }

    LOG_MESSAGE (LOG_LEVEL_INFO,
        "RESULT: (prediction node %ld) %s(%d) (%f)",
        prediction->prediction_id,
        label_present ? classification->class_label : NULL,
        classification->class_id, classification->class_prob);

    if (frameinfo->inframe->props.fmt == IVAS_VFMT_Y8) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "Drawing rectangle for GREY8 image");
      if (label_present) {
        printf ("saket label_string %s\n", label_string);
        printf ("saket y_offset %d\n", kpriv->y_offset);
        printf ("saket x_offset %d\n", kpriv->x_offset);
        /* Draw label text on the filled rectanngle */
        cv::putText(frameinfo->lumaImg, label_string, cv::Point(kpriv->x_offset, kpriv->y_offset), kpriv->font, kpriv->font_size,
                    Scalar (255.0, 255.0, 255.0), 1, 1);
        printf ("saket after put text\n");
        kpriv->y_offset += ydiff;
      }
    }
  }
  return FALSE;
}

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
    GstInferenceMeta *infer_meta = NULL;
    char *pstr;

    ivas_xoverlaypriv *kpriv = (ivas_xoverlaypriv *) handle->kernel_priv;
    struct overlayframe_info *frameinfo = &(kpriv->frameinfo);

    frameinfo->inframe = input[0];
    char *lumaBuf = (char *) frameinfo->inframe->vaddr[0];
    infer_meta = ((GstInferenceMeta *) gst_buffer_get_meta ((GstBuffer *)
            frameinfo->inframe->app_priv, gst_inference_meta_api_get_type ()));
    if (infer_meta == NULL) {
      LOG_MESSAGE (LOG_LEVEL_ERROR,
          "ivas meta data is not available for postdpu");
      return false;
    } else {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "ivas_mata ptr %p", infer_meta);
    }

    if (frameinfo->inframe->props.fmt == IVAS_VFMT_Y8) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "Input frame is in GREY8 format\n");
      printf ("saket Input frame is in GREY8 format\n");
      printf ("saket Input input[0]->props.height %d input[0]->props.stride %d \n", input[0]->props.height, input[0]->props.stride);
      frameinfo->lumaImg.create (input[0]->props.height, input[0]->props.stride, CV_8U);
      frameinfo->lumaImg.data = (unsigned char *) lumaBuf;
    } else {
      LOG_MESSAGE (LOG_LEVEL_WARNING, "Unsupported color format\n");
      return 0;
    }

    /* Print the entire prediction tree */
    pstr = gst_inference_prediction_to_string (infer_meta->prediction);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "Prediction tree: \n%s", pstr);
    free (pstr);

    g_node_traverse (infer_meta->prediction->predictions, G_PRE_ORDER,
        G_TRAVERSE_ALL, -1, overlay_node_foreach, kpriv);

    return 0;
  }


  int32_t xlnx_kernel_done (IVASKernel * handle)
  {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");
    return 0;
  }
}
