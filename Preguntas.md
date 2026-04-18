# FreeRTOS — Respuestas de práctica

---

## Pregunta 1: ¿Cómo se ejecutan tareas de FreeRTOS con la misma función pero distintos parámetros?

En FreeRTOS, `xTaskCreate` acepta un puntero a función y un cuarto argumento llamado `pvParameters` (de tipo `void*`). Ese puntero se pasa a la función de tarea cuando el scheduler la ejecuta. Así, una misma función puede comportarse de forma diferente según los datos que reciba por ese parámetro.

```cpp
// Estructura con los parámetros de cada sensor
typedef struct {
  int pin;
  QueueHandle_t cola;
  const char* nombre;
} SensorParams;

SensorParams paramsA = { 4, colaSensor1, "SensorA" };
SensorParams paramsB = { 5, colaSensor2, "SensorB" };

// Misma función, distintos parámetros
xTaskCreate(tareaLeerSensor, "TareaA", 2048, &paramsA, 1, NULL);
xTaskCreate(tareaLeerSensor, "TareaB", 2048, &paramsB, 1, NULL);
```

El scheduler llama a `tareaLeerSensor(&paramsA)` y `tareaLeerSensor(&paramsB)` de forma independiente.

---

## Pregunta 2: ¿Cuál es el tipo de dato que recibe una tarea de FreeRTOS? ¿Cómo se convierte al tipo específico?

Toda función de tarea en FreeRTOS tiene la firma `void tarea(void* pvParameters)`. El parámetro es siempre un **puntero genérico** `void*`, que no tiene tipo concreto en tiempo de compilación.

Para usarlo, se hace un **cast explícito** al tipo esperado dentro de la función:

```cpp
void tareaLeerSensor(void* pvParameters) {
  // Cast del void* al tipo concreto
  SensorParams* params = (SensorParams*) pvParameters;

  // Ahora se accede a los campos normalmente
  int pin            = params->pin;
  QueueHandle_t cola = params->cola;
  const char* nombre = params->nombre;

  for (;;) {
    int valor = touchRead(pin);
    // ... enviar a la cola
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
```

> **Nota:** El objeto al que apunta el puntero debe existir durante toda la vida de la tarea. Usar variables en el stack de `setup()` es seguro si las tareas corren indefinidamente mientras el programa corre.

---

## Pregunta 3: ¿Qué pasa cuando una cola se llena y una tarea quiere insertar nuevos elementos?

El comportamiento depende del parámetro `xTicksToWait` que se pasa a `xQueueSend`:

```cpp
// Caso 1: sin espera — retorna pdFALSE inmediatamente si la cola está llena
xQueueSend(cola, &dato, 0);

// Caso 2: espera hasta N ms antes de rendirse
xQueueSend(cola, &dato, pdMS_TO_TICKS(50));

// Caso 3: espera indefinidamente (bloquea la tarea)
xQueueSend(cola, &dato, portMAX_DELAY);
```

Cuando la tarea queda bloqueada esperando espacio, el scheduler la suspende y da CPU a otras tareas. Cuando una tarea consumidora extrae un elemento y libera espacio, FreeRTOS despierta a la tarea productora automáticamente.

> **Cuidado:** Usar `portMAX_DELAY` en producción puede causar inanición si el consumidor es lento. Diseña el tamaño de la cola según la diferencia de velocidad entre productor y consumidor.

---

## Pregunta 4: ¿Es posible que varias tareas lean y escriban a la misma cola?

**Sí.** Las colas de FreeRTOS son estructuras thread-safe por diseño. Múltiples tareas pueden llamar a `xQueueSend` y `xQueueReceive` sobre la misma cola sin necesidad de un mutex adicional.

FreeRTOS usa suspensión de interrupciones internamente para proteger el acceso a la estructura de la cola. El patrón más común es:

```cpp
// N productores → 1 cola → M consumidores

// Productor A
xQueueSend(colaCompartida, &datoA, portMAX_DELAY);

// Productor B (otra tarea)
xQueueSend(colaCompartida, &datoB, portMAX_DELAY);

// Consumidor (recibe de quien sea que haya enviado antes)
MiDato recibido;
xQueueReceive(colaCompartida, &recibido, portMAX_DELAY);
```

> **Nota:** Si tienes varios consumidores en una misma cola, cada mensaje solo lo recibirá uno de ellos (el primero que llame a `xQueueReceive`). No es broadcast. Para broadcast usa `xQueuePeek` o considera Event Groups.

---

## Pregunta 5: ¿Qué es un deadlock? Ejemplo de código Arduino/FreeRTOS

Un **deadlock** ocurre cuando dos o más tareas se bloquean mutuamente esperando recursos que la otra tiene tomados, creando un ciclo de espera del que ninguna puede salir.

### Ejemplo con deadlock

```cpp
SemaphoreHandle_t mutexA;
SemaphoreHandle_t mutexB;

// Tarea 1: toma A, luego intenta tomar B
void tarea1(void* p) {
  for (;;) {
    xSemaphoreTake(mutexA, portMAX_DELAY); // Toma A ✓
    vTaskDelay(pdMS_TO_TICKS(10));         // Pausa — Tarea2 toma B
    xSemaphoreTake(mutexB, portMAX_DELAY); // BLOQUEADA: B la tiene Tarea2
    // ... lógica
    xSemaphoreGive(mutexB);
    xSemaphoreGive(mutexA);
  }
}

// Tarea 2: toma B, luego intenta tomar A
void tarea2(void* p) {
  for (;;) {
    xSemaphoreTake(mutexB, portMAX_DELAY); // Toma B ✓
    vTaskDelay(pdMS_TO_TICKS(10));         // Pausa — Tarea1 toma A
    xSemaphoreTake(mutexA, portMAX_DELAY); // BLOQUEADA: A la tiene Tarea1
    // ... lógica
    xSemaphoreGive(mutexA);
    xSemaphoreGive(mutexB);
  }
}
```

**Resultado:** Tarea1 espera B, Tarea2 espera A. Ninguna libera lo que tiene. El sistema se congela permanentemente.

### Diagrama del deadlock

```
  Tarea1 ──tiene──► mutexA
    │                  ▲
  espera             espera
    │                  │
    ▼                  │
  mutexB ◄──tiene── Tarea2
```

### Solución: orden consistente de adquisición

```cpp
// Ambas tareas toman los mutex en el mismo orden: A → B
void tarea1(void* p) {
  for (;;) {
    xSemaphoreTake(mutexA, portMAX_DELAY);
    xSemaphoreTake(mutexB, portMAX_DELAY);
    // ... lógica segura
    xSemaphoreGive(mutexB);
    xSemaphoreGive(mutexA);
  }
}

void tarea2(void* p) {
  for (;;) {
    xSemaphoreTake(mutexA, portMAX_DELAY); // mismo orden que tarea1
    xSemaphoreTake(mutexB, portMAX_DELAY);
    // ... lógica segura
    xSemaphoreGive(mutexB);
    xSemaphoreGive(mutexA);
  }
}
```

> **Otras estrategias:** usar timeout finito en lugar de `portMAX_DELAY` y reintentar; o rediseñar para que cada tarea solo necesite un mutex a la vez.

---

## Pregunta 6: Acceso concurrente sin y con semáforos/mutex

### Sin protección — datos inconsistentes

```cpp
int contadorGlobal = 0;  // variable compartida

void tareaA(void* p) {
  for (;;) {
    // Operación no atómica: read → modify → write
    // El scheduler puede interrumpir entre estas instrucciones
    int tmp = contadorGlobal;  // lee: 5
    // <-- scheduler cambia a TareaB aquí -->
    tmp++;                     // calcula: 6
    contadorGlobal = tmp;      // escribe: 6 (pero TareaB ya puso 6 también)
    vTaskDelay(1);
  }
}

void tareaB(void* p) {
  for (;;) {
    int tmp = contadorGlobal;  // lee: 5 (antes de que A escriba)
    tmp++;                     // calcula: 6
    contadorGlobal = tmp;      // escribe: 6
    // Resultado: dos incrementos → contador debería ser 7, es 6
    vTaskDelay(1);
  }
}
```

**Problema:** Las dos tareas incrementaron el contador pero el valor solo subió en 1. Se perdió una actualización.

### Con mutex — datos consistentes

```cpp
SemaphoreHandle_t mutex;
int contadorGlobal = 0;

void tareaA(void* p) {
  for (;;) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    contadorGlobal++;           // sección crítica protegida
    xSemaphoreGive(mutex);
    vTaskDelay(1);
  }
}

void tareaB(void* p) {
  for (;;) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    contadorGlobal++;           // solo una tarea a la vez
    xSemaphoreGive(mutex);
    vTaskDelay(1);
  }
}

void setup() {
  mutex = xSemaphoreCreateMutex();
  xTaskCreate(tareaA, "A", 1024, NULL, 1, NULL);
  xTaskCreate(tareaB, "B", 1024, NULL, 1, NULL);
  vTaskStartScheduler();
}
```

**Resultado:** El mutex garantiza que solo una tarea ejecuta la sección crítica a la vez. El contador ahora refleja exactamente el número total de incrementos de ambas tareas.
