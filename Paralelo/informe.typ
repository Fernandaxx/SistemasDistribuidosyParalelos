#let color-titulos = rgb("426A8C")
#let color-fijos = rgb("D0DCE6")
#set heading(numbering: "1.1.")
#set par(justify: true)
#let tabla-carga-UP(celda-1, ..datos) = {
  let celdas-dinamicas = datos.pos()
  let filas-completas = ()
  let valores-col1 = (1, 4, 8, 16)

  for (i, grupo) in celdas-dinamicas.chunks(5).enumerate() {
    let valor-actual = valores-col1.at(i, default: "-")
    filas-completas.push(table.cell(fill: color-fijos)[*#valor-actual*])

    for celda in grupo {
      filas-completas.push(celda)
    }
  }

  table(
    columns: (auto, 1fr, 1fr, 1fr, 1fr, 1fr),
    align: center,
    celda-1,
    table.cell(colspan: 5, fill: color-titulos)[*CARGA (N)*],
    table.cell(fill: color-titulos)[*UP*],
    table.cell(fill: color-fijos)[*14*],
    table.cell(fill: color-fijos)[*15*],
    table.cell(fill: color-fijos)[*16*],
    table.cell(fill: color-fijos)[*17*],
    table.cell(fill: color-fijos)[*18*],

    ..filas-completas,
  )
}

= Descripción del algoritmo secuencial
El algoritmo secuencial implementa una solución para el problema de las N-Reinas utilizando Backtracking y operaciones a nivel de bits para optimizar los cálculos.
== Representación de los elementos
Se representa el tablero utilizando un arreglo de enteros donde cada índice del arreglo representa una fila del tablero y el valor almacenado en cada posición se interpreta como una máscara de bits. En cada número entero, un bit en 1 indica la columna donde se encuentra la reina de esa fila, mientras que los bits en 0 representan las casillas vacías.
== Estrategia para determinar si un tablero es válido
El algoritmo construye las soluciones garantizando que cada nueva reina agregada esté en una posición segura. Para esto, mantiene las variables down, left y right, que se actualizan en cada llamada a las funciones recursivas para tener un registro de las posiciones que están amenazadas por las reinas de las filas anteriores y, por lo tanto, son inválidas.\
Las posiciones válidas para la fila actual se marcan con un 1 en la variable bitmap, la cuál se calcula a partir de las variables down, left y right.\
Además de generar únicamente tableros que cumplen las condiciones del problema de las N-Reinas, también se descartan todos los tableros que sean rotaciones o espejos de soluciones ya encontradas. Esto se logra limitando las columnas iniciales evaluadas en la primera fila (con variables como BOUND1) y utilizando la función Check() para reconocer simetrías.
== Almacenamiento de resultados y contabilización
El arreglo BOARD se sobrescribe continuamente a medida que el algoritmo se ejecuta. Para llevar el registro de los tableros válidos, se utilizan contadores según la simetría de la solución hallada.\
Si una solución puede rotarse 90, 180 y 270 grados, luego espejarse y realizar las mismas rotaciones y que en los ocho casos las soluciones sean diferentes, entonces se suma a COUNT8. Puede suceder que alguna de estas rotaciones resulte en un tablero idéntico al que se tiene, y es en estos casos que se utilizan COUNT4 Y COUNT2, según el nivel de simetría.\
Finalmente, el número total de tableros válidos se calcula multiplicando cada contador por la cantidad de tableros que representa (TOTAL = COUNT8 \* 8 + COUNT4 \* 4 + COUNT2 \* 2), mientras que el total de tableros únicos se obtiene simplemente sumando los contadores (UNIQUE = COUNT8 + COUNT4 + COUNT2).

= Estrategia y descripción de las etapas de diseño paralelo
== Descomposición
La estrategia de descomposición más adecuada es la de *descomposición exploratoria*, ya que la estructura del problema no se conoce completamente al inicio y evoluciona durante la ejecución, explorando un espacio de soluciones posibles. La descomposición progresa dinámicamente: a medida que se explora el espacio de búsqueda se generan nuevas tareas, lo que hace que el paralelismo no esté completamente definido desde el comienzo.\
En este caso, se optó por que el proceso con rank 0 realice las primeras iteraciones del algoritmo, creando tareas que luego los otros procesos puedan tomar y distribuir entre sus hilos.
Esto se hace para balancear mejor la carga, ya que cada tablero puede requerir una cantidad de operaciones muy diferente.

Una tarea en este caso será uno de esos tableros intermedios.
== Comunicación

En este problema, cada tarea representa un subárbol del backtracking y, una vez que recibe su estado inicial, puede ejecutarse de manera independiente. Por eso, la comunicación no ocurre durante el cómputo interno de cada tarea, sino principalmente para distribuir trabajo y devolver resultados parciales.


La comunicación se organiza en dos niveles, siguiendo el modelo híbrido MPI + Pthreads. A nivel inter-nodo se utiliza MPI, mediante una comunicación explícita por pasaje de mensajes, punto a punto, centralizada y dinámica bajo demanda. El rank 0 actúa como master, administra el pool global de tareas y entrega lotes a los ranks trabajadores cuando estos los solicitan. Cuando no quedan tareas, envía un lote vacío como señal de finalización, y al terminar cada rank trabajador devuelve sus contadores parciales.


A nivel intra-nodo se utiliza Pthreads, aprovechando la memoria compartida entre los hilos de un mismo proceso. Los hilos toman tareas desde estructuras compartidas protegidas con mutexes y, en los ranks remotos, se usan variables de condición para avisar la llegada y finalización de lotes. Durante el procesamiento de cada tarea no hay comunicación entre hilos, ya que cada uno trabaja con su propio tablero y sus propios contadores parciales, reduciendo condiciones de carrera, sincronización innecesaria y contención.

== Aglomeración
== Mapeo

= Tiempos de ejecución, métricas y análisis de escalabilidad
== Análisis de tiempos de ejecución
Se obtuvieron los siguientes tiempos de ejecución. Para UP=1 se muestran los $T_s$, resultado de ejecutar el algoritmo secuencial. Para el resto, se muestra $T_p (P)$, resultado de ejecutar el algoritmo paralelo desarrollado con la cantidad de hilos necesaria.
#tabla-carga-UP(
  [*Tiempo de ejecución (s)*],
  // Secuencial
  [0.180799], [1.114150], [7.252414], [51.850495], [364.626777],
  // UP=4
  [0.077322], [0.407687], [2.589023], [17.822687], [129.300311],
  // UP=8
  [0.041199], [0.184199], [1.132308], [7.703234], [55.764344],
  // UP=16
  [0.029512], [0.096900], [0.546728], [3.676072], [26.567920],
)
Para los tamaños de tableros menores, el algoritmo paralelo ya muestra mejoras respecto de la versión secuencial, aunque el beneficio obtenido es relativamente bajo.\
A medida que aumenta el tamaño del problema, el paralelismo se aprovecha mejor. En los casos con 8 y 16 unidades de procesamiento, los tiempos de ejecución disminuyen considerablemente respecto de la versión secuencial.
/*
Número de resultados: 365596 - Soluciones únicas: 45752  
N=14, Totales=365596, Unicas=45752
Número de resultados: 2279184 - Soluciones únicas: 285053  
N=15, Totales=2279184, Unicas=285053
Número de resultados: 14772512 - Soluciones únicas: 1846955  
N=16, Totales=14772512, Unicas=1846955
Número de resultados: 95815104 - Soluciones únicas: 11977939  
N=17, Totales=95815104, Unicas=11977939
Número de resultados: 666090624 - Soluciones únicas: 83263591
N=18, Totales=666090624, Unicas=83263591
*/
== Análisis de speedup
Se calculó la tabla de speedups  a partir de los tiempos de ejecución de la siguiente manera:\
$ S (P) = ( T_s )/( T_p (P) ) $
#tabla-carga-UP(
  [*Speedup*],
  [1], [1], [1], [1], [1],
  // UP=4
  [2.34], [2.73], [2.80], [2.91], [2.82],
  // UP=8
  [4.39], [6.05], [6.40], [6.73], [6.54],
  // UP=16
  [6.13], [11.50], [13.26], [14.11], [13.72],
)

Para todos los escenarios evaluados, el speedup obtenido es mayor que 1, lo que indica que el algoritmo paralelo logra mejorar el rendimiento respecto de la versión secuencial incluso para los tamaños de problema más chicos. Sin embargo, para cargas menores, las mejoras observadas son moderadas y el speedup se encuentra bastante alejado del ideal. Para N=14 se observa que, si bien el tiempo de ejecución mejoraba, no es una mejora significativa ya que el speedup no se acerca al ideal. Esto se debe a que, para tamaños de problema chicos, el overhead de comunicación sigue siendo significativo frente al tiempo de cómputo.\
A medida que aumenta el tamaño del problema, y para una cantidad fija de unidades de procesamiento, el speedup mejora considerablemente y comienza a acercarse al valor ideal. Este comportamiento concuerda con la ley de Gustafson-Barsis. Para una cantidad fija de unidades de procesamiento, al incrementar el tamaño del problema el speedup crece y se aproxima cada vez más al ideal. Esto puede observarse claramente en las ejecuciones con 16 unidades de procesamiento, donde el speedup es cada vez más significativo a medida que aumenta la carga de trabajo.\
Para las cargas analizadas, los límites impuestos por la ley de Amdahl no se perciben. Si observamos cualquier columna de la tabla, no se ve un estancamiento en el valor del speedup. Esto significa que podemos agregar más unidades de procesamiento, ya que todavía podríamos mejorar el tiempo de ejecución.
== Análisis de escalabilidad
Se calculó la tabla de eficiencia a partir de la tabla de speedup, para poder analizar la escalabilidad del algoritmo. Para esto se usó la relación:\
$ E(P) = ( S(P) )/P $
#tabla-carga-UP(
  [*Eficiencia*],
  // Secuencial
  [1], [1], [1], [1], [1],
  // UP=4
  [0.59], [0.68], [0.70], [0.73], [0.70],
  // UP=8
  [0.55], [0.76], [0.80], [0.84], [0.82],
  // UP=16
  [0.38], [0.72], [0.83], [0.88], [0.86],
)
Cuando analizamos N=14 podemos ver que, a pesar de que se obtenían mayor tiempo de ejecución y speedup al aumentar la cantidad de unidades de procesamiento, en realidad se están aprovechando cada vez menos los recursos. La paralelización sí aporta beneficios, pero implica un consumo innecesario de recursos y energía.\
Para el resto de las cargas, se puede analizar escalabilidad fuerte y escalabilidad débil.\
Un programa paralelo es *fuertemente escalable* si la eficiencia se mantiene aproximadamente constante al incrementar el número de unidades de procesamiento sin aumentar el tamaño del problema. Para analizar esta escalabilidad entonces, miramos las columnas de la tabla. En todos los casos para N entre 15 y 18 se puede observar que la eficiencia se mantiene aproximadamente constante, por lo que el algoritmo es fuertemente escalable para estas cargas.\
Un programa paralelo es *débilmente escalable*(o simplemente escalable) si la eficiencia se
mantiene aproximadamente constante al incrementar
simultáneamente el número de unidades de procesamiento y
el tamaño del problema.
= Uso de inteligencia artificial
#table(
  columns: (1fr, 1fr, 1fr),
  align: center,
  table.cell(colspan: 3, fill: color-titulos)[#text(white)[*Herramienta: Gemini*]],
  table.header(
    table.cell(fill: color-fijos)[*PROMPT*],
    table.cell(fill: color-fijos)[*ÉXITO*],
    table.cell(fill: color-fijos)[*OBSERVACIÓN*],
  ),
  [], [Parcial], [Dio una versión en un solo archivo.],
  [],
  [Parcial],
  [Dio una versión con alocación de memoria dinámica que interfería con el registro del tiempo, ya que se contaba esa alocación en el tiempo de ejecución.],
  [],
  [Parcial],
  [La solución tenía un hilo en mpi para que el rank0 compute sin bloquearse, se cambió por una versión que usa MPI_probe.],
  [], [Si], [],
  [], [], [],
)
