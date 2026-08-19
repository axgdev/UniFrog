#include <unifrog/task.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

struct sf2000_task_start {
   unifrog_task_entry entry;
   void *arg;
};

static void sf2000_task_trampoline(void *arg)
{
   struct sf2000_task_start start = *(struct sf2000_task_start *)arg;

   vPortFree(arg);
   start.entry(start.arg);
   vTaskDelete(NULL);
}

int unifrog_task_create(unifrog_task_entry entry, void *arg,
   const char *name, enum unifrog_task_priority priority,
   unifrog_task_handle *handle)
{
   struct sf2000_task_start *start;
   TaskHandle_t task = NULL;
   UBaseType_t rtos_priority = priority == UNIFROG_TASK_PRIORITY_HIGH ?
      portPRI_TASK_HIGH : portPRI_TASK_NORMAL;

   if (handle)
      *handle = NULL;
   if (!entry)
      return -1;
   start = pvPortMalloc(sizeof(*start));
   if (!start)
      return -1;
   start->entry = entry;
   start->arg = arg;
   if (xTaskCreate(sf2000_task_trampoline, name ? name : "unifrog",
       configTASK_STACK_DEPTH, start, rtos_priority, &task) != pdPASS) {
      vPortFree(start);
      return -1;
   }
   if (handle)
      *handle = (unifrog_task_handle)task;
   return 0;
}

void unifrog_task_delay_ms(unsigned ms)
{
   TickType_t ticks = ms / portTICK_PERIOD_MS;

   if (ms % portTICK_PERIOD_MS)
      ticks++;
   vTaskDelay(ticks);
}
