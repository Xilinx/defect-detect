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

#include <ivas/ivaslogs.h>
#include <ivas/ivas_kernel.h>
#include <gst/ivas/gstinferencemeta.h>

#define MAX_SUPPORTED_WIDTH         1280
#define MAX_SUPPORTED_HEIGHT        800

typedef struct _kern_priv
{
    int log_level;
    IVASFrame *tmp_mem1;
    IVASFrame *tmp_mem2;
    IVASFrame *mango_pix;
    IVASFrame *defect_pix;
} PreProcessingKernelPriv;

int32_t xlnx_kernel_start(IVASKernel *handle, int start, IVASFrame *input[MAX_NUM_OBJECT], IVASFrame *output[MAX_NUM_OBJECT]);
int32_t xlnx_kernel_done(IVASKernel *handle);
int32_t xlnx_kernel_init(IVASKernel *handle);
uint32_t xlnx_kernel_deinit(IVASKernel *handle);

uint32_t xlnx_kernel_deinit(IVASKernel *handle)
{
    PreProcessingKernelPriv *kernel_priv;
    kernel_priv = (PreProcessingKernelPriv *)handle->kernel_priv;
    if (kernel_priv->mango_pix)
        ivas_free_buffer (handle, kernel_priv->mango_pix);
    if (kernel_priv->defect_pix)
        ivas_free_buffer (handle, kernel_priv->defect_pix);
    if (kernel_priv->tmp_mem1)
        ivas_free_buffer (handle, kernel_priv->tmp_mem1);
    if (kernel_priv->tmp_mem2)
        ivas_free_buffer (handle, kernel_priv->tmp_mem2);
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
    uint32_t resolution = MAX_SUPPORTED_HEIGHT * MAX_SUPPORTED_WIDTH;
    kernel_priv->mango_pix  = ivas_alloc_buffer (handle, 1*(sizeof(uint32_t)), IVAS_INTERNAL_MEMORY, NULL);
    kernel_priv->defect_pix = ivas_alloc_buffer (handle, 1*(sizeof(uint32_t)), IVAS_INTERNAL_MEMORY, NULL);
    kernel_priv->tmp_mem1   = ivas_alloc_buffer (handle, resolution*(sizeof(uint8_t)), IVAS_INTERNAL_MEMORY, NULL);
    kernel_priv->tmp_mem2   = ivas_alloc_buffer (handle, resolution*(sizeof(uint8_t)), IVAS_INTERNAL_MEMORY, NULL);

    /* parse config */
    val = json_object_get (jconfig, "debug_level");
    if (!val || !json_is_integer (val))
	    kernel_priv->log_level = LOG_LEVEL_WARNING;
    else
	    kernel_priv->log_level = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kernel_priv->log_level, "IVAS PPE: debug_level %d", kernel_priv->log_level);

    handle->kernel_priv = (void *)kernel_priv;
    handle->is_multiprocess = 1;
    return 0;
}

int32_t xlnx_kernel_start(IVASKernel *handle, int start, IVASFrame *input[MAX_NUM_OBJECT], IVASFrame *output[MAX_NUM_OBJECT])
{
    PreProcessingKernelPriv *kernel_priv;
    int ret;
    uint32_t *mango_pixel;
    uint32_t *defect_pixel;
    GstInferenceMeta *infer_meta = NULL;
    IVASFrame *outframe = output[0];

    kernel_priv = (PreProcessingKernelPriv *)handle->kernel_priv;
    ivas_register_write(handle, &(input[0]->paddr[0]), sizeof(uint64_t), 0x10);                /* Input buffer */
    ivas_register_write(handle, &(input[0]->paddr[0]), sizeof(uint64_t), 0x1C);                /* Input buffer */
    ivas_register_write(handle, &(kernel_priv->tmp_mem1->paddr[0]), sizeof(uint64_t), 0x28);   /* temp buffer */
    ivas_register_write(handle, &(kernel_priv->tmp_mem2->paddr[0]), sizeof(uint64_t), 0x34);   /* temp buffer */
    ivas_register_write(handle, &(output[0]->paddr[0]), sizeof(uint64_t), 0x40);               /* Output buffer */
    ivas_register_write(handle, &(input[0]->props.height), sizeof(uint32_t), 0x64);            /* rows */
    ivas_register_write(handle, &(input[0]->props.width), sizeof(uint32_t), 0x6C);             /* columns */
    ivas_register_write(handle, &(kernel_priv->mango_pix->paddr[0]), sizeof(uint64_t), 0x4C);  /* mango pixel */
    ivas_register_write(handle, &(kernel_priv->defect_pix->paddr[0]), sizeof(uint64_t), 0x58); /* defect pixel */

    ret = ivas_kernel_start (handle);
    if (ret < 0) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, kernel_priv->log_level, "Failed to issue execute command");
        return FALSE;
    }

    /* wait for kernel completion */
    ret = ivas_kernel_done (handle, 1000);
    if (ret < 0) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, kernel_priv->log_level, "Failed to receive response from kernel");
        return FALSE;
    }

    mango_pixel =  kernel_priv->mango_pix->vaddr[0];
    defect_pixel =  kernel_priv->defect_pix->vaddr[0];

    infer_meta = (GstInferenceMeta *) gst_buffer_add_meta ((GstBuffer *) outframe->app_priv,
                                                      gst_inference_meta_get_info (), NULL);
    if (infer_meta == NULL) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, kernel_priv->log_level, "ivas meta data is not available");
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

int32_t xlnx_kernel_done(IVASKernel *handle)
{
    /* dummy */
    return 0;
}
