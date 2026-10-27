#include <iostream>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <string>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <ctime>
using namespace std;

int tiempoMin = 50, tiempoMax = 300;
unsigned int semillaGeneral = 1234;
int inicioPumaF = 0, inicioPumaC = 2;
int inicioPerroF[12], inicioPerroC[12];

volatile sig_atomic_t detener = 0;
string modo = "normal";

sem_t mesasLibres;
pthread_mutex_t mutexPantalla = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexCola = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condCola = PTHREAD_COND_INITIALIZER;

vector<int> colaJugadores;
bool llegadasTerminadas = false;

struct Mesa {
    int numero, jugadorPuma, jugadorPerros;
    char tablero[7][5];
    int perroFila[12], perroCol[12];
    int pumaFila, pumaCol;
    int capturas, movimientos, resultado, estado;
    long long actividad[2];
    bool controladorTerminado[2];
    pthread_mutex_t mutex;
};

struct Ficha {
    Mesa* mesa;
    int id;
};

struct Controlador {
    Mesa* mesa;
    int id;
};

struct Jugador {
    int id;
};

vector<Mesa> partidas;
vector<pthread_t> hilosPartidas;
int partidasCreadas = 0;

void recibirSenal(int) {
    detener = 1;
}

long long milisegundos() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

bool posicionValida(int f, int c) {
    if (f < 0 || f >= 7 || c < 0 || c >= 5)
        return false;

    if (f == 0)
        return c == 2;

    if (f == 1)
        return c == 1 || c == 3;

    return true;
}

void posicionesIniciales() {
    int k = 0;

    for (int f = 5; f <= 6; f++) {
        for (int c = 0; c < 5; c++) {
            inicioPerroF[k] = f;
            inicioPerroC[k] = c;
            k++;
        }
    }

    inicioPerroF[10] = 4;
    inicioPerroC[10] = 1;
    inicioPerroF[11] = 4;
    inicioPerroC[11] = 3;
}

bool leerConfiguracion(const char* archivo) {
    ifstream entrada(archivo);

    if (!entrada)
        return false;

    string clave;
    int perros = 0;

    while (entrada >> clave) {
        if (clave == "min_ms")
            entrada >> tiempoMin;
        else if (clave == "max_ms")
            entrada >> tiempoMax;
        else if (clave == "seed")
            entrada >> semillaGeneral;
        else if (clave == "puma")
            entrada >> inicioPumaF >> inicioPumaC;
        else if (clave == "perro") {
            if (perros >= 12)
                return false;

            entrada >> inicioPerroF[perros]
                    >> inicioPerroC[perros];
            perros++;
        }
        else
            return false;

        if (!entrada)
            return false;
    }

    return tiempoMin >= 1 &&
           tiempoMax >= tiempoMin &&
           tiempoMax <= 60000 &&
           (perros == 0 || perros == 12);
}

bool validarInicio() {
    bool ocupadas[7][5] = {};

    if (!posicionValida(inicioPumaF, inicioPumaC))
        return false;

    ocupadas[inicioPumaF][inicioPumaC] = true;

    for (int i = 0; i < 12; i++) {
        int f = inicioPerroF[i];
        int c = inicioPerroC[i];

        if (!posicionValida(f, c) || ocupadas[f][c])
            return false;

        ocupadas[f][c] = true;
    }

    return true;
}

bool conectadas(int f1, int c1, int f2, int c2) {
    if (!posicionValida(f1, c1) ||
        !posicionValida(f2, c2))
        return false;

    if (f1 >= 2 && f2 >= 2)
        return abs(f2-f1) + abs(c2-c1) == 1;

    if ((f1 == 0 && c1 == 2) ||
        (f2 == 0 && c2 == 2)) {
        int f = f1 == 0 ? f2 : f1;
        int c = f1 == 0 ? c2 : c1;

        return (f == 1 && (c == 1 || c == 3)) ||
               (f == 2 && c == 2);
    }

    if (f1 == 1 && f2 == 1)
        return (c1 == 1 && c2 == 3) ||
               (c1 == 3 && c2 == 1);

    if (f1 == 1 && f2 == 2)
        return (c1 == 1 && (c2 == 0 || c2 == 1)) ||
               (c1 == 3 && (c2 == 3 || c2 == 4));

    if (f2 == 1 && f1 == 2)
        return (c2 == 1 && (c1 == 0 || c1 == 1)) ||
               (c2 == 3 && (c1 == 3 || c1 == 4));

    return false;
}

void iniciarMesa(Mesa* m) {
    m->pumaFila = inicioPumaF;
    m->pumaCol = inicioPumaC;
    m->capturas = 0;
    m->movimientos = 0;
    m->resultado = 0;

    for (int f = 0; f < 7; f++) {
        for (int c = 0; c < 5; c++) {
            m->tablero[f][c] =
                posicionValida(f, c) ? '.' : ' ';
        }
    }

    m->tablero[inicioPumaF][inicioPumaC] = 'P';

    for (int i = 0; i < 12; i++) {
        int f = inicioPerroF[i];
        int c = inicioPerroC[i];

        m->perroFila[i] = f;
        m->perroCol[i] = c;
        m->tablero[f][c] = 'D';
    }

    for (int i = 0; i < 2; i++) {
        m->actividad[i] = milisegundos();
        m->controladorTerminado[i] = false;
    }
}

void mostrarTablero(Mesa* m) {
    pthread_mutex_lock(&mutexPantalla);

    cout << "\nPartida " << m->numero << " - tablero\n";

    for (int f = 0; f < 7; f++) {
        for (int c = 0; c < 5; c++) {
            cout << m->tablero[f][c] << "   ";
        }
        cout << endl;
    }

    cout << "Capturas: " << m->capturas
         << " | Movimientos: " << m->movimientos << endl;

    pthread_mutex_unlock(&mutexPantalla);
}

bool encontrarPerro(Mesa* m, int f1, int c1,
                  int f2, int c2, int& fp, int& cp) {
    if (!posicionValida(f1, c1) ||
        !posicionValida(f2, c2) ||
        m->tablero[f2][c2] != '.')
        return false;

    int df = f2 - f1;
    int dc = c2 - c1;

    if (df != 0 && dc != 0 && abs(df) != abs(dc))
        return false;

    for (int f = 0; f < 7; f++) {
        for (int c = 0; c < 5; c++) {
            if (m->tablero[f][c] != 'D')
                continue;

            if (!conectadas(f1, c1, f, c) ||
                !conectadas(f, c, f2, c2))
                continue;

            int df1 = f - f1;
            int dc1 = c - c1;
            int df2 = f2 - f;
            int dc2 = c2 - c;

            if (df1 * dc2 == dc1 * df2 &&
                df1 * df2 + dc1 * dc2 > 0) {
                fp = f;
                cp = c;
                return true;
            }
        }
    }

    return false;
}

bool puedeCapturar(Mesa* m, int f1, int c1,
                   int f2, int c2) {
    int fp, cp;
    return encontrarPerro(m, f1, c1, f2, c2, fp, cp);
}

bool pumaPuedeMover(Mesa* m) {
    for (int f = 0; f < 7; f++) {
        for (int c = 0; c < 5; c++) {
            if (m->tablero[f][c] != '.')
                continue;

            if (conectadas(m->pumaFila, m->pumaCol, f, c) ||
                puedeCapturar(m, m->pumaFila, m->pumaCol, f, c))
                return true;
        }
    }

    return false;
}

void* mover(void* argumento) {
    Ficha* ficha = (Ficha*)argumento;
    Mesa* m = ficha->mesa;
    int id = ficha->id;

    unsigned int semilla =
        semillaGeneral ^ ((m->numero * 31u + id + 1u) * 7919u);

    while (!detener) {
        int retraso = tiempoMin +
            rand_r(&semilla) % (tiempoMax - tiempoMin + 1);

        usleep(retraso * 1000);

        pthread_mutex_lock(&m->mutex);

        if (detener || m->resultado != 0) {
            pthread_mutex_unlock(&m->mutex);
            break;
        }

        if (modo == "fallo" &&
            m->numero == 1 &&
            id == 0 &&
            m->controladorTerminado[0]) {
            pthread_mutex_unlock(&m->mutex);
            break;
        }

        int f1 = id == 0 ? m->pumaFila : m->perroFila[id-1];
        int c1 = id == 0 ? m->pumaCol : m->perroCol[id-1];

        if (f1 >= 0) {
            int opcionesF[35], opcionesC[35], tipo[35];
            int cantidad = 0;

            for (int f = 0; f < 7; f++) {
                for (int c = 0; c < 5; c++) {
                    if (m->tablero[f][c] != '.')
                        continue;

                    if (conectadas(f1, c1, f, c)) {
                        opcionesF[cantidad] = f;
                        opcionesC[cantidad] = c;
                        tipo[cantidad++] = 0;
                    }
                    else if (id == 0 &&
                             puedeCapturar(m, f1, c1, f, c)) {
                        opcionesF[cantidad] = f;
                        opcionesC[cantidad] = c;
                        tipo[cantidad++] = 1;
                    }
                }
            }

            if (cantidad > 0) {
                int elegido = rand_r(&semilla) % cantidad;
                int f2 = opcionesF[elegido];
                int c2 = opcionesC[elegido];

                if (tipo[elegido] == 1) {
                    int fm, cm;
                    encontrarPerro(m, f1, c1, f2, c2, fm, cm);

                    for (int k = 0; k < 12; k++) {
                        if (m->perroFila[k] == fm &&
                            m->perroCol[k] == cm) {
                            m->perroFila[k] = -1;
                            m->perroCol[k] = -1;
                            break;
                        }
                    }

                    m->tablero[fm][cm] = '.';
                    m->capturas++;
                }

                m->tablero[f2][c2] = m->tablero[f1][c1];
                m->tablero[f1][c1] = '.';

                if (id == 0) {
                    m->pumaFila = f2;
                    m->pumaCol = c2;
                }
                else {
                    m->perroFila[id-1] = f2;
                    m->perroCol[id-1] = c2;
                }

                m->movimientos++;

                if (m->movimientos % 30 == 0)
                    mostrarTablero(m);
            }
        }

        if (modo == "abandono" &&
            m->numero == 1 &&
            m->movimientos >= 20) {
            m->resultado = 3;
        }

        pthread_mutex_unlock(&m->mutex);
    }

    return NULL;
}

void* controlarJugador(void* argumento) {
    Controlador* c = (Controlador*)argumento;
    Mesa* m = c->mesa;

    while (!detener) {
        pthread_mutex_lock(&m->mutex);

        if (m->resultado != 0) {
            pthread_mutex_unlock(&m->mutex);
            break;
        }

        if (modo == "fallo" &&
            m->numero == 1 &&
            c->id == 0 &&
            m->movimientos >= 20) {
            m->controladorTerminado[c->id] = true;
            pthread_mutex_unlock(&m->mutex);
            break;
        }

        m->actividad[c->id] = milisegundos();

        pthread_mutex_unlock(&m->mutex);
        usleep(80000);
    }

    return NULL;
}

void* arbitrar(void* argumento) {
    Mesa* m = (Mesa*)argumento;

    while (!detener) {
        usleep(40000);

        pthread_mutex_lock(&m->mutex);

        if (m->resultado == 0) {
            if (m->capturas >= 6) {
                m->resultado = 1;
            }
            else if (!pumaPuedeMover(m)) {
                m->resultado = 2;
            }
            else if (milisegundos() - m->actividad[0] > 600) {
                m->resultado = 5;
            }
            else if (milisegundos() - m->actividad[1] > 600) {
                m->resultado = 6;
            }
        }

        bool fin = m->resultado != 0;

        pthread_mutex_unlock(&m->mutex);

        if (fin)
            break;
    }

    return NULL;
}

bool esperarMesa() {
    while (!detener) {
        timespec limite;
        clock_gettime(CLOCK_REALTIME, &limite);
        limite.tv_sec++;

        if (sem_timedwait(&mesasLibres, &limite) == 0) {
            if (detener) {
                sem_post(&mesasLibres);
                return false;
            }
            return true;
        }

        if (errno != ETIMEDOUT && errno != EINTR)
            return false;
    }

    return false;
}

void* jugarPartida(void* argumento) {
    Mesa* m = (Mesa*)argumento;
    m->estado = 0;

    if (!esperarMesa()) {
        m->estado = detener ? 4 : 5;

        pthread_mutex_lock(&mutexPantalla);
        cout << "Partida " << m->numero
             << (detener ? ": cancelada mientras esperaba mesa"
                         : ": error al esperar mesa")
             << endl;
        pthread_mutex_unlock(&mutexPantalla);

        return NULL;
    }

    m->estado = 1;
    iniciarMesa(m);

    if (pthread_mutex_init(&m->mutex, NULL) != 0) {
        m->estado = 5;
        sem_post(&mesasLibres);
        return NULL;
    }

    pthread_mutex_lock(&mutexPantalla);

    cout << "Partida " << m->numero
         << " comienza (Puma: jugador " << m->jugadorPuma
         << ", Perros: jugador " << m->jugadorPerros << ")"
         << endl;

    pthread_mutex_unlock(&mutexPantalla);

    pthread_t hilos[13];
    pthread_t controladores[2];
    pthread_t juez;

    Ficha fichas[13];
    Controlador datos[2];

    int creados = 0;
    int controlesCreados = 0;
    bool juezCreado = false;

    for (int i = 0; i < 13 && !detener; i++) {
        fichas[i].mesa = m;
        fichas[i].id = i;

        if (pthread_create(&hilos[i], NULL,
                           mover, &fichas[i]) != 0)
            break;

        creados++;
    }

    if (creados == 13 && !detener) {
        for (int i = 0; i < 2; i++) {
            datos[i].mesa = m;
            datos[i].id = i;

            if (pthread_create(&controladores[i], NULL,
                               controlarJugador, &datos[i]) != 0)
                break;

            controlesCreados++;
        }
    }

    if (creados == 13 &&
        controlesCreados == 2 &&
        !detener) {
        juezCreado =
            pthread_create(&juez, NULL, arbitrar, m) == 0;
    }

    if (!detener &&
        (!juezCreado || controlesCreados != 2 || creados != 13)) {
        pthread_mutex_lock(&m->mutex);
        m->resultado = 4;
        pthread_mutex_unlock(&m->mutex);
    }

    if (juezCreado)
        pthread_join(juez, NULL);

    for (int i = 0; i < creados; i++)
        pthread_join(hilos[i], NULL);

    for (int i = 0; i < controlesCreados; i++)
        pthread_join(controladores[i], NULL);

    pthread_mutex_lock(&mutexPantalla);

    cout << "Partida " << m->numero << ": ";

    if (detener) {
        cout << "interrumpida con Ctrl+C";
        m->estado = 3;
    }
    else if (m->resultado == 1) {
        cout << "gana Puma, jugador " << m->jugadorPuma;
        m->estado = 2;
    }
    else if (m->resultado == 2) {
        cout << "ganan Perros, jugador " << m->jugadorPerros;
        m->estado = 2;
    }
    else if (m->resultado == 3) {
        cout << "abandono simulado del Puma; GANADOR: jugador "
             << m->jugadorPerros << " (Perros)";
        m->estado = 2;
    }
    else if (m->resultado == 5) {
        cout << "fallo detectado del Puma; GANADOR: jugador "
             << m->jugadorPerros << " (Perros)";
        m->estado = 2;
    }
    else if (m->resultado == 6) {
        cout << "fallo detectado de Perros; GANADOR: jugador "
             << m->jugadorPuma << " (Puma)";
        m->estado = 2;
    }
    else {
        cout << "error creando hilos";
        m->estado = 5;
    }

    cout << " | Capturas: " << m->capturas
         << " | Movimientos: " << m->movimientos << endl;

    pthread_mutex_unlock(&mutexPantalla);

    pthread_mutex_destroy(&m->mutex);
    sem_post(&mesasLibres);

    return NULL;
}

void* llegarJugador(void* argumento) {
    Jugador* j = (Jugador*)argumento;

    unsigned int semilla =
        semillaGeneral ^ (j->id * 997u);

    usleep((rand_r(&semilla) % 301) * 1000);

    pthread_mutex_lock(&mutexCola);

    if (!detener) {
        colaJugadores.push_back(j->id);

        pthread_mutex_lock(&mutexPantalla);
        cout << "Llego jugador " << j->id << endl;
        pthread_mutex_unlock(&mutexPantalla);

        pthread_cond_signal(&condCola);
    }

    pthread_mutex_unlock(&mutexCola);
    return NULL;
}

void* emparejar(void*) {
    while (!detener) {
        pthread_mutex_lock(&mutexCola);

        while (colaJugadores.size() < 2 &&
               !llegadasTerminadas && !detener) {
            timespec limite;
            clock_gettime(CLOCK_REALTIME, &limite);
            limite.tv_sec++;

            pthread_cond_timedwait(&condCola,
                                   &mutexCola, &limite);
        }

        if (detener ||
            (colaJugadores.size() < 2 && llegadasTerminadas)) {
            pthread_mutex_unlock(&mutexCola);
            break;
        }

        int j1 = colaJugadores[0];
        int j2 = colaJugadores[1];

        colaJugadores.erase(
            colaJugadores.begin(),
            colaJugadores.begin() + 2
        );

        pthread_mutex_unlock(&mutexCola);

        int n = partidasCreadas;

        partidas[n].numero = n + 1;
        partidas[n].jugadorPuma = j1;
        partidas[n].jugadorPerros = j2;
        partidas[n].estado = 0;

        pthread_mutex_lock(&mutexPantalla);
        cout << "Pareja formada: " << j1
             << " y " << j2 << endl;
        pthread_mutex_unlock(&mutexPantalla);

        if (pthread_create(&hilosPartidas[n], NULL,
                           jugarPartida, &partidas[n]) != 0) {
            detener = 1;
            break;
        }

        partidasCreadas++;
    }

    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 5) {
        cout << "Uso: ./torneo N M [config.txt] [abandono|fallo]"
             << endl;
        return 1;
    }

    char* finN;
    char* finM;

    long n = strtol(argv[1], &finN, 10);
    long m = strtol(argv[2], &finM, 10);

    if (*finN || *finM ||
        n <= 0 || n % 2 != 0 ||
        m <= 0 || n > 10000 || m > 10000) {
        cout << "Error en jugadores o mesas" << endl;
        return 1;
    }

    posicionesIniciales();

    if (argc >= 4 && !leerConfiguracion(argv[3])) {
        cout << "Error en config.txt" << endl;
        return 1;
    }

    if (argc == 5) {
        modo = argv[4];

        if (modo != "abandono" && modo != "fallo") {
            cout << "Modo de prueba desconocido" << endl;
            return 1;
        }
    }

    if (!validarInicio()) {
        cout << "Error en posiciones iniciales" << endl;
        return 1;
    }

    struct sigaction accion = {};
    accion.sa_handler = recibirSenal;
    sigemptyset(&accion.sa_mask);
    sigaction(SIGINT, &accion, NULL);

    if (sem_init(&mesasLibres, 0, (unsigned int)m) != 0)
        return 1;

    int total = n / 2;

    partidas.resize(total);
    hilosPartidas.resize(total);

    vector<Jugador> datos(n);
    vector<pthread_t> hilosJugadores(n);

    cout << "Retardos: " << tiempoMin
         << " a " << tiempoMax
         << " ms | Seed: " << semillaGeneral << endl;

    cout << "Jugadores: " << n
         << " | Maximo mesas: " << m << endl;

    if (modo != "normal")
        cout << "Modo prueba de " << modo << " activado" << endl;

    pthread_t hiloServidor;

    if (pthread_create(&hiloServidor, NULL,
                       emparejar, NULL) != 0) {
        sem_destroy(&mesasLibres);
        return 1;
    }

    int llegados = 0;

    for (int i = 0; i < n && !detener; i++) {
        datos[i].id = i + 1;

        if (pthread_create(&hilosJugadores[i], NULL,
                           llegarJugador, &datos[i]) != 0)
            break;

        llegados++;
    }

    for (int i = 0; i < llegados; i++)
        pthread_join(hilosJugadores[i], NULL);

    pthread_mutex_lock(&mutexCola);
    llegadasTerminadas = true;
    pthread_cond_signal(&condCola);
    pthread_mutex_unlock(&mutexCola);

    pthread_join(hiloServidor, NULL);

    for (int i = 0; i < partidasCreadas; i++)
        pthread_join(hilosPartidas[i], NULL);

    int iniciadas = 0;
    int completadas = 0;
    int interrumpidas = 0;
    int canceladas = 0;
    int errores = 0;

    for (int i = 0; i < partidasCreadas; i++) {
        int estado = partidas[i].estado;

        if (estado == 2 || estado == 3)
            iniciadas++;

        if (estado == 2)
            completadas++;
        else if (estado == 3)
            interrumpidas++;
        else if (estado == 4)
            canceladas++;
        else
            errores++;
    }

    sem_destroy(&mesasLibres);
    pthread_cond_destroy(&condCola);
    pthread_mutex_destroy(&mutexCola);
    pthread_mutex_destroy(&mutexPantalla);

    cout << "\nRESUMEN DEL TORNEO" << endl;
    cout << "Partidas creadas: " << partidasCreadas << endl;
    cout << "Partidas iniciadas: " << iniciadas << endl;
    cout << "Partidas completadas: " << completadas << endl;
    cout << "Partidas interrumpidas: " << interrumpidas << endl;
    cout << "Partidas canceladas: " << canceladas << endl;
    cout << "Partidas con error: " << errores << endl;

    if (detener)
        cout << "Servidor detenido con Ctrl+C" << endl;
    else
        cout << "Torneo finalizado" << endl;

    cout << "Recursos liberados" << endl;

    return detener ? 130 :
           (partidasCreadas == total && errores == 0 ? 0 : 1);
}
