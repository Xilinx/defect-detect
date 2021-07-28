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
#include <opencv2/imgproc.hpp>
#include <ivas/ivas_kernel.h>
#include <gst/ivas/gstinferencemeta.h>

int log_level;
using namespace cv;
using namespace std;

#define DEFAULT_DEFECT_THRESHOLD  0.16

enum
{
  LOG_LEVEL_ERROR,
  LOG_LEVEL_WARNING,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG
};

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define LOG_MESSAGE(level, ...) {\
  do {\
    char *str; \
    if (level == LOG_LEVEL_ERROR)\
      str = (char*)"ERROR";\
    else if (level == LOG_LEVEL_WARNING)\
      str = (char*)"WARNING";\
    else if (level == LOG_LEVEL_INFO)\
      str = (char*)"INFO";\
    else if (level == LOG_LEVEL_DEBUG)\
      str = (char*)"DEBUG";\
    if (level <= log_level) {\
      printf("[%s %s:%d] %s: ",__FILENAME__, __func__, __LINE__, str);\
      printf(__VA_ARGS__);\
      printf("\n");\
    }\
  } while (0); \
}


struct overlayframe_info
{
  IVASFrame *inframe;
  Mat lumaImg;
};

struct ivas_xoverlaypriv
{
  float font_size;
  float defect_threshold;
  unsigned int is_acc_result;
  unsigned int font;
  unsigned int y_offset;
  unsigned int x_offset;
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

    char *lumaBuf = (char *) frameinfo->inframe->vaddr[0];

    frameinfo->lumaImg.create (input[0]->props.height, input[0]->props.stride, CV_8U);
    frameinfo->lumaImg.data = (unsigned char *) lumaBuf;
    GstInferenceMeta *infer_meta;
    infer_meta = ((GstInferenceMeta *) gst_buffer_get_meta((GstBuffer *)frameinfo->inframe->app_priv,
                                                                 gst_inference_meta_api_get_type()));
    if (infer_meta == NULL) {
        LOG_MESSAGE(LOG_LEVEL_INFO, "ivas meta data is not available for crop");
        return FALSE;
    }
    uint32_t *mango_pixel, *defect_pixel;
    GstInferencePrediction *root = infer_meta->prediction;
    /* Iterate through the immediate child predictions */
    GSList *tmp = gst_inference_prediction_get_children(root);

    for (GSList *child_predictions = tmp; child_predictions; child_predictions = g_slist_next(child_predictions)) {
        GstInferencePrediction *child = (GstInferencePrediction *)child_predictions->data;
        mango_pixel = (uint32_t *)child->reserved_1;
        defect_pixel = (uint32_t *)child->reserved_2;
    }

    double defect_density = ((double)*defect_pixel / *mango_pixel) * 100.0;
    bool defect_decision = (defect_density > kpriv->defect_threshold);

    char text_buffer[512] = {0,};
    int y_point = kpriv->y_offset;
    if (defect_decision) {
        kpriv->total_defect++;
    }

    LOG_MESSAGE (LOG_LEVEL_DEBUG, "Defect Density: %.2lf %%", defect_density);
    sprintf(text_buffer, "Defect Density: %.2lf %%", defect_density);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "text buffer : %s", text_buffer);
    /* Draw label text on the filled rectanngle */
    putText(frameinfo->lumaImg, text_buffer, cv::Point(kpriv->x_offset, y_point), kpriv->font,
            kpriv->font_size, Scalar (255.0, 255.0, 255.0), 1, 1);
    y_point += 30;

    LOG_MESSAGE (LOG_LEVEL_DEBUG, "Is Defected: %s", defect_decision ? "Yes": "No");
    sprintf(text_buffer, "Is Defected: %s", defect_decision ? "Yes": "No");
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "text buffer : %s", text_buffer);
    /* Draw label text on the filled rectanngle */
    putText(frameinfo->lumaImg, text_buffer, cv::Point(kpriv->x_offset, y_point), kpriv->font,
            kpriv->font_size, Scalar (255.0, 255.0, 255.0), 1, 1);
    y_point += 30;

    if (kpriv->is_acc_result) {
        LOG_MESSAGE (LOG_LEVEL_DEBUG, "Accumulated Defects: %u", kpriv->total_defect);
        sprintf(text_buffer, "Accumulated defects: %u", kpriv->total_defect);
        LOG_MESSAGE (LOG_LEVEL_DEBUG, "text buffer : %s", text_buffer);
         /* Draw label text on the filled rectanngle */
        putText(frameinfo->lumaImg, text_buffer, cv::Point(kpriv->x_offset, y_point), kpriv->font,
                kpriv->font_size, Scalar (255.0, 255.0, 255.0), 1, 1);
    }
    g_slist_free(tmp);

    return 0;
  }


  int32_t xlnx_kernel_done (IVASKernel * handle)
  {
      return 0;
  }
}
