// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "freertos/FreeRTOS.h"
#include "soc/periph_defs.h"
#include "soc/soc_memory_layout.h"
#include "hal/cp_dma_hal.h"
#include "hal/cp_dma_ll.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_async_memcpy_impl.h"

IRAM_ATTR static void async_memcpy_impl_default_isr_handler(void *args)
{
    async_memcpy_impl_t *mcp_impl = (async_memcpy_impl_t *)args;

    portENTER_CRITICAL_ISR(&mcp_impl->hal_lock);
    uint32_t status = cp_dma_hal_get_intr_status(&mcp_impl->hal);
    cp_dma_hal_clear_intr_status(&mcp_impl->hal, status);
    portEXIT_CRITICAL_ISR(&mcp_impl->hal_lock);

    // End-Of-Frame on RX side
    if (status & CP_DMA_LL_EVENT_RX_EOF) {
        async_memcpy_isr_on_rx_done_event(mcp_impl);
    }

    if (mcp_impl->isr_need_yield) {
        mcp_impl->isr_need_yield = false;
        portYIELD_FROM_ISR();
    }
}

esp_err_t async_memcpy_impl_allocate_intr(async_memcpy_impl_t *impl, int int_flags, intr_handle_t *intr)
{
    return esp_intr_alloc(ETS_DMA_COPY_INTR_SOURCE, int_flags, async_memcpy_impl_default_isr_handler, impl, intr);
}

esp_err_t async_memcpy_impl_init(async_memcpy_impl_t *impl, dma_descriptor_t *outlink_base, dma_descriptor_t *inlink_base)
{
    impl->hal_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    cp_dma_hal_config_t config = {
        .inlink_base = inlink_base,
        .outlink_base = outlink_base
    };
    cp_dma_hal_init(&impl->hal, &config);
    return ESP_OK;
}

esp_err_t async_memcpy_impl_deinit(async_memcpy_impl_t *impl)
{
    cp_dma_hal_deinit(&impl->hal);
    return ESP_OK;
}

esp_err_t async_memcpy_impl_start(async_memcpy_impl_t *impl)
{
    cp_dma_hal_start(&impl->hal); // enable DMA and interrupt
    return ESP_OK;
}

esp_err_t async_memcpy_impl_stop(async_memcpy_impl_t *impl)
{
    cp_dma_hal_stop(&impl->hal); // disable DMA and interrupt
    return ESP_OK;
}

esp_err_t async_memcpy_impl_restart(async_memcpy_impl_t *impl)
{
    cp_dma_hal_restart_rx(&impl->hal);
    cp_dma_hal_restart_tx(&impl->hal);
    return ESP_OK;
}

bool async_memcpy_impl_is_buffer_address_valid(async_memcpy_impl_t *impl, void *src, void *dst)
{
    // CP_DMA can only access SRAM
    return esp_ptr_internal(src) && esp_ptr_internal(dst);
}

dma_descriptor_t *async_memcpy_impl_get_rx_suc_eof_descriptor(async_memcpy_impl_t *impl)
{
    return (dma_descriptor_t *)cp_dma_ll_get_rx_eof_descriptor_address(impl->hal.dev);
}
