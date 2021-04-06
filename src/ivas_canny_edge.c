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

#define DEFAULT_MIN_THR     	40
#define DEFAULT_MAX_THR	        255

typedef struct _kern_priv
{
    int thr1;
    int thr2;
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

    val = json_object_get (jconfig, "thr1");
    if (!val || !json_is_integer (val))
	    kernel_priv->thr1 = DEFAULT_MIN_THR;
    else
	    kernel_priv->thr1 = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kernel_priv->log_level, "IVAS PPE: thr1 %d", kernel_priv->thr1);

    val = json_object_get (jconfig, "thr2");
    if (!val || !json_is_integer (val))
	    kernel_priv->thr2 = DEFAULT_MAX_THR;
    else
	    kernel_priv->thr2 = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kernel_priv->log_level, "IVAS PPE: thr2 %d", kernel_priv->thr2);
    handle->kernel_priv = (void *)kernel_priv;
    handle->is_multiprocess = 1;
    return 0;
}

int32_t xlnx_kernel_start(IVASKernel *handle, int start, IVASFrame *input[MAX_NUM_OBJECT], IVASFrame *output[MAX_NUM_OBJECT])
{
    PreProcessingKernelPriv *kernel_priv;
    int ret;
    kernel_priv = (PreProcessingKernelPriv *)handle->kernel_priv;

    ivas_register_write(handle, &(input[0]->paddr[0]), sizeof(uint64_t),     0x10);      /* Input buffer */
    ivas_register_write(handle, &(input[0]->props.height), sizeof(uint32_t), 0x28);      /* In Y8 rows */
    ivas_register_write(handle, &(input[0]->props.width), sizeof(uint32_t),  0x30);      /* In Y8 columns */
    ivas_register_write(handle, &(kernel_priv->thr1), sizeof(uint32_t),      0x38);      /* low threashold */
    ivas_register_write(handle, &(kernel_priv->thr2), sizeof(uint32_t),      0x40);      /* high threashold */
    ivas_register_write(handle, &(output[0]->paddr[0]), sizeof(uint64_t),    0x1C);      /* Output buffer */

    ret = ivas_kernel_start (handle);
    if (ret < 0) {
         LOG_MESSAGE (LOG_LEVEL_ERROR, kernel_priv->log_level, "Failed to issue execute command");
        return 0;
    }

    /* wait for kernel completion */
    ret = ivas_kernel_done (handle, 1000);
    if (ret < 0) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, kernel_priv->log_level, "Failed to receive response from kernel");
        return 0;
    }

    return TRUE;
}

int32_t xlnx_kernel_done(IVASKernel *handle)
{
    /* dummy */
    return 0;
}
