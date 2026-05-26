/*
 * pokemon_mpi.c
 *
 * Compilar:
 *   mpicc -O2 -o pokemon_mpi pokemon_mpi.c -lm
 *
 * Ejecutar (ej. 4 procesos, 100 iteraciones):
 *   mpirun -np 4 ./pokemon_mpi pokemon.csv 100
 *
 * Cada proceso recibe un subconjunto de índices aleatorios y ejecuta
 * pokeSearch() de forma independiente. El rank 0 recolecta y reporta.
 *
 * Formato esperado del CSV (sin header, o con header ignorado):
 *   nombre, tipo1_idx, tipo2_idx, atk, spatk, def, spdef, velocidad,
 *   mov0_tipo, mov0_poder, mov1_tipo, mov1_poder, ..., mov8_poder   (27 cols de movs)
 *   total_stats
 *
 * Ajusta NUM_MOVES y el orden de columnas si tu CSV difiere.
 *
 * Columnas asumidas (0-indexed):
 *  0  : nombre
 *  1  : tipo1   (0-17)
 *  2  : tipo2   (-1 si no tiene segundo tipo)
 *  3  : atk
 *  4  : spatk
 *  5  : def
 *  6  : spdef
 *  7  : total_stats (para el filtro ±20)
 *  8..35 : 9 movimientos × (tipo, poder) — ajusta NUM_MOVES si son distintos
 *            en el código Python se itera ini=8, c in range(3), t in range(18) →
 *            son 3 grupos × 18 valores, es decir 54 columnas de movs
 *  Nota: el python usa `ini` recorriendo 3*18=54 slots, cada slot es un "poder"
 *        asociado a un tipo t. Se interpreta como: 3 categorías (física, especial, ?)
 *        × 18 tipos posibles de movimiento, con el poder almacenado directamente.
 */

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ─── Constantes ─────────────────────────────────────────────── */
#define MAX_POKEMON   1100
#define NAME_LEN      64
#define NUM_CATS      3       /* categorías de ataque (física, especial, otra) */
#define NUM_TYPES     18      /* tipos de Pokémon */
#define MOV_SLOTS     (NUM_CATS * NUM_TYPES)  /* 54 slots de movimiento */
#define STAT_WINDOW   20      /* filtro ±20 en total_stats                    */
#define TOP_N         3       /* mejores/peores matchups a reportar            */

/* ─── Tabla de tipos (atacante × defensor) ───────────────────── */
static const float TT[NUM_TYPES][NUM_TYPES] = {
    {1  , 2  , 1  , 1  , 1  , 1  , 1  , 0  , 1  , 1  , 1  , 1  , 1  , 1  , 1  , 1  , 1  , 1  },
    {1  , 1  , 2  , 1  , 1  , 0.5, 0.5, 1  , 1  , 1  , 1  , 1  , 1  , 2  , 1  , 1  , 0.5, 2  },
    {1  , 0.5, 1  , 1  , 0  , 2  , 0.5, 1  , 1  , 1  , 1  , 0.5, 2  , 1  , 2  , 1  , 1  , 1  },
    {1  , 0.5, 1  , 0.5, 2  , 1  , 0.5, 1  , 1  , 1  , 1  , 0.5, 1  , 2  , 1  , 1  , 1  , 0.5},
    {1  , 1  , 1  , 0.5, 1  , 0.5, 1  , 1  , 1  , 1  , 2  , 2  , 0  , 1  , 2  , 1  , 1  , 1  },
    {0.5, 2  , 0.5, 0.5, 2  , 1  , 1  , 1  , 2  , 0.5, 2  , 2  , 1  , 1  , 1  , 1  , 1  , 1  },
    {1  , 0.5, 2  , 1  , 0.5, 2  , 1  , 1  , 1  , 2  , 1  , 0.5, 1  , 1  , 1  , 1  , 1  , 1  },
    {0  , 0  , 1  , 0.5, 1  , 1  , 0.5, 2  , 1  , 1  , 1  , 1  , 1  , 1  , 1  , 1  , 2  , 1  },
    {0.5, 2  , 0.5, 0  , 2  , 0.5, 0.5, 1  , 0.5, 2  , 1  , 0.5, 1  , 0.5, 0.5, 0.5, 1  , 0.5},
    {1  , 1  , 1  , 1  , 2  , 2  , 0.5, 1  , 0.5, 0.5, 2  , 0.5, 1  , 1  , 0.5, 1  , 1  , 0.5},
    {1  , 1  , 1  , 1  , 1  , 1  , 1  , 1  , 0.5, 0.5, 0.5, 2  , 2  , 1  , 0.5, 1  , 1  , 1  },
    {1  , 1  , 2  , 2  , 0.5, 1  , 2  , 1  , 1  , 2  , 0.5, 0.5, 0.5, 1  , 2  , 1  , 1  , 1  },
    {1  , 1  , 0.5, 1  , 2  , 1  , 1  , 1  , 0.5, 1  , 1  , 1  , 0.5, 1  , 1  , 1  , 1  , 1  },
    {1  , 0.5, 1  , 1  , 1  , 1  , 2  , 2  , 1  , 1  , 1  , 1  , 1  , 0.5, 1  , 1  , 2  , 1  },
    {1  , 2  , 1  , 1  , 1  , 2  , 1  , 1  , 2  , 2  , 1  , 1  , 1  , 1  , 0.5, 1  , 1  , 1  },
    {1  , 1  , 1  , 1  , 1  , 1  , 1  , 1  , 1  , 0.5, 0.5, 0.5, 0.5, 1  , 2  , 2  , 1  , 2  },
    {1  , 2  , 1  , 1  , 1  , 1  , 2  , 0.5, 1  , 1  , 1  , 1  , 1  , 0  , 1  , 1  , 0.5, 2  },
    {1  , 0.5, 1  , 2  , 1  , 1  , 0.5, 1  , 2  , 1  , 1  , 1  , 1  , 1  , 1  , 0  , 0.5, 1  }
};

/* ─── Estructura de datos ────────────────────────────────────── */
typedef struct {
    char  name[NAME_LEN];
    int   type1;          /* 0-17 */
    int   type2;          /* 0-17, o -1 */
    int   atk;
    int   spatk;
    int   def;
    int   spdef;
    int   speed;
    int   total;
    float moves[MOV_SLOTS]; /* 3 categorías × 18 tipos */
} Pokemon;

static Pokemon pokedex[MAX_POKEMON];
static int     pokedex_size = 0;

/* ─── Carga del CSV ──────────────────────────────────────────── */
/*
 * Asume que la primera línea es header y la salta.
 * Cada línea: nombre,tipo1,tipo2,atk,spatk,def,spdef,total,m0..m53
 * (54 valores de movimiento)
 */
static int load_csv(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen"); return -1; }

    char line[4096];
    /* saltar header */
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }

    int count = 0;
    while (fgets(line, sizeof(line), f) && count < MAX_POKEMON) {
        Pokemon *pk = &pokedex[count];
        char *tok;

        /* nombre */
        tok = strtok(line, ",");
        if (!tok) continue;
        strncpy(pk->name, tok, NAME_LEN - 1);
        pk->name[NAME_LEN - 1] = '\0';

        /* tipo1 */
        tok = strtok(NULL, ","); if (!tok) continue;
        pk->type1 = atoi(tok);

        /* tipo2 */
        tok = strtok(NULL, ","); if (!tok) continue;
        pk->type2 = atoi(tok);

        /* stats */
        tok = strtok(NULL, ","); if (!tok) continue; pk->atk   = atoi(tok);
        tok = strtok(NULL, ","); if (!tok) continue; pk->spatk = atoi(tok);
        tok = strtok(NULL, ","); if (!tok) continue; pk->def   = atoi(tok);
        tok = strtok(NULL, ","); if (!tok) continue; pk->spdef = atoi(tok);
        tok = strtok(NULL, ","); if (!tok) continue; pk->speed = atoi(tok);
        tok = strtok(NULL, ","); if (!tok) continue; pk->total = atoi(tok);

        /* movimientos: 54 valores */
        for (int i = 0; i < MOV_SLOTS; i++) {
            tok = strtok(NULL, ",\n\r");
            pk->moves[i] = tok ? (float)atof(tok) : 0.0f;
        }
        count++;
    }
    fclose(f);
    return count;
}

/* ─── Lógica del algoritmo (traducción directa del Python) ───── */

/* Determina la categoría prioritaria basándose en stat físico vs especial.
 * Retorna 0 (física), 2 (especial) o -1 (empate). */
static inline int pri_category(int stat_phys, int stat_spec) {
    if (stat_phys > stat_spec) return 0;
    if (stat_phys == stat_spec) return -1;
    return 2;
}

static float puntaje(const Pokemon *M, int m_atk, int o_def, const Pokemon *O) {
    float pt = 0.0f;
    int   ini = 0; /* índice en M->moves */

    for (int c = 0; c < NUM_CATS; c++) {
        for (int t = 0; t < NUM_TYPES; t++) {
            float p  = M->moves[ini];
            float pp = p;

            if (c == m_atk) p += pp / 4.0f;
            if (c == o_def) p -= pp / 4.0f;
            if (t == M->type1 || t == M->type2) p += pp / 2.0f;

            /* efectividad de tipo del atacante t contra los tipos del defensor */
            p *= TT[O->type1][t];
            if (O->type2 != -1) p *= TT[O->type2][t];

            ini++;
            pt += p;
        }
    }
    return pt;
}

static float calcular(const Pokemon *M, const Pokemon *O) {
    int pri_atk   = pri_category(M->atk,  M->spatk);
    int pri_def   = pri_category(M->def,  M->spdef);
    int other_atk = pri_category(O->atk,  O->spatk);
    int other_def = pri_category(O->def,  O->spdef);

    float ptM = puntaje(M, pri_atk,   other_def, O);
    float ptO = puntaje(O, other_atk, pri_def,   M);

    return ptM - ptO + (float)(M->speed - O->speed);
}

/* Comparadores para qsort */
typedef struct { int idx; float score; } Pair;

static int cmp_asc(const void *a, const void *b) {
    float d = ((Pair*)a)->score - ((Pair*)b)->score;
    return (d < 0) ? -1 : (d > 0) ? 1 : 0;
}

/*
 * pokeSearch: devuelve los 3 mejores y 3 peores matchups.
 * worst[]: índices de los 3 pokémon más difíciles para pk_idx
 * best[] : índices de los 3 pokémon más fáciles para pk_idx
 */
static void pokeSearch(int pk_idx,
                       int best_out[TOP_N], int worst_out[TOP_N]) {
    const Pokemon *me = &pokedex[pk_idx];
    int ts = me->total;

    /* lista temporal, en el peor caso todos los pokémon */
    Pair *pl = malloc(pokedex_size * sizeof(Pair));
    int   pl_n = 0;

    for (int i = 0; i < pokedex_size; i++) {
        const Pokemon *other = &pokedex[i];
        /*if (other->total < ts - STAT_WINDOW || other->total > ts + STAT_WINDOW)
		continue;*/
        pl[pl_n].idx   = i;
        pl[pl_n].score = calcular(me, other);
        pl_n++;
    }

    qsort(pl, pl_n, sizeof(Pair), cmp_asc);

    /* peores matchups (scores más bajos → el rival nos gana) */
    for (int i = 0; i < TOP_N && i < pl_n; i++)
        worst_out[i] = pl[i].idx;

    /* mejores matchups (scores más altos → nosotros ganamos) */
    for (int i = 0; i < TOP_N && i < pl_n; i++)
        best_out[i] = pl[pl_n - 1 - i].idx;

    free(pl);
}

/* ─── Main con MPI ───────────────────────────────────────────── */
/*
 * run_corrida: ejecuta una corrida de iter_count iteraciones y devuelve
 * el tiempo máximo entre todos los procesos. Solo rank 0 recibe el valor real.
 */
static double run_corrida(int iter_count, int nprocs, int rank) {

    /* Construimos sendcounts y displs */
    int *sendcounts = malloc(nprocs * sizeof(int));
    int *displs     = malloc(nprocs * sizeof(int));
    int base  = iter_count / nprocs;
    int extra = iter_count % nprocs;
    int offset = 0;
    for (int r = 0; r < nprocs; r++) {
        sendcounts[r] = base + (r < extra ? 1 : 0);
        displs[r]     = offset;
        offset       += sendcounts[r];
    }

    /* Rank 0 genera índices aleatorios */
    int *all_indices = NULL;
    if (rank == 0) {
        all_indices = malloc(iter_count * sizeof(int));
        for (int i = 0; i < iter_count; i++)
            all_indices[i] = rand() % pokedex_size;
    }

    /* Distribuir índices */
    int my_count = sendcounts[rank];
    int *my_indices = malloc(my_count * sizeof(int));
    MPI_Scatterv(all_indices, sendcounts, displs, MPI_INT,
                 my_indices,  my_count,            MPI_INT,
                 0, MPI_COMM_WORLD);

    /* Cómputo */
    double t_start = MPI_Wtime();
    for (int i = 0; i < my_count; i++) {
        int best[TOP_N], worst[TOP_N];
        pokeSearch(my_indices[i], best, worst);
    }
    double t_local = MPI_Wtime() - t_start;

    /* Reducir a rank 0 */
    double t_max, t_min, t_sum;
    MPI_Reduce(&t_local, &t_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_local, &t_min, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_local, &t_sum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    /* Rank 0 imprime y escribe en CSV */
    if (rank == 0) {
        double t_avg = t_sum / nprocs;
        printf("  t_max=%.4f  t_min=%.4f  t_avg=%.4f\n", t_max, t_min, t_avg);
        /* devolver t_max para que el caller lo guarde */
        free(all_indices);
        free(my_indices);
        free(sendcounts);
        free(displs);
        return t_max;   /* valor usado por el caller para el CSV */
    }

    free(my_indices);
    free(sendcounts);
    free(displs);
    return 0.0;  /* no usado en ranks > 0 */
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    /*
     * Argumentos: csv_path  iteraciones_por_corrida  num_corridas  output.csv
     *
     * Ejemplo:
     *   mpirun -np 8 ./pokemon_mpi pokemon.csv 10000 5 metricas.csv
     */
    if (argc < 5) {
        if (rank == 0)
            fprintf(stderr,
                "Uso: %s <pokemon.csv> <iter_por_corrida> <num_corridas> <output.csv>\n",
                argv[0]);
        MPI_Finalize();
        return 1;
    }

    const char *csv_path    = argv[1];
    int         iter_count  = atoi(argv[2]);
    int         num_corridas= atoi(argv[3]);
    const char *out_path    = argv[4];

    /* Todos los procesos cargan el CSV */
    pokedex_size = load_csv(csv_path);
    if (pokedex_size <= 0) {
        fprintf(stderr, "[rank %d] Error cargando CSV\n", rank);
        MPI_Finalize();
        return 1;
    }

    /* Rank 0 prepara el archivo CSV de salida */
    FILE *fout = NULL;
    if (rank == 0) {
        fout = fopen(out_path, "a");
        if (!fout) { perror("fopen output"); MPI_Finalize(); return 1; }
        //fprintf(fout, "corrida,procesos,iteraciones,t_max_s,t_min_s,t_avg_s\n");
        srand((unsigned)time(NULL));
        printf("Corridas: %d  |  Iter/corrida: %d  |  Procesos: %d\n",
               num_corridas, iter_count, nprocs);
    }

    /* ── Loop de corridas ── */
    for (int c = 0; c < num_corridas; c++) {

        /* Sincronizar todos antes de cada corrida */
        MPI_Barrier(MPI_COMM_WORLD);

        if (rank == 0)
            printf("[corrida %d/%d] ", c + 1, num_corridas);

        /* Construimos sendcounts y displs */
        int *sendcounts = malloc(nprocs * sizeof(int));
        int *displs     = malloc(nprocs * sizeof(int));
        int base  = iter_count / nprocs;
        int extra = iter_count % nprocs;
        int offset = 0;
        for (int r = 0; r < nprocs; r++) {
            sendcounts[r] = base + (r < extra ? 1 : 0);
            displs[r]     = offset;
            offset       += sendcounts[r];
        }

        /* Rank 0 genera índices */
        int *all_indices = NULL;
        if (rank == 0) {
            all_indices = malloc(iter_count * sizeof(int));
            for (int i = 0; i < iter_count; i++)
                all_indices[i] = rand() % pokedex_size;
        }

        /* Distribuir */
        int my_count = sendcounts[rank];
        int *my_indices = malloc(my_count * sizeof(int));
        MPI_Scatterv(all_indices, sendcounts, displs, MPI_INT,
                     my_indices,  my_count,            MPI_INT,
                     0, MPI_COMM_WORLD);

        /* Cómputo */
        double t_start = MPI_Wtime();
        for (int i = 0; i < my_count; i++) {
            int best[TOP_N], worst[TOP_N];
            pokeSearch(my_indices[i], best, worst);
        }
        double t_local = MPI_Wtime() - t_start;

        /* Reducir métricas a rank 0 */
        double t_max, t_min, t_sum;
        MPI_Reduce(&t_local, &t_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&t_local, &t_min, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
        MPI_Reduce(&t_local, &t_sum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

        /* Rank 0 escribe y reporta */
	if (rank == 0) {
            double t_avg = t_sum / nprocs;
            printf("t_max=%.4fs  t_min=%.4fs  t_avg=%.4fs\n",
                   t_max, t_min, t_avg);
            fprintf(fout, "%d,%d,%d,%.6f,%.6f,%.6f\n",
                    c + 1, nprocs, iter_count, t_max, t_min, t_avg);
            fflush(fout);
        }

        free(my_indices);
        free(sendcounts);
        free(displs);
        if (rank == 0) free(all_indices);
    }

    if (rank == 0) {
        fclose(fout);
        printf("Métricas guardadas en: %s\n", out_path);
    }

    MPI_Finalize();
    return 0;
}
