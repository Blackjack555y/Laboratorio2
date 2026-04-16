#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// ─── Pines de los sensores touch ────────────────────────────
#define TOUCH_PIN_1  T0
#define TOUCH_PIN_2  T3
#define TOUCH_THRESHOLD 40
#define QUEUE_SIZE 10

// ─── Estructura del mensaje en la cola ───────────────────────
typedef struct {
    int       value;
    TickType_t timestamp;
    int       sensorId;
} SensorData_t;

// ─── Parámetros tarea lectora ─────────────────────────────────
typedef struct {
    int               pin;
    int               sensorId;
    QueueHandle_t     queue;

    SemaphoreHandle_t touchMutex;  // ← NUEVO: protege touchRead()

} SensorTaskParams_t;

// ─── Parámetros tarea escritora ───────────────────────────────
typedef struct {
    QueueHandle_t     queue;
    SemaphoreHandle_t mutex;
    int               sensorId;
} SerialTaskParams_t;

// ─── Handles globales ────────────────────────────────────────
QueueHandle_t     xQueue1, xQueue2;
SemaphoreHandle_t xSerialMutex;

SemaphoreHandle_t xTouchMutex;  // ← NUEVO


// ═══════════════════════════════════════════════════════════════
// FUNCIÓN A: lectura del sensor
// ═══════════════════════════════════════════════════════════════
void taskSensorRead(void *pvParameters) {
    SensorTaskParams_t *params = (SensorTaskParams_t *) pvParameters;
    SensorData_t data;

    for (;;) {
        int raw = 0;


        // ← NUEVO: mutex evita condición de carrera en el driver de touch
        if (xSemaphoreTake(params->touchMutex, pdMS_TO_TICKS(100)) == pdPASS) {
            raw = touchRead(params->pin);
            xSemaphoreGive(params->touchMutex);
        }


        data.value     = raw;
        data.timestamp = xTaskGetTickCount();
        data.sensorId  = params->sensorId;

        if (xQueueSend(params->queue, &data, pdMS_TO_TICKS(10)) != pdPASS) {
            // Cola llena: muestra descartada
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ═══════════════════════════════════════════════════════════════
// FUNCIÓN B: envío serial con mutex
// ═══════════════════════════════════════════════════════════════
void taskSerialSend(void *pvParameters) {
    SerialTaskParams_t *params = (SerialTaskParams_t *) pvParameters;
    SensorData_t data;
    char jsonBuf[128];

    for (;;) {
        if (xQueueReceive(params->queue, &data, portMAX_DELAY) == pdPASS) {

            uint32_t ms = data.timestamp * portTICK_PERIOD_MS;

            snprintf(jsonBuf, sizeof(jsonBuf),
                "{\"sensor\":%d,\"value\":%d,\"ts_ms\":%lu}\r\n",
                data.sensorId,
                data.value,
                (unsigned long) ms
            );

            if (xSemaphoreTake(params->mutex, pdMS_TO_TICKS(500)) == pdPASS) {
                Serial.print(jsonBuf);
                xSemaphoreGive(params->mutex);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    while (!Serial) { vTaskDelay(pdMS_TO_TICKS(10)); }

    // ── Crear colas ──────────────────────────────────────────
    xQueue1 = xQueueCreate(QUEUE_SIZE, sizeof(SensorData_t));
    xQueue2 = xQueueCreate(QUEUE_SIZE, sizeof(SensorData_t));

    if (xQueue1 == NULL || xQueue2 == NULL) {
        Serial.println("ERROR: no se pudo crear las colas");
        while (1);
    }

    // ── Crear mutex serial ────────────────────────────────────
    xSerialMutex = xSemaphoreCreateMutex();
    if (xSerialMutex == NULL) {
        Serial.println("ERROR: no se pudo crear xSerialMutex");
        while (1);
    }


    // ── Crear mutex touch ← NUEVO ─────────────────────────────
    xTouchMutex = xSemaphoreCreateMutex();
    if (xTouchMutex == NULL) {
        Serial.println("ERROR: no se pudo crear xTouchMutex");
        while (1);
    }


    // ── Parámetros de las tareas lectoras ← MODIFICADO ───────
    static SensorTaskParams_t sensorParams1 = { TOUCH_PIN_1, 1, NULL, NULL };
    static SensorTaskParams_t sensorParams2 = { TOUCH_PIN_2, 2, NULL, NULL };
    sensorParams1.queue = xQueue1;

    sensorParams1.touchMutex = xTouchMutex;  // ← NUEVO

    sensorParams2.queue = xQueue2;

    sensorParams2.touchMutex = xTouchMutex;  // ← NUEVO


    // ── Parámetros de las tareas escritoras ──────────────────
    static SerialTaskParams_t serialParams1 = { NULL, NULL, 1 };
    static SerialTaskParams_t serialParams2 = { NULL, NULL, 2 };
    serialParams1.queue = xQueue1;  serialParams1.mutex = xSerialMutex;
    serialParams2.queue = xQueue2;  serialParams2.mutex = xSerialMutex;

    // ── Crear las 4 tareas ───────────────────────────────────
    xTaskCreate(taskSensorRead, "SensorRead1", 2048, &sensorParams1, 2, NULL);
    xTaskCreate(taskSensorRead, "SensorRead2", 2048, &sensorParams2, 2, NULL);
    xTaskCreate(taskSerialSend, "SerialSend1", 2048, &serialParams1, 1, NULL);
    xTaskCreate(taskSerialSend, "SerialSend2", 2048, &serialParams2, 1, NULL);

    Serial.println("{\"status\":\"FreeRTOS iniciado\"}");
}

// ═══════════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}