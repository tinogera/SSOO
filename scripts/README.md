# Scripts de proceso

Cada archivo representa el programa de un proceso. El nombre del archivo es el PID (`{PID}.txt`).
Cada línea es una instrucción. El Kernel Memory lee la línea correspondiente al Program Counter actual.

## Instrucciones disponibles

| Instrucción | Parámetros | Descripción |
|---|---|---|
| `SLEEP` | `tiempo_ms` | IO bloqueante: duerme N milisegundos |
| `STDOUT` | `dir_logica tamanio` | IO bloqueante: escribe N bytes desde memoria |
| `STDIN` | `dir_logica tamanio` | IO bloqueante: lee N bytes a memoria |
| `MUTEX_CREATE` | `nombre` | Crea un mutex con el nombre dado |
| `MUTEX_LOCK` | `nombre` | Toma el mutex (bloquea si está tomado) |
| `MUTEX_UNLOCK` | `nombre` | Libera el mutex |
| `EXIT` | — | Finaliza el proceso |

## Ejemplo: `0.txt`

El archivo `0.txt` es el proceso con PID 0. El KS lo crea al arrancar y le pasa la ruta
del script como argumento. El KM construye la ruta como `{SCRIPTS_BASEPATH}/{PID}.txt`.
