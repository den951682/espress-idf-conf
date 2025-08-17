#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/timers.h"


struct DelayedSendContext {
    QueueHandle_t queue;
    const void* cmd;
};

static void DelayedSendCallback(TimerHandle_t xTimer) {
    DelayedSendContext* ctx = static_cast<DelayedSendContext*>(pvTimerGetTimerID(xTimer));
    if (ctx) {
        xQueueSend(ctx->queue, &ctx->cmd, 0);
        delete ctx;  
    }
    xTimerDelete(xTimer, 0); 
}

void sendDelayed(QueueHandle_t queue, const void* cmd, uint32_t delayMs) {
    auto* ctx = new DelayedSendContext{queue, cmd};

    TimerHandle_t timer = xTimerCreate(
        "SendDelayed",
        pdMS_TO_TICKS(delayMs),
        pdFALSE, 
        ctx,     
        DelayedSendCallback
    );

    if (timer != nullptr) {
        xTimerStart(timer, 0);
    } else {
        delete ctx; 
    }
}
