= Descripción del algoritmo secuencial
En el algoritmo secuencial, el problema se resuelve de forma eficiente aprovechando las simetrías del problema.
== Representación de los datos
El tablero se representa con un vector de `MAXSIZE` enteros, de los cuales se utilizan los primeros N. Cada entero representa una fila y se lo lee como una cadena de bits donde un 1 significa que hay una reina en esa posición y un 0 que no la hay.\
Esta representación permite optimizar las operaciones, ya que se utilizan operaciones de bits.

= Estrategia y descripción de las etapas de diseño paralelo
== Descomposición
La estrategia de descomposición más adecuada es la de *descomposición exploratoria*, ya que la estructura del problema no se conoce completamente al inicio y evoluciona durante la ejecución, explorando un espacio de soluciones posibles. La descomposición progresa dinámicamente: a medida que se explora el espacio de búsqueda se generan nuevas tareas, lo que hace que el paralelismo no esté completamente definido desde el comienzo.\
En este caso, se optó por que el proceso con rank 0 realice las primeras iteraciones del algoritmo, creando tareas que luego los otros procesos puedan tomar y distribuir entre sus hilos.
Esto se hace para balancear mejor la carga, ya que cada tablero puede tomar una cantidad de operaciones muy diferente.

Una tarea en este caso sera uno de esos tableros semiprocesados.

= Tiempos de ejecución, métricas y análisis de escalabilidad

= Uso de inteligencia artificial
1 dio una version en un solo archivo
2 dio una version con alocacion dinamica que interferia con dwalltime
3 la solucion tenia un hilo en mpi para que el rank0 compute sin bloquearse, se cambio por una version que usa MPI_probe
