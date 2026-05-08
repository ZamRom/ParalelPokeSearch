# ParalelPokeSearch
Repository to High Performance Computing project

## Descripción

PokeSearch usa un algoritmo de ponderacion de pokemon que dado un pokemon retorna 6 pokemon similares: 3 pokemon contra los que tiene ventaja y otros 3 contra los que tiene desventaja.

El proposito del projecto es paralelizar este algoritmo pasandole a cada nodo la informacion del pokemon principal y otro para que cada uno haga la ponderacion y la retorne al nodo maestro y este haga el ordenamiento para retornar los 3 mejores y 3 peores.


## Fuente de datos

Los datos originales fueron obtenidos de la pokeAPI y procesados para hacerlo una base de datos (procedimiento en el repositorio de [PokeSearch](https://github.com/ZamRom/PokeSearch)). 

## Procedimiento

Dado que los datos de la fuente del repositorio original estan en un base de datos, primero se tiene que pasar a un tipo de archivo como CSV que pueda ser mas facilmente procesado en C.

Y como el algoritmo es totalmente numerico e independiente entonces el procedimiento se puede paralelizar.

## Contactos

Ariel Rodolfo Zarmudio Romero- zamromxd@gmail.com