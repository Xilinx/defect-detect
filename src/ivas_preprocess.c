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

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ivas/ivaslogs.h>
#include <ivas/ivas_kernel.h>
#include <gst/ivas/gstivasinpinfer.h>
#include <gst/ivas/gstinferencemeta.h>

#define DEFAULT_THRESHOLD	60
#define DEFAULT_MAX_VALUE	255

typedef struct _kern_priv
{
    int threshold;
    int max_value;
    int log_level;
} PreProcessingKernelPriv;

int32_t xlnx_kernel_start(IVASKernel *handle, int start, IVASFrame *input[MAX_NUM_OBJECT], IVASFrame *output[MAX_NUM_OBJECT]);
int32_t xlnx_kernel_done(IVASKernel *handle);
int32_t xlnx_kernel_init(IVASKernel *handle);
uint32_t xlnx_kernel_deinit(IVASKernel *handle);

uint32_t xlnx_kernel_deinit(IVASKernel *handle)
{
    PreProcessingKernelPriv *kernel_priv;
    kernel_priv = (PreProcessingKernelPriv *)handle->kernel_priv;
    free(kernel_priv);
    return 0;
}

int32_t xlnx_kernel_init(IVASKernel *handle)
{
    json_t *jconfig = handle->kernel_config;
    json_t *val; /* kernel config from app */
    PreProcessingKernelPriv *kernel_priv;

    kernel_priv = (PreProcessingKernelPriv *)calloc(1, sizeof(PreProcessingKernelPriv));
    if (!kernel_priv) {
        printf("Error: Unable to allocate PPE kernel memory\n");
    }

    /* parse config */
    val = json_object_get (jconfig, "debug_level");
    if (!val || !json_is_integer (val))
	    kernel_priv->log_level = LOG_LEVEL_WARNING;
    else
	    kernel_priv->log_level = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kernel_priv->log_level, "IVAS PPE: debug_level %d", kernel_priv->log_level);

    val = json_object_get (jconfig, "threshold");
    if (!val || !json_is_integer (val))
	    kernel_priv->threshold = DEFAULT_THRESHOLD;
    else
	    kernel_priv->threshold = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kernel_priv->log_level, "IVAS PPE: threshold %d", kernel_priv->threshold);

    val = json_object_get (jconfig, "max_value");
    if (!val || !json_is_integer (val))
	    kernel_priv->max_value = DEFAULT_MAX_VALUE;
    else
	    kernel_priv->max_value = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kernel_priv->log_level, "IVAS PPE: max_value %d", kernel_priv->max_value);
    handle->kernel_priv = (void *)kernel_priv;
    handle->is_multiprocess = 1;
    return 0;
}

int32_t xlnx_kernel_start(IVASKernel *handle, int start, IVASFrame *input[MAX_NUM_OBJECT], IVASFrame *output[MAX_NUM_OBJECT])
{
    int uv_h, uv_w;
    PreProcessingKernelPriv *kernel_priv;
    GstInferenceMeta *infer_meta = NULL;
    IVASFrame *outframe = output[0];
    char *pstr;                   /* prediction string */
    int ret;
    kernel_priv = (PreProcessingKernelPriv *)handle->kernel_priv;

    uv_h = (input[0]->props.height);
    uv_w = (input[0]->props.width);
    printf ("saket uv_h: %d and uv_w: %d\n", uv_h, uv_w);
    printf ("saket threashold: %d and maxval: %d\n", kernel_priv->threshold, kernel_priv->max_value);
    printf ("saket start: %d\n", start);
    ivas_register_write(handle, &(input[0]->paddr[0]), sizeof(uint64_t), 0x10);       /* Input buffer */
    ivas_register_write(handle, &(input[0]->props.height), sizeof(uint32_t), 0x38);   /* In Y8 rows */
    ivas_register_write(handle, &(input[0]->props.width), sizeof(uint32_t), 0x40);    /* In Y8 columns */
    ivas_register_write(handle, &(kernel_priv->threshold), sizeof(uint32_t), 0x28);   /* In threashold */
    ivas_register_write(handle, &(kernel_priv->max_value), sizeof(uint32_t), 0x30);   /* In Y8 height */
    ivas_register_write(handle, &(output[0]->paddr[0]), sizeof(uint64_t), 0x1C);      /* Output buffer */

    ret = ivas_kernel_start (handle);
    if (ret < 0) {
        printf("ERROR: IVAS MS: failed to issue execute command");
        return 0;
    }

    /* wait for kernel completion */
    ret = ivas_kernel_done (handle, 1000);
    if (ret < 0) {
        printf("ERROR: IVAS MS: failed to receive response from kernel");
        return 0;
    }

    infer_meta = (GstInferenceMeta *) gst_buffer_add_meta ((GstBuffer *)
        outframe->app_priv, gst_inference_meta_get_info (), NULL);
    if (infer_meta == NULL) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kernel_priv->log_level,
          "ivas meta data is not available for dpu");
      return -1;
    }
    if (NULL == infer_meta->prediction) {
      printf ("saket allocating prediction \n");
      infer_meta->prediction = gst_inference_prediction_new ();
    } else {
      printf ("saket already allocated prediction \n");
    }

    GstInferencePrediction *predict;
    GstInferenceClassification *c = NULL;
    GstInferenceClassification *a = NULL;
    GstInferenceClassification *b = NULL;
    predict = gst_inference_prediction_new ();

    c = gst_inference_classification_new_full (-1, 0.0,
        "DEFECTED", 0, NULL, NULL, NULL);
    a = gst_inference_classification_new_full (-1, 0.0,
        "ACCURACY", 0, NULL, NULL, NULL);
    b = gst_inference_classification_new_full (-1, 0.0,
        "RATE", 0, NULL, NULL, NULL);
    gst_inference_prediction_append_classification (predict, c);
    gst_inference_prediction_append_classification (predict, a);
    gst_inference_prediction_append_classification (predict, b);

    gst_inference_prediction_append (infer_meta->prediction, predict);

    pstr = gst_inference_prediction_to_string (infer_meta->prediction);
    free(pstr);
    return TRUE;
}

int32_t xlnx_kernel_done(IVASKernel *handle)
{
    /* dummy */
    return 0;
}
