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

#define MAX_SUPPORTED_WIDTH         1280
#define MAX_SUPPORTED_HEIGHT        800

typedef struct _kern_priv
{
    int log_level;
    VVASFrame *tmp_mem1;
    VVASFrame *tmp_mem2;
    VVASFrame *mango_pix;
    VVASFrame *defect_pix;
} PreProcessingKernelPriv;

int32_t xlnx_kernel_start(VVASKernel *handle, int start, VVASFrame *input[MAX_NUM_OBJECT], VVASFrame *output[MAX_NUM_OBJECT]);
int32_t xlnx_kernel_done(VVASKernel *handle);
int32_t xlnx_kernel_init(VVASKernel *handle);
uint32_t xlnx_kernel_deinit(VVASKernel *handle);

uint32_t xlnx_kernel_deinit(VVASKernel *handle)
{
    PreProcessingKernelPriv *kernel_priv;
    kernel_priv = (PreProcessingKernelPriv *)handle->kernel_priv;
    if (kernel_priv->mango_pix)
        vvas_free_buffer (handle, kernel_priv->mango_pix);
    if (kernel_priv->defect_pix)
        vvas_free_buffer (handle, kernel_priv->defect_pix);
    if (kernel_priv->tmp_mem1)
        vvas_free_buffer (handle, kernel_priv->tmp_mem1);
    if (kernel_priv->tmp_mem2)
        vvas_free_buffer (handle, kernel_priv->tmp_mem2);
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
    uint32_t resolution = MAX_SUPPORTED_HEIGHT * MAX_SUPPORTED_WIDTH;
    kernel_priv->mango_pix  = vvas_alloc_buffer (handle, 1*(sizeof(uint32_t)), VVAS_INTERNAL_MEMORY, DEFAULT_MEM_BANK, NULL);
    kernel_priv->defect_pix = vvas_alloc_buffer (handle, 1*(sizeof(uint32_t)), VVAS_INTERNAL_MEMORY, DEFAULT_MEM_BANK, NULL);
    kernel_priv->tmp_mem1   = vvas_alloc_buffer (handle, resolution*(sizeof(uint8_t)), VVAS_INTERNAL_MEMORY, DEFAULT_MEM_BANK, NULL);
    kernel_priv->tmp_mem2   = vvas_alloc_buffer (handle, resolution*(sizeof(uint8_t)), VVAS_INTERNAL_MEMORY, DEFAULT_MEM_BANK, NULL);

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
    uint32_t *mango_pixel;
    uint32_t *defect_pixel;
    GstInferenceMeta *infer_meta = NULL;
    VVASFrame *outframe = output[0];

    kernel_priv = (PreProcessingKernelPriv *)handle->kernel_priv;
    ret = vvas_kernel_start (handle, "pppppppuu", input[0]->paddr[0], input[0]->paddr[0], \
                             kernel_priv->tmp_mem1->paddr[0], kernel_priv->tmp_mem2->paddr[0], \
                             output[0]->paddr[0], kernel_priv->mango_pix->paddr[0], kernel_priv->defect_pix->paddr[0], \
                             input[0]->props.height, input[0]->props.width); 
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

    mango_pixel =  kernel_priv->mango_pix->vaddr[0];
    defect_pixel =  kernel_priv->defect_pix->vaddr[0];

    infer_meta = (GstInferenceMeta *) gst_buffer_add_meta ((GstBuffer *) outframe->app_priv,
                                                      gst_inference_meta_get_info (), NULL);
    if (infer_meta == NULL) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, kernel_priv->log_level, "vvas meta data is not available");
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

    a = gst_inference_classification_new_full (-1, 0.0, "DEFECT DENSITY", 0, NULL, NULL, NULL);
    predict->reserved_1 = (void *) mango_pixel;
    predict->reserved_2 = (void *) defect_pixel;
    gst_inference_prediction_append_classification (predict, a);

    gst_inference_prediction_append (infer_meta->prediction, predict);
    return TRUE;
}

int32_t xlnx_kernel_done(VVASKernel *handle)
{
    /* dummy */
    return 0;
}
