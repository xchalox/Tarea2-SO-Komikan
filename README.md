# Tarea 2 – Torneo de Komikan Concurrente

## Sistemas Operativos

### Integrantes
- Gonzalo Letelier Muñoz
- Camila Celis Silva

## 1. Descripción del proyecto

En esta tarea desarrollamos una simulación concurrente del juego Komikan utilizando C++ y la biblioteca pthread.

La idea principal fue implementar un torneo donde llegan varios jugadores, se forman parejas y se ejecutan partidas de manera simultánea, respetando un límite máximo de mesas disponibles.

Dentro de cada partida, el Puma y los Perros se controlan mediante hilos que realizan movimientos automáticos. También se incluye un árbitro encargado de revisar cuándo se cumple alguna condición de victoria.

Para evitar problemas al trabajar con varios hilos al mismo tiempo, utilizamos mutexes, semáforos y variables de condición.

## 2. Requisitos

Para compilar y ejecutar el programa se necesita:

- Ubuntu o un sistema Linux compatible.
- g++ con soporte para C++17.
- Biblioteca pthread.

## 3. Compilación

Desde la terminal, dentro de la carpeta del proyecto, se ejecuta:

```bash
g++ -Wall -Wextra -std=c++17 -pthread main.cpp -o torneo
```

## 4. Ejecución

Para iniciar el torneo se utiliza:

```bash
./torneo 2 1 config.txt
```

Donde:

- `2` corresponde a la cantidad de jugadores.
- `1` corresponde al máximo de partidas simultáneas.
- `config.txt` contiene la configuración inicial del tablero y los tiempos.

También se pueden ejecutar torneos con más jugadores. Por ejemplo:

```bash
./torneo 20 2 config.txt
```

En este caso participan 20 jugadores y pueden desarrollarse hasta dos partidas al mismo tiempo.

## 5. Funcionamiento

El programa simula la llegada de jugadores y los mantiene en espera hasta que se pueda formar una pareja.

Cuando hay dos jugadores disponibles, se crea una partida y se asignan los roles de Puma y Perros.

Los movimientos se realizan automáticamente y el estado del tablero se comparte entre los hilos de la partida. Para evitar que dos fichas modifiquen el tablero al mismo tiempo, se protege el acceso utilizando mutexes.

El árbitro revisa el desarrollo de la partida y determina cuándo termina. También se consideran situaciones como el abandono, fallos simulados e interrupciones mediante Ctrl+C.

Al finalizar el torneo se muestra un resumen con la cantidad de partidas creadas, completadas, canceladas y con errores.

## 6. Pruebas realizadas

Durante el desarrollo realizamos distintas pruebas para comprobar el funcionamiento del programa.

Primero revisamos las reglas de movimiento y captura, incluyendo casos especiales del tablero. Después probamos partidas normales, abandono de jugadores y fallos simulados.

También ejecutamos una prueba con 20 jugadores y un máximo de dos partidas simultáneas. En esa ejecución se completaron las 10 partidas y no se registraron errores del torneo.

Para revisar la memoria utilizamos Valgrind Memcheck, que no detectó errores ni memoria pendiente de liberar. Además, utilizamos Helgrind y ThreadSanitizer para analizar posibles problemas de sincronización.

Helgrind mostró una advertencia durante la creación de hilos, mientras que ThreadSanitizer no reportó condiciones de carrera en la ejecución realizada.

Los registros de estas pruebas se encuentran en la carpeta `pruebas`.

## 7. Archivos del proyecto

- `main.cpp`: contiene la implementación del torneo.
- `config.txt`: permite configurar el tablero y los tiempos.
- `pruebas/`: contiene los resultados de las pruebas realizadas.
- `README.md`: explica el funcionamiento y la ejecución del proyecto.

## 8. Conclusión

Con esta tarea pudimos aplicar los contenidos de concurrencia y sincronización vistos en Sistemas Operativos.

Una de las partes más importantes fue coordinar los distintos hilos para que pudieran trabajar sobre un mismo tablero sin generar conflictos. También fue necesario controlar la cantidad de partidas simultáneas y asegurar que los recursos se liberaran al terminar.

Las pruebas realizadas nos permitieron comprobar el funcionamiento del torneo en distintos escenarios y detectar posibles problemas durante el desarrollo.
