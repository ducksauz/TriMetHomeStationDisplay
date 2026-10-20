#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "bsp_board.h"
#include "lv_port.h"
#include "esp_event.h"
#include "esp_event_base.h"

#include "indicator_model.h"
#include "indicator_view.h"
#include "trimet_client.h"
#include "trimet_config.h"
#include "trimet_dimmer.h"
#include "trimet_health.h"
#include "trimet_ota.h"
#include "trimet_view.h"

static const char *TAG = "app_main";

#define SENSECAP  "\n\
   _____                      _________    ____         \n\
  / ___/___  ____  ________  / ____/   |  / __ \\       \n\
  \\__ \\/ _ \\/ __ \\/ ___/ _ \\/ /   / /| | / /_/ /   \n\
 ___/ /  __/ / / (__  )  __/ /___/ ___ |/ ____/         \n\
/____/\\___/_/ /_/____/\\___/\\____/_/  |_/_/           \n\
--------------------------------------------------------\n\
 Version: %s %s %s\n\
--------------------------------------------------------\n\
"

ESP_EVENT_DEFINE_BASE(VIEW_EVENT_BASE);
esp_event_loop_handle_t view_event_handle;


void app_main(void)
{
    ESP_LOGI("", SENSECAP, esp_app_get_description()->version, __DATE__, __TIME__);

    if (strlen(CONFIG_TRIMET_TZ) > 0) {
        setenv("TZ", CONFIG_TRIMET_TZ, 1);
        tzset();
    }

    ESP_ERROR_CHECK(bsp_board_init());
    lv_port_init();


    esp_event_loop_args_t view_event_task_args = {
        .queue_size = 10,
        .task_name = "view_event_task",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 10240,
        .task_core_id = tskNO_AFFINITY
    };

    ESP_ERROR_CHECK(esp_event_loop_create(&view_event_task_args, &view_event_handle));


    lv_port_sem_take();
    indicator_view_init();
    trimet_view_init();     /* replaces the template's time screen as the start screen */
    lv_port_sem_give();

    indicator_model_init();

    trimet_config_init();   /* after the model: needs NVS */
    /* After the model: needs the default event loop created by its Wi-Fi init. */
    if (trimet_client_start() != ESP_OK) {
        ESP_LOGE(TAG, "TriMet client not started");
    }
    trimet_dimmer_init();
    trimet_health_init();
    trimet_ota_init();

    static char buffer[128];    /* Make sure buffer is enough for `sprintf` */
    while (1) {
        // sprintf(buffer, "   Biggest /     Free /    Total\n"
        //         "\t  DRAM : [%8d / %8d / %8d]\n"
        //         "\t PSRAM : [%8d / %8d / %8d]",
        //         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        //         heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        //         heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
        //         heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
        //         heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        //         heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
        // ESP_LOGI("MEM", "%s", buffer);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
