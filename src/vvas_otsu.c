/*
 * Copyright 2021-2022 Xilinx, Inc.
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

#include <vvas/vvaslogs.h>
#include <vvas/vvas_kernel.h>
#include <gst/vvas/gstinferencemeta.h>

typedef struct _kern_priv
{
    int log_level;
    VVASFrame *mem;
} PreProcessingKernelPriv;

int32_t  xlnx_kernel_start(VVASKernel *handle, int start, VVASFrame *input[MAX_NUM_OBJECT], VVASFrame *output[MAX_NUM_OBJECT]);
int32_t  xlnx_kernel_done(VVASKernel *handle);
int32_t  xlnx_kernel_init(VVASKernel *handle);
uint32_t xlnx_kernel_deinit(VVASKernel *handle);

uint32_t xlnx_kernel_deinit(VVASKernel *handle)
{
    PreProcessingKernelPriv *kernel_priv;
    kernel_priv = (PreProcessingKernelPriv *)handle->kernel_priv;
    if (kernel_priv->mem)
        vvas_free_buffer (handle, kernel_priv->mem);
    free(kernel_priv);
    return 0;
}

int32_t xlnx_kernel_init(VVASKernel *handle)
{
    json_t *jconfig = handle->kernel_config;
    json_t *val; /* kernel config from app */
    PreProcessingKernelPriv *kernel_priv;

    kernel_priv = (PreProcessingKernelPriv *)calloc(1, sizeof(PreProcessingKernelPriv));
    if (!kernel_priv) {
        printf("Error: Unable to allocate PPE kernel memory\n");
    }
    kernel_priv->mem = vvas_alloc_buffer (handle, 1*(sizeof(uint32_t)), VVAS_INTERNAL_MEMORY, DEFAULT_MEM_BANK, NULL);


    /* parse config */
    val = json_object_get (jconfig, "debug_level");
    if (!val || !json_is_integer (val))
	    kernel_priv->log_level = LOG_LEVEL_WARNING;
    else
	    kernel_priv->log_level = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kernel_priv->log_level, "VVAS PPE: debug_level %d", kernel_priv->log_level);

    handle->kernel_priv = (void *)kernel_priv;
    handle->is_multiprocess = 1;
    return 0;
}

int32_t xlnx_kernel_start(VVASKernel *handle, int start, VVASFrame *input[MAX_NUM_OBJECT], VVASFrame *output[MAX_NUM_OBJECT])
{
    PreProcessingKernelPriv *kernel_priv;
    int ret;
    uint32_t *thr;
    float sigma = 0.0;
    GstInferenceMeta *infer_meta = NULL;
    VVASFrame *outframe = output[0];
    kernel_priv = (PreProcessingKernelPriv *)handle->kernel_priv;
    ret = vvas_kernel_start (handle, "ppuufp", input[0]->paddr[0], \
                             output[0]->paddr[0], input[0]->props.height, input[0]->props.width, sigma, kernel_priv->mem->paddr[0]);
    if (ret < 0) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, kernel_priv->log_level, "Failed to issue execute command");
        return FALSE;
    }

    /* wait for kernel completion */
    ret = vvas_kernel_done (handle, 1000);
    if (ret < 0) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, kernel_priv->log_level, "Failed to receive response from kernel");
        return FALSE;
    }
    thr =  kernel_priv->mem->vaddr[0]; 
    infer_meta = (GstInferenceMeta *) gst_buffer_add_meta ((GstBuffer *)outframe->app_priv,
                                                     gst_inference_meta_get_info (), NULL);
    if (infer_meta == NULL) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, kernel_priv->log_level, "Meta data is not available");
        return FALSE;
    }

    if (NULL == infer_meta->prediction) {
        LOG_MESSAGE (LOG_LEVEL_INFO, kernel_priv->log_level, "Allocating prediction");
        infer_meta->prediction = gst_inference_prediction_new ();
    } else {
        LOG_MESSAGE (LOG_LEVEL_INFO, kernel_priv->log_level, "Already allocated prediction");
    }

    GstInferencePrediction *predict;
    GstInferenceClassification *a = NULL;
    predict = gst_inference_prediction_new ();

    a = gst_inference_classification_new_full (-1, 0.0, "OTSU THRSHOLD", 0, NULL, NULL, NULL);
    predict->reserved_1 = (void *)thr;
    gst_inference_prediction_append_classification (predict, a);

    gst_inference_prediction_append (infer_meta->prediction, predict);

    return TRUE;
}

int32_t xlnx_kernel_done(VVASKernel *handle)
{
    /* dummy */
    return 0;
}
