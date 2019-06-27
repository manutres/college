/*
 * Shell `simplesh` (basado en el shell de xv6)
 *
 * Ampliación de Sistemas Operativos
 * Departamento de Ingeniería y Tecnología de Computadores
 * Facultad de Informática de la Universidad de Murcia
 *
 * Alumnos: ABBOU AAZAZ, MORAD (G2.2)
 *          MORENO GUILLAMON, MANUEL (G2.X)
 *
 * Convocatoria: FEBRERO/JUNIO/JULIO
 */


/*
 * Ficheros de cabecera
 */


#define _POSIX_C_SOURCE 200809L /* IEEE 1003.1-2008 (véase /usr/include/features.h) */
//#define NDEBUG                /* Traduce asertos y DMACROS a 'no ops' */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pwd.h>
#include <limits.h>
#include <libgen.h>
// Biblioteca readline
#include <readline/readline.h>
#include <readline/history.h>
#include <stdbool.h>
#include <dirent.h>

/******************************************************************************
 * Constantes, macros y variables globales
 ******************************************************************************/


static const char* VERSION = "0.18";

// Niveles de depuración
#define DBG_CMD   (1 << 0)
#define DBG_TRACE (1 << 1)
// . . .
static int g_dbg_level = 0;

#ifndef NDEBUG
#define DPRINTF(dbg_level, fmt, ...)                            \
    do {                                                        \
        if (dbg_level & g_dbg_level)                            \
            fprintf(stderr, "%s:%d:%s(): " fmt,                 \
                    __FILE__, __LINE__, __func__, ##__VA_ARGS__);       \
    } while ( 0 )

#define DBLOCK(dbg_level, block)                                \
    do {                                                        \
        if (dbg_level & g_dbg_level)                            \
            block;                                              \
    } while( 0 );
#else
#define DPRINTF(dbg_level, fmt, ...)
#define DBLOCK(dbg_level, block)
#endif

#define TRY(x)                                                  \
    do {                                                        \
        int __rc = (x);                                         \
        if( __rc < 0 ) {                                        \
            fprintf(stderr, "%s:%d:%s: TRY(%s) failed\n",       \
                    __FILE__, __LINE__, __func__, #x);          \
            fprintf(stderr, "ERROR: rc=%d errno=%d (%s)\n",     \
                    __rc, errno, strerror(errno));              \
            exit(EXIT_FAILURE);                                 \
        }                                                       \
    } while( 0 )


// Número máximo de argumentos de un comando
#define MAX_ARGS 16
#define DEFAULT_LINE_HD  3
#define BSIZE 1024
#define MAX_PROCS 8
#define MAX_CMD_BUFF_LENGTH 4096



// Delimitadores
static const char WHITESPACE[] = " \t\r\n\v";
// Caracteres especiales
static const char SYMBOLS[] = "<|>&;()";
static sigset_t signal_child;


/******************************************************************************
 * Estructuras de datos `cmd`
 ******************************************************************************/


// Las estructuras `cmd` se utilizan para almacenar información que servirá a
// simplesh para ejecutar líneas de órdenes con redirecciones, tuberías, listas
// de comandos y tareas en segundo plano. El formato es el siguiente:

//     |----------+--------------+--------------|
//     | (1 byte) | ...          | ...          |
//     |----------+--------------+--------------|
//     | type     | otros campos | otros campos |
//     |----------+--------------+--------------|

// Nótese cómo las estructuras `cmd` comparten el primer campo `type` para
// identificar su tipo. A partir de él se obtiene un tipo derivado a través de
// *casting* forzado de tipo. Se consigue así polimorfismo básico en C.

// Valores del campo `type` de las estructuras de datos `cmd`
enum cmd_type { EXEC=1, REDR=2, PIPE=3, LIST=4, BACK=5, SUBS=6, INV=7 };

struct cmd { enum cmd_type type; };

// Comando con sus parámetros
struct execcmd {
    enum cmd_type type;
    char* argv[MAX_ARGS];
    char* eargv[MAX_ARGS];
    int argc;
};

// Comando con redirección
struct redrcmd {
    enum cmd_type type;
    struct cmd* cmd;
    char* file;
    char* efile;
    int flags;
    mode_t mode;
    int fd;
};

// Comandos con tubería
struct pipecmd {
    enum cmd_type type;
    struct cmd* left;
    struct cmd* right;
};

// Lista de órdenes
struct listcmd {
    enum cmd_type type;
    struct cmd* left;
    struct cmd* right;
};

// Tarea en segundo plano (background) con `&`
struct backcmd {
    enum cmd_type type;
    struct cmd* cmd;
};

// Subshell
struct subscmd {
    enum cmd_type type;
    struct cmd* cmd;
};

/**
 * COMANDO SRC (similar al "source" o "." de linux)
 *
 * FUNCIONALIDAD: Ejecuta los comandos que se encuentran dentro del fichero que se le pasa como parametro
 * o se redirecciona desde la salida estandard de el anterior comando mediante tuberias.
*/

struct cmd * parse_cmd(char * start_of_cmd);
void free_cmd(struct cmd * cmd);
struct cmd *null_terminate(struct cmd * cmd);
void run_cmd(struct cmd * cmd);

char * readline_from_file(int fd) {

    char buffer[MAX_CMD_BUFF_LENGTH] = {0};
    char * actualcmd;
    int filepos = lseek(fd, 0, SEEK_CUR);
    int cmdreadedtam = 0;

    int n = read(fd, buffer, MAX_CMD_BUFF_LENGTH);
    if(n > 0) {
        for(int i = 0; i<n; i++) {
            if(buffer[i] == '\n') {
                cmdreadedtam = (&buffer[i] - buffer); //sin +1 para obviar el '\n'
                filepos = filepos + i + 1;
                actualcmd = malloc(sizeof(char) * cmdreadedtam+1);
                memcpy(actualcmd, buffer, cmdreadedtam);
                actualcmd[cmdreadedtam] = 0;
                lseek(fd,filepos, SEEK_SET);
                return actualcmd;
            }
            if(i == n-1){
                cmdreadedtam = (&buffer[n] - buffer);
                actualcmd = malloc(sizeof(char) * cmdreadedtam+1);
                memcpy(actualcmd, buffer, cmdreadedtam);
                actualcmd[cmdreadedtam] = 0;
                return actualcmd;
            }
        }
    } else {
        return NULL;
    }
    return NULL;
}

void src_from_file(int fd, char commentchar) {
    char  * linea = NULL;
    struct cmd * cmd = NULL;

    while((linea = readline_from_file(fd))) {
        if(linea[0] != commentchar) {
            cmd = parse_cmd(linea);
            null_terminate(cmd);
            run_cmd(cmd);

            free_cmd(cmd);
            free(cmd);
            free(linea);
        } else {
            free(linea);
        }
    }
}


/**
 * Funciones SRC
 */

void print_src_help()
{
    printf("USO: src [-d DELIM] [FILE 1] [FILE 2]\n");
    printf("\tOptions:\n");
    printf("\t-d DELIM: initial commentary character\n");
    printf("\t-h: show command functioning\n");
}


void parse_src_args(int argc, char * argv[])
{
    optind = 0;
    int c;
    while((c = getopt(argc, argv, "hd:")) != -1) {
        switch(c) {
            case 'h':
                print_src_help();
                return;
            case 'd':
                if(optind == argc) {
                    src_from_file(STDIN_FILENO, optarg[0]);
                } else {
                    for(int i = optind; i<argc; i++) {
                        int fd = open(argv[i], O_RDONLY);
                        src_from_file(fd, optarg[0]);
                        close(fd);
                    }
                }
                return;
            case '?':
                if(optopt == 'd') {
                    fprintf(stderr, "Option '%c' requies an argument.\n", optopt);
                }
                return;
            default:
                fprintf(stderr, "Invalid option.\n");
                return;
        }
    }

    //si no hubieran opciones solo nos interesan los ficheros
    if(optind == argc) {
        src_from_file(STDIN_FILENO, '%');
    } else {
        for(int i = optind; i<argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            src_from_file(fd, '%');
            close(fd);
        }
    }
}





/******************************************************************************
 * FLAGS para HD
 ******************************************************************************/

#define HD_NO_FLAGS 0           // $ hd <fichero>
#define HD_LINES_FLAG 1         // $ hd -l lineas <fichero>
#define HD_BYTES_FLAG 2         // $ hd -b bytes <fichero>
#define HD_TAM_FLAG 3           // $ hd -t bytes <fichero>
#define HD_LINES_TAM_FLAG 4     // $ hd -l linea -t bytesSize
#define HD_BYTES_TAM_FLAG 5     // $ hd -b bytes -t bytesSize

/******************************************************************************
 * FUNCIONES  PARA LAS SEÑALES
 ******************************************************************************/

//Nuestro array de pids de tamaña maximo MAX_PROCS == 8 para el caso de args -p
int pids_procesos[MAX_PROCS];

//Funcion auxiliar para darle la vuelva al a una cadena.
/**
 * Esta
 * @param begin
 * @param end
 */
void cadenaInversa(char *begin, char *end)
{
    char aux;
    while(end>begin)
        aux=*end, *end--=*begin, *begin++=aux;
}

/**
 * Esta función se utiliza para convertir un entero a string
 * @param value El Entero que se quiere transformar
 * @param str El buffer en el que se inserta el entero
 */
void integerToString(int value, char *str)
{
    char* wstr=str;
    int sign;
    div_t res;

    if ((sign=value) < 0) value = -value;

    do {
        *wstr++ = (value%10)+'0';
    }while((value=value/10) > 0);

    if(sign<0) *wstr++='-';
    *wstr='\0';

    cadenaInversa(str, wstr - 1);
}

/**
 * Comprueba que un proceso está en segundo plano
 * @param pid  El proceso del que se quiere hacer la comprobación
 * @return  Devuelve el valor 0 si es el proceso esta en segundo plano. Devuelve 1 si no lo esta
 */
int is_back_process(int pid){
    if (pid < 0) return 1;
    int i = 0 ;
    while(i < MAX_PROCS){
        if(pid == pids_procesos[i]) return 0;
        i++;
    }
    return 1;
}

/**
 * Esta funcion sirve para calcular el numero de cifras de un valor Integer.
 * @param numero El numero de tipo Integer
 * @return Devuelve el numero de cifras que tiene x
 */
int longitud_numero(int numero) {
    int cifras = 0;
    while (numero > 0) {
        numero /= 10;
        cifras++;
    }
    return cifras < 1 ? 1 : cifras;
}

/**
 * Función que se utiliza para escribir un tamaño de bytes en la salida estandar leyendo desde un fichero o entrada estandar
 * @param fd Descriptor de fichero del que se quiere hacer la lectura
 * @param buffer Puntero a una cadena de caracteres donde se almacenaran los bytes leidos del Descriptor de fichero
 * @param bytes_size Tamano en bytes del chunk y escribir
 */
void print_bytes(int fd, char* buffer, int bytes_size) ;


int longitud_numero(int numero);

//Funcion auxiliar para imprimir el PID que nos pasan como parametro.
/**
 * Imprime un proceso en el formato [ PID ]
 * @param pid Proceso que se quiere imprimir
 */
void print_processid(int pid){
    int longitud = longitud_numero(pid);

    char barra1= '[';
    write(STDOUT_FILENO, &barra1, 1);

    char pid_str[longitud];
    integerToString(pid, pid_str);
    write(STDOUT_FILENO, pid_str,longitud);         //TODO: Aqui veo el fallo

    char * barra2 = "]\n";
    write(STDOUT_FILENO, barra2, 2);
}

/**
 * Esta función se utiliza para bloquear las señal signal_child
 */
void block_SIG_CHLD() {
    // printf("> bloqueamos\n");
    if ( sigprocmask ( SIG_BLOCK , &signal_child , NULL ) == -1) {
        perror ( " sigprocmask " ) ;
        exit ( EXIT_FAILURE ) ;
    }
}

void release_SIG_CHLD() {
    // printf("> DESbloqueamos\n");
    if ( sigprocmask ( SIG_UNBLOCK , &signal_child , NULL ) == -1) {
        perror ( " sigprocmask " ) ;
        exit ( EXIT_FAILURE ) ;
    }
}


//Manejador de la señal SIGCHLD.
void handle_sigchld(int signal) {
    int saved_errno = errno;
    int pid;
    while ((pid = waitpid((pid_t)(-1), 0, WNOHANG)) > 0) {
        for(int i = 0; i < MAX_PROCS ;i++){
            if(pid == pids_procesos[i]){
                pids_procesos[i] = -1;
                break;
            }
        }
        print_processid(pid);
    }
    errno = saved_errno;
}

/******************************************************************************
 * Funciones auxiliares
 ******************************************************************************/

void print_hd_help() {
    printf("Uso: hd [-l NLINES] [-b NBYTES] [-t BSIZE] [FILE1] [FILE2]...\n"
                   "\tOpciones:\n"
                   "\t-l NLINES Numero maximo de lineas a mostrar.\n"
                   "\t-b NBYTES Numero maximo de bytes a mostrar.\n"
                   "\t-t BSIZE Tamano en bytes de los bloques leidos de [FILEn] o stdin.\n"
                   "\t-h help\n");
}

void print_bjobs_help() {
    //TODO:
}


/**
 * Funcion para el comando HD. ( $ hd -l N fichero )
 * Esta función se utiliza para leer un fichero e imprimir N líneas que se pidan
 * @param fd Despcriptor del fichero del que se va a leer
 * @param buffer  Buffer donde se almacenan los bytes que se van a leer
 * @param numLineas  Numero de lineas que se desean imprimir
 */
void read_and_print_lines(int fd, char* buffer, int numLineas) {
    ssize_t n = read(fd, buffer, BSIZE);                                                   // Leemos BSIZE bytes del fichero
    int pos_orign = 0;                                                                     // Tanto pos_origen como pos_actual
    int pos_actual = 0;                                                                    // se utilizan para el tamaño y situar las posiciones de escritura
    int num_saltos_linea = 0;                                                              // Contador de numero de saltos de linea

    for (int contador = 0; contador < n && num_saltos_linea < numLineas; ++contador) {     // Mientras que el contador sea menor que los bytes leidos y el
        if (buffer[contador] == '\n') {                                                    // Si se encuentra un \n
            pos_actual = contador + 1;                                                     //   Se sitúa la posicion actual en la pos. siguiente a \n
            write(STDOUT_FILENO, buffer + pos_orign, pos_actual - pos_orign);              //   Se escribe con tamano = pos_actual - pos origen
            pos_orign = contador + 1;                                                      //   Se sitúa la pos_orign en la posición siguiente al salto de linea
            num_saltos_linea++;                                                            //   Incrementamos el numero de saltos de linea
        }
    }

}

/**
 * Función que se utiliza para escribir un tamaño de bytes en la salida estandar leyendo desde un fichero o entrada estandar
 * @param fd Descriptor de fichero del que se quiere hacer la lectura
 * @param buffer Puntero a una cadena de caracteres donde se almacenaran los bytes leidos del Descriptor de fichero
 * @param bytes_size Tamano en bytes del chunk y escribir
 */
void print_bytes(int fd, char* buffer, int bytes_size) {
    ssize_t bytes_escritos;                                             // Variable para los bytes que se vayan a escribir en la salida estandar
    ssize_t n = 0;
    int bytes_leidos = 0;
    do {
        n = read(fd, buffer + bytes_leidos, BSIZE);                     // Leemos los bytes
        bytes_leidos += n;                                              // Incrementamos el numero de bytes leidos
    } while (n > 0 && bytes_leidos < bytes_size);                       // mientras no hayamos alcanzado el numero total de bytes a imprimir o no haya nada mas que leer
    int total_escrito = 0;                                              // Variable para el numero total de bytes escritos en la salida standar
    if (bytes_size <= bytes_leidos) {                                   // Si los el tamanno de bytes que queremos escribir es menor al tamano leido
        bytes_escritos = write(STDOUT_FILENO, buffer, bytes_size);      //  Escribimos el tamano de bytes
        total_escrito += bytes_escritos;                                //  Incrementamos el tamano total escrito
        while (bytes_escritos < bytes_size) {                           //   Comprobamos si se han escrito todos los bytes y aseguramos
            bytes_escritos = write(STDOUT_FILENO, buffer + total_escrito, bytes_size - total_escrito);
            total_escrito += bytes_escritos;
        }
    } else if (bytes_size > bytes_leidos) {                             // Sino
        int leidos = bytes_leidos;                                      //
        bytes_escritos = write(STDOUT_FILENO, buffer, leidos);          // Escribimos los bytes en la entrada estandar
        total_escrito += bytes_escritos;                                // incrementamos el numero de bytes total escritos
        while(total_escrito < leidos) {                                 // Comprobamos si los bytes escritos iguales que los bytes leidos
            bytes_escritos = write(STDOUT_FILENO, buffer + total_escrito, leidos - total_escrito);
            total_escrito += bytes_escritos;
        }
    }
}

/**
 * Esta funcion para el comando HD ( $ hd -t size )
 * Se trata de una funcion que lee de un fichero o una entrada estandar con un tamaño determinado de bytes
 * y los imprime en la salida estandar
 * @param fd Descritpor del fichero del que se van a leer los bytes
 * @param buffer Buffer donde se almacenaran los bytes leidos
 * @param size Tamano en bytes del chunk que se tiene que leer de un fichero
 */
void read_and_print_tam(int fd, char * buffer, int size) {
    ssize_t n = read(fd, buffer, size);
    int total = 0;
    do {
        ssize_t w = write(STDOUT_FILENO, buffer, 1);
        if (w != n) {
            int total = w;
            do {
                w = write(STDOUT_FILENO, total + buffer, n - total);
                total += w;
            } while(total < size);
        }
        total += n;
        n = read(fd, buffer, BSIZE);
    } while (total < size && n > 0);
}

/**
 * Esta funcion es para el comando HD. ( $ hd -l lines -t size)
 * Esta función lo que hace es leer un fichero o entrada estandar con un tamanño de chunk establecido e imprimir tantas lineas
 * como le hayamos indicado.
 * @param fd Descriptor de fichero del que se quieren leer los bytes
 * @param buffer Buffer donde se almacenaran los bytes leídos
 * @param lines Numero de lineas que se quiere imprimir
 * @param size  Establece el tamano de chunk en bytes que se utiliza para leer.
 */
void rd_tam_print_lines(int fd, char * buffer,  int lines, int size) { // TODO: Aqui falla algo
    ssize_t bytes_leidos = 0;                                                     //
    int ultima_pos_origen = 0;
    int num_saltos_lineas = 0;
    int tam_restante = 0;

    do {                                                               // Mientras los bytes leidos sea menor que cero y el num de saltos de lineas < num de linesas
        bytes_leidos = read(fd, buffer, (size_t) size);                                                // Leemos los bytes
        int contador = 0;                                                                              // Contador para recorrer el buffer
        ultima_pos_origen = 0;                                                                         //
        while (contador < bytes_leidos && num_saltos_lineas < lines) {                                 // Mientras que el numero de saltos de linea es menor que el numero de lineas establecidas
            if (buffer[contador] == '\n') {                                                            //   Si encontramos un \n
                write(STDOUT_FILENO, buffer + ultima_pos_origen, contador - ultima_pos_origen + 1);    //       Escribrimos desde la ulitma posición origen
                ultima_pos_origen = contador + 1;                                                      //
                num_saltos_lineas++;                                                                   //       actualizamos la ultima posicion origne
            }                                                                                          //
            tam_restante = contador - ultima_pos_origen + 1;                                           //       El tamaño restante es igual a la posicion actual - la posición orign
            contador++;
        }

        if(tam_restante > 0) {                                                                          // Si aun quedan bytes por escribir
            write(STDOUT_FILENO, buffer + ultima_pos_origen, tam_restante);                             // Lo escribimos
            tam_restante = 0;
        }
    } while(bytes_leidos > 0 && num_saltos_lineas < lines);
}

/**
 * Funcion para hd ( $ hd -t bytes_to_read -b bytes_to_write)
 * Esta función sirve para leer un un tamaño de bytes de un fichero y luego
 * imprimir numero de bytes.
 * @param fd    Descriptor de fichero del que se van a leer los bytes
 * @param buffer  Buffer donde se almacenan los bytes leidos
 * @param bytes_to_write El numero de bytes que tendremos que imprimir en la entrada estnadar
 * @param bytes_to_read El numero de bytes que tenemos que leer del fichero
 */
void rd_tam_print_bytes(int fd, char * buffer, int bytes_to_write, int bytes_to_read) {
    buffer = malloc(bytes_to_read * sizeof(char));
    memset(buffer, 0,  bytes_to_read);
    ssize_t w;
    ssize_t bytes_leidos = 0;
    bytes_leidos = read(fd, buffer, bytes_to_read);                         // Numero de bytes que tenemos que leer
    ssize_t total_escrito = 0;                                              // Numero de bytes totales impresos
    while(bytes_leidos > 0 && total_escrito < bytes_to_write) {             // Mientras queden bytes por imprimir
        int escritura_parcial = 0;
        w = write(STDOUT_FILENO, buffer, bytes_leidos);                     // Escribimos y comprobamos que se hayan escrito
        total_escrito += w;
        escritura_parcial = w;
        while(escritura_parcial < bytes_leidos) {
            w = write(STDOUT_FILENO, buffer + escritura_parcial, bytes_leidos - escritura_parcial);
            escritura_parcial += w;
        }
        bytes_leidos = read(fd, buffer, bytes_to_read);                     // Leemos
    }
}

/**
 * Funcion para el comando hd que se encarga utilizar la función adecuada según
 * los flags que se han utilzado.
 * @param fd
 * @param valor1
 * @param tam
 * @param flag
 */
void process_hd_from_file(int fd, int valor1, int tam, int flag) {
    char * buffer = malloc(BSIZE * sizeof(char));
    ssize_t n;
    switch (flag) {
        case HD_NO_FLAGS:   // No hay flags
            read_and_print_lines(fd, buffer, DEFAULT_LINE_HD);
            break;
        case HD_LINES_FLAG: // -l
            read_and_print_lines(fd, buffer, valor1);
            break;
        case HD_BYTES_FLAG: // -b
            print_bytes(fd, buffer, valor1);
            break;
        case HD_TAM_FLAG:   // -t
            read_and_print_tam(fd, buffer, tam);
            break;
        case HD_LINES_TAM_FLAG:     // -l -t
            rd_tam_print_lines(fd, buffer, valor1, tam);
            break;
        case HD_BYTES_TAM_FLAG:     // -b -t
            rd_tam_print_bytes(fd, buffer, valor1, tam);
            break;
    }
    free(buffer);
}

/**
 * Para leer de la entrada estandar para el comando hd e imprimir las lineas establecidas
 * @param buffer
 * @param lines_read
 */
void  read_lines_from_term(char * buffer, int lines_read) {
    ssize_t n;
    int number_of_reads = 0;
    do {
        n = read(STDIN_FILENO, buffer, BSIZE);
        int pos_orign = 0;
        int pos_destino = 0;
        int num_enter = 0;
        for (int i = 0; i < n && num_enter < lines_read; ++i) {
            if (buffer[i] == '\n') {
                pos_destino = i + 1;
                write(STDOUT_FILENO, buffer + pos_orign, pos_destino - pos_orign);
                pos_orign = i + 1;
                num_enter++;
            }
        }
        number_of_reads++;
    } while(n != 0 && number_of_reads < lines_read);
}
/**
 * Funcion que se usa para procesar el comando hd cuando se lee desde la entrada estandar
 * @param valor1
 * @param tam
 * @param flag
 */
void process_hd_from_terminal(int valor1, int tam, int flag) {
    char * buffer = malloc(BSIZE * sizeof(char));
    memset(buffer, 0, BSIZE);
    switch (flag) {
        case HD_NO_FLAGS:
            read_lines_from_term(buffer, DEFAULT_LINE_HD);
            break;
        case HD_LINES_FLAG:
            read_lines_from_term(buffer, valor1);
            break;
        case HD_BYTES_FLAG:
            print_bytes(STDIN_FILENO, buffer, valor1);
            break;
        case HD_TAM_FLAG:
            read_and_print_tam(STDIN_FILENO, buffer, tam);
            break;
        case HD_LINES_TAM_FLAG:
            rd_tam_print_lines(STDIN_FILENO, buffer, valor1, tam);
            break;
        case HD_BYTES_TAM_FLAG:
            rd_tam_print_bytes(STDIN_FILENO, buffer, valor1, tam);
            break;
    }
    free(buffer);
}

/**
 * Funcion del comando HD para parsear los flags.
 * @param argc
 * @param argv
 */
void parse_hd_args(int argc, char *argv[]) {
    int opt, flag;          // La variable flag la utilizaremos para clasificar el tipo de comando y flags que existen y así poder
    int arg_1 = 0;          // procesar la función correspondiente
    int arg_t = 0;
    flag = HD_NO_FLAGS;
    optind = 1;
    int hayficheros = 0;
    while ((opt = getopt(argc, argv, "l:b:t:h")) != -1) {
        switch (opt) {
            case 'l':
                if (flag == HD_NO_FLAGS) {
                    flag = HD_LINES_FLAG;
                    arg_1 = atoi(optarg);
                } else if (flag == HD_TAM_FLAG) {
                    flag = HD_LINES_TAM_FLAG;
                    arg_1 = atoi(optarg);
                } else {
                    printf("hd: Opciones incompatibles.\n");
                    return;
                }
                break;
            case 'b':
                if (flag == HD_NO_FLAGS) {
                    flag = HD_BYTES_FLAG;
                    arg_1 = atoi(optarg);
                } else if (flag == HD_TAM_FLAG) {
                    flag = HD_BYTES_TAM_FLAG;
                    arg_1 = atoi(optarg);
                } else {
                    printf("hd: Opciones incompatibles.\n");
                    return;
                }
                break;
            case 't':
                if (flag == HD_NO_FLAGS) {
                    arg_t = atoi(optarg);
                    flag = HD_TAM_FLAG;
                } else if (flag == HD_LINES_FLAG) {
                    flag = HD_LINES_TAM_FLAG;
                    arg_t = atoi(optarg);
                } else if (flag == HD_BYTES_FLAG) {
                    flag = HD_BYTES_TAM_FLAG;
                    arg_t = atoi(optarg);
                } else {
                    printf("hd: Opciones incompatibles.\n");
                    return;
                }
                break;
            case 'h':
                print_hd_help();
                return;
                break;
            default:
                fprintf(stderr, "Usage: %s [-f] [-n NUM]\n", argv[0]);
                break;
        }
    }

    if ((flag == HD_TAM_FLAG || flag == HD_LINES_TAM_FLAG || flag == HD_BYTES_TAM_FLAG) && arg_1 <= 0) {
        printf("hd: Opción no válida.\n");
        return;
    }

    hayficheros = argc - optind;
    if (hayficheros > 0) {
        for (int i = optind; i < argc; ++i) {
            int fd = open(argv[i], O_RDONLY);
            if (fd > 0)
                process_hd_from_file(fd, arg_1, arg_t, flag);
            else
                printf("hd: No se encontró el archivo '%s'\n", argv[i]);
            close(fd);
        }
    } else {
        process_hd_from_terminal(arg_1, arg_t, flag);
    }
}


// Imprime el mensaje
void info(const char *fmt, ...)
{
    va_list arg;

    fprintf(stdout, "%s: ", __FILE__);
    va_start(arg, fmt);
    vfprintf(stdout, fmt, arg);
    va_end(arg);
}


// Imprime el mensaje de error
void error(const char *fmt, ...)
{
    va_list arg;

    fprintf(stderr, "%s: ", __FILE__);
    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);
}

// Imprime el mensaje de error y aborta la ejecución
void panic(const char *fmt, ...)
{
    va_list arg;

    fprintf(stderr, "%s: ", __FILE__);
    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);

    exit(EXIT_FAILURE);
}


// `fork()` que muestra un mensaje de error si no se puede crear el hijo
int fork_or_panic(const char* s)
{
    int pid;

    pid = fork();
    if(pid == -1)
        panic("%s failed: errno %d (%s)", s, errno, strerror(errno));
    return pid;
}

/**
 *
 * @param argc
 * @param argv
 */
void run_bjobs(int argc, char* argv[]) {
    int opt;
    optind = 1;
    int hay_args = 0;
    while ((opt = getopt(argc, argv, "sc")) != -1) {
        switch (opt) {
            case 's':
                if (hay_args > 0) {
                    fprintf(stderr, "bjobs: opciones no compatibles.\n");
                } else {
                    for (int i = 0; i < MAX_PROCS; ++i) {
                        if (is_back_process(pids_procesos[i]) == 0) {
                            kill(pids_procesos[i], SIGSTOP);
                        }
                    }
                    hay_args++;
                }
                break;
            case 'c':
                if (hay_args > 0) {
                    fprintf(stderr, "bjobs: opciones no compatibles.\n");
                } else {
                    for (int i = 0; i < MAX_PROCS; ++i) {
                        if (is_back_process(pids_procesos[i]) == 0) {
                            kill(pids_procesos[i], SIGCONT);
                        }
                    }
                    hay_args++;
                }
                break;
            case 'h':
                print_bjobs_help();
                return;
                break;
            default:
                fprintf(stderr, "Usage: %s [-f] [-n NUM]\n", argv[0]);
                break;
        }
    }
    if (hay_args == 0) {
        for (int i = 0; i < MAX_PROCS; ++i) {               // TODO: Un momento O.o
            if(pids_procesos[i] != -1)
                printf("[%d]\n",pids_procesos[i]);
        }
    }
}


void run_cwd() {
    char ruta[PATH_MAX];
    if (!getcwd(ruta, PATH_MAX)) {
        perror("FALLO DE RUTA");
        exit(EXIT_FAILURE);
    }
    printf("cwd: %s\n", ruta);
}

/*
 * Devuelve la ruta actual
 */
char * get_cur_dir() {
    char * ruta = malloc(PATH_MAX * sizeof(char*));
    if (!getcwd(ruta, PATH_MAX)) {
        perror("FALLO DE RUTA");
        exit(EXIT_FAILURE);
    }
    return ruta;
}

void run_cd_HOME() {
    char * dir_actual = get_cur_dir();
    char * dir_home = getenv("HOME");
    chdir(dir_home);
    setenv("OLDPWD", dir_actual, true);
}

void run_cd(char* path) {
    if (strcmp(path, "-") == 0) {
        char * dir = getenv("OLDPWD");
        if (dir != NULL) {
            run_cd(dir);
        } else {
            printf("run_cd: Variable OLDPWD no definida\n");
        }
    } else {
        DIR* dir = opendir(path);
        if(!dir) {
            printf("run_cd: No existe el directorio '%s'\n", path);
        } else {
            char * dir_actual = get_cur_dir();
            char * directorio = malloc((PATH_MAX + 1) * sizeof(char*));
            realpath(path, directorio);
            if (strcmp(directorio, "") == 0) {
                perror("run_cd: No existe el directorio\n");
            } else {
                int succes = chdir(directorio);
                if (succes == 0) {
                    setenv("OLDPWD", dir_actual, true);
                } else {
                    perror("NO se ha podido cambiar de directorio\n");
                }
                free(directorio);
            }
        }
    }
}

/******************************************************************************
 * Funciones para construir las estructuras de datos `cmd`
 ******************************************************************************/


// Construye una estructura `cmd` de tipo `EXEC`
struct cmd* execcmd(void)
{
    struct execcmd* cmd;

    if ((cmd = malloc(sizeof(*cmd))) == NULL)
    {
        perror("execcmd: malloc");
        exit(EXIT_FAILURE);
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = EXEC;

    return (struct cmd*) cmd;
}

// Construye una estructura `cmd` de tipo `REDR`
struct cmd* redrcmd(struct cmd* subcmd,
                    char* file, char* efile,
                    int flags, mode_t mode, int fd)
{
    struct redrcmd* cmd;

    if ((cmd = malloc(sizeof(*cmd))) == NULL)
    {
        perror("redrcmd: malloc");
        exit(EXIT_FAILURE);
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = REDR;
    cmd->cmd = subcmd;
    cmd->file = file;
    cmd->efile = efile;
    cmd->flags = flags;
    cmd->mode = mode;
    cmd->fd = fd;

    return (struct cmd*) cmd;
}

// Construye una estructura `cmd` de tipo `PIPE`
struct cmd* pipecmd(struct cmd* left, struct cmd* right)
{
    struct pipecmd* cmd;

    if ((cmd = malloc(sizeof(*cmd))) == NULL)
    {
        perror("pipecmd: malloc");
        exit(EXIT_FAILURE);
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = PIPE;
    cmd->left = left;
    cmd->right = right;

    return (struct cmd*) cmd;
}

// Construye una estructura `cmd` de tipo `LIST`
struct cmd* listcmd(struct cmd* left, struct cmd* right)
{
    struct listcmd* cmd;

    if ((cmd = malloc(sizeof(*cmd))) == NULL)
    {
        perror("listcmd: malloc");
        exit(EXIT_FAILURE);
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = LIST;
    cmd->left = left;
    cmd->right = right;

    return (struct cmd*)cmd;
}

// Construye una estructura `cmd` de tipo `BACK`
struct cmd* backcmd(struct cmd* subcmd)
{
    struct backcmd* cmd;

    if ((cmd = malloc(sizeof(*cmd))) == NULL)
    {
        perror("backcmd: malloc");
        exit(EXIT_FAILURE);
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = BACK;
    cmd->cmd = subcmd;

    return (struct cmd*)cmd;
}

// Construye una estructura `cmd` de tipo `SUB`
struct cmd* subscmd(struct cmd* subcmd)
{
    struct subscmd* cmd;

    if ((cmd = malloc(sizeof(*cmd))) == NULL)
    {
        perror("subscmd: malloc");
        exit(EXIT_FAILURE);
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = SUBS;
    cmd->cmd = subcmd;

    return (struct cmd*) cmd;
}

void run_exit(struct execcmd * ecmd) {

    // LIMPIAR LA MEMORIA
    free(ecmd);
    exit(EXIT_SUCCESS);
}

/******************************************************************************
 * Funciones para realizar el análisis sintáctico de la línea de órdenes
 ******************************************************************************/


// `get_token` recibe un puntero al principio de una cadena (`start_of_str`),
// otro puntero al final de esa cadena (`end_of_str`) y, opcionalmente, dos
// punteros para guardar el principio y el final del token, respectivamente.
//
// `get_token` devuelve un *token* de la cadena de entrada.

int get_token(char** start_of_str, char* end_of_str,
              char** start_of_token, char** end_of_token)
{
    char* s;
    int ret;

    // Salta los espacios en blanco
    s = *start_of_str;
    while (s < end_of_str && strchr(WHITESPACE, *s))
        s++;

    // `start_of_token` apunta al principio del argumento (si no es NULL)
    if (start_of_token)
        *start_of_token = s;

    ret = *s;
    switch (*s)
    {
        case 0:
            break;
        case '|':
        case '(':
        case ')':
        case ';':
        case '&':
        case '<':
            s++;
            break;
        case '>':
            s++;
            if (*s == '>')
            {
                ret = '+';
                s++;
            }
            break;

        default:

            // El caso por defecto (cuando no hay caracteres especiales) es el
            // de un argumento de un comando. `get_token` devuelve el valor
            // `'a'`, `start_of_token` apunta al argumento (si no es `NULL`),
            // `end_of_token` apunta al final del argumento (si no es `NULL`) y
            // `start_of_str` avanza hasta que salta todos los espacios
            // *después* del argumento. Por ejemplo:
            //
            //     |-----------+---+---+---+---+---+---+---+---+---+-----------|
            //     | (espacio) | a | r | g | u | m | e | n | t | o | (espacio)
            //     |
            //     |-----------+---+---+---+---+---+---+---+---+---+-----------|
            //                   ^                                   ^
            //            start_o|f_token                       end_o|f_token

            ret = 'a';
            while (s < end_of_str &&
                   !strchr(WHITESPACE, *s) &&
                   !strchr(SYMBOLS, *s))
                s++;
            break;
    }

    // `end_of_token` apunta al final del argumento (si no es `NULL`)
    if (end_of_token)
        *end_of_token = s;

    // Salta los espacios en blanco
    while (s < end_of_str && strchr(WHITESPACE, *s))
        s++;

    // Actualiza `start_of_str`
    *start_of_str = s;

    return ret;
}



//************************************************************************
//*          FUNCIONES DE LA PRÁCTICA
//************************************************************************

int internal_cmd(struct execcmd * ecmd) {
    if (ecmd->argv[0] == 0) return 0;
    if (strcmp(ecmd->argv[0], "exit") == 0) {
        return 1;
    } else if (strcmp(ecmd->argv[0], "cwd") == 0) {
        return 2;
    } else if (strcmp(ecmd->argv[0], "cd") == 0) {
        return  3;
    } else  if (strcmp(ecmd->argv[0], "hd") == 0) {
        return 4;
    } else if (strcmp(ecmd->argv[0], "src") == 0) {
        return 5;
    } else if (strcmp(ecmd->argv[0], "bjobs") == 0) {
        return 6;
    }
    return 0;
}

void exec_internal_cmd(struct execcmd * ecmd) {
    if (strcmp(ecmd->argv[0], "exit") == 0) {
        run_exit(ecmd);
    } else if (strcmp(ecmd->argv[0], "cwd") == 0) {
        run_cwd();
    } else if (strcmp(ecmd->argv[0], "cd") == 0) {
        if (ecmd->argv[1] != NULL) {
            if(ecmd->argc > 2) {
                printf("run_cd: Demasiados argumentos\n");
            } else {
                run_cd(ecmd->argv[1]);
            }
        } else {
            run_cd_HOME();
        }
    } else if (strcmp(ecmd->argv[0], "hd") == 0) {
        parse_hd_args(ecmd->argc, ecmd->argv);
    } else if (strcmp(ecmd->argv[0], "bjobs") == 0) {
        run_bjobs(ecmd->argc, ecmd->argv);
    } else if (strcmp(ecmd->argv[0], "src") == 0) {
        parse_src_args(ecmd->argc, ecmd->argv);
    }
}





//*******************************************************************

// `peek` recibe un puntero al principio de una cadena (`start_of_str`), otro
// puntero al final de esa cadena (`end_of_str`) y un conjunto de caracteres
// (`delimiter`).
//
// El primer puntero pasado como parámero (`start_of_str`) avanza hasta el
// primer carácter que no está en el conjunto de caracteres `WHITESPACE`.
//
// `peek` devuelve un valor distinto de `NULL` si encuentra alguno de los
// caracteres en `delimiter` justo después de los caracteres en `WHITESPACE`.

int peek(char** start_of_str, char* end_of_str, char* delimiter)
{
    char* s;

    s = *start_of_str;
    while (s < end_of_str && strchr(WHITESPACE, *s))
        s++;
    *start_of_str = s;

    return *s && strchr(delimiter, *s);
}


// Definiciones adelantadas de funciones
struct cmd* parse_line(char**, char*);
struct cmd* parse_pipe(char**, char*);
struct cmd* parse_exec(char**, char*);
struct cmd* parse_subs(char**, char*);
struct cmd* parse_redr(struct cmd*, char**, char*);
struct cmd* null_terminate(struct cmd*);


// `parse_cmd` realiza el *análisis sintáctico* de la línea de órdenes
// introducida por el usuario.
//
// `parse_cmd` utiliza `parse_line` para obtener una estructura `cmd`.

struct cmd* parse_cmd(char* start_of_str)
{
    char* end_of_str;
    struct cmd* cmd;

    DPRINTF(DBG_TRACE, "STR\n");

    end_of_str = start_of_str + strlen(start_of_str);

    cmd = parse_line(&start_of_str, end_of_str);

    // Comprueba que se ha alcanzado el final de la línea de órdenes
    peek(&start_of_str, end_of_str, "");
    if (start_of_str != end_of_str)
        error("%s: error sintáctico: %s\n", __func__);

    DPRINTF(DBG_TRACE, "END\n");

    return cmd;
}


// `parse_line` realiza el análisis sintáctico de la línea de órdenes
// introducida por el usuario.
//
// `parse_line` comprueba en primer lugar si la línea contiene alguna tubería.
// Para ello `parse_line` llama a `parse_pipe` que a su vez verifica si hay
// bloques de órdenes y/o redirecciones.  A continuación, `parse_line`
// comprueba si la ejecución de la línea se realiza en segundo plano (con `&`)
// o si la línea de órdenes contiene una lista de órdenes (con `;`).

struct cmd* parse_line(char** start_of_str, char* end_of_str)
{
    struct cmd* cmd;
    int delimiter;

    cmd = parse_pipe(start_of_str, end_of_str);

    while (peek(start_of_str, end_of_str, "&"))
    {
        // Consume el delimitador de tarea en segundo plano
        delimiter = get_token(start_of_str, end_of_str, 0, 0);
        assert(delimiter == '&');

        // Construye el `cmd` para la tarea en segundo plano
        cmd = backcmd(cmd);
    }

    if (peek(start_of_str, end_of_str, ";"))
    {
        if (cmd->type == EXEC && ((struct execcmd*) cmd)->argv[0] == 0)
            error("%s: error sintáctico: no se encontró comando\n", __func__);

        // Consume el delimitador de lista de órdenes
        delimiter = get_token(start_of_str, end_of_str, 0, 0);
        assert(delimiter == ';');

        // Construye el `cmd` para la lista
        cmd = listcmd(cmd, parse_line(start_of_str, end_of_str));
    }

    return cmd;
}


// `parse_pipe` realiza el análisis sintáctico de una tubería de manera
// recursiva si encuentra el delimitador de tuberías '|'.
//
// `parse_pipe` llama a `parse_exec` y `parse_pipe` de manera recursiva para
// realizar el análisis sintáctico de todos los componentes de la tubería.

struct cmd* parse_pipe(char** start_of_str, char* end_of_str)
{
    struct cmd* cmd;
    int delimiter;

    cmd = parse_exec(start_of_str, end_of_str);

    if (peek(start_of_str, end_of_str, "|"))
    {
        if (cmd->type == EXEC && ((struct execcmd*) cmd)->argv[0] == 0)
            error("%s: error sintáctico: no se encontró comando\n", __func__);

        // Consume el delimitador de tubería
        delimiter = get_token(start_of_str, end_of_str, 0, 0);
        assert(delimiter == '|');

        // Construye el `cmd` para la tubería
        cmd = pipecmd(cmd, parse_pipe(start_of_str, end_of_str));
    }

    return cmd;
}


// `parse_exec` realiza el análisis sintáctico de un comando a no ser que la
// expresión comience por un paréntesis, en cuyo caso se llama a `parse_subs`.
//
// `parse_exec` reconoce las redirecciones antes y después del comando.

struct cmd* parse_exec(char** start_of_str, char* end_of_str)
{
    char* start_of_token;
    char* end_of_token;
    int token, argc;
    struct execcmd* cmd;
    struct cmd* ret;

    // ¿Inicio de un bloque?        Si empieza por '(' encot
    if (peek(start_of_str, end_of_str, "("))
        return parse_subs(start_of_str, end_of_str);

    // Si no, lo primero que hay en una línea de órdenes es un comando

    // Construye el `cmd` para el comando
    ret = execcmd();
    cmd = (struct execcmd*) ret;

    // ¿Redirecciones antes del comando?
    ret = parse_redr(ret, start_of_str, end_of_str);

    // Bucle para separar los argumentos de las posibles redirecciones
    argc = 0;
    while (!peek(start_of_str, end_of_str, "|)&;"))
    {
        if ((token = get_token(start_of_str, end_of_str,
                               &start_of_token, &end_of_token)) == 0)
            break;

        // El siguiente token debe ser un argumento porque el bucle
        // para en los delimitadores
        if (token != 'a')
            error("%s: error sintáctico: se esperaba un argumento\n", __func__);

        // Almacena el siguiente argumento reconocido. El primero es
        // el comando
        cmd->argv[argc] = start_of_token;
        cmd->eargv[argc] = end_of_token;
        cmd->argc = ++argc;
        if (argc >= MAX_ARGS)
            panic("%s: demasiados argumentos\n", __func__);

        // ¿Redirecciones después del comando?
        ret = parse_redr(ret, start_of_str, end_of_str);
    }

    // El comando no tiene más parámetros
    cmd->argv[argc] = 0;
    cmd->eargv[argc] = 0;

    return ret;
}


// `parse_subs` realiza el análisis sintáctico de un bloque de órdenes
// delimitadas por paréntesis o `subshell` llamando a `parse_line`.
//
// `parse_subs` reconoce las redirecciones después del bloque de órdenes.

struct cmd* parse_subs(char** start_of_str, char* end_of_str)
{
    int delimiter;
    struct cmd* cmd;
    struct cmd* scmd;

    // Consume el paréntesis de apertura
    if (!peek(start_of_str, end_of_str, "("))
        error("%s: error sintáctico: se esperaba '('", __func__);
    delimiter = get_token(start_of_str, end_of_str, 0, 0);
    assert(delimiter == '(');

    // Realiza el análisis sintáctico hasta el paréntesis de cierre
    scmd = parse_line(start_of_str, end_of_str);

    // Construye el `cmd` para el bloque de órdenes
    cmd = subscmd(scmd);

    // Consume el paréntesis de cierre
    if (!peek(start_of_str, end_of_str, ")"))
        error("%s: error sintáctico: se esperaba ')'", __func__);
    delimiter = get_token(start_of_str, end_of_str, 0, 0);
    assert(delimiter == ')');

    // ¿Redirecciones después del bloque de órdenes?
    cmd = parse_redr(cmd, start_of_str, end_of_str);

    return cmd;
}


// `parse_redr` realiza el análisis sintáctico de órdenes con
// redirecciones si encuentra alguno de los delimitadores de
// redirección ('<' o '>').

struct cmd* parse_redr(struct cmd* cmd, char** start_of_str, char* end_of_str)
{
    int delimiter;
    char* start_of_token;
    char* end_of_token;

    // Si lo siguiente que hay a continuación es delimitador de
    // redirección...
    while (peek(start_of_str, end_of_str, "<>"))
    {
        // Consume el delimitador de redirección
        delimiter = get_token(start_of_str, end_of_str, 0, 0);
        assert(delimiter == '<' || delimiter == '>' || delimiter == '+');

        // El siguiente token tiene que ser el nombre del fichero de la
        // redirección entre `start_of_token` y `end_of_token`.
        if ('a' != get_token(start_of_str, end_of_str, &start_of_token, &end_of_token))
            error("%s: error sintáctico: se esperaba un fichero", __func__);

        // Construye el `cmd` para la redirección
        switch(delimiter)
        {
            case '<':
                cmd = redrcmd(cmd, start_of_token, end_of_token, O_RDONLY, 0, 0);
                break;
            case '>':
                cmd = redrcmd(cmd, start_of_token, end_of_token, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU, 1);     //
                break;
            case '+': // >>
                cmd = redrcmd(cmd, start_of_token, end_of_token, O_RDWR|O_CREAT|O_APPEND, S_IRWXU, 1);
                break;
        }
    }

    return cmd;
}


// Termina en NULL todas las cadenas de las estructuras `cmd`
struct cmd* null_terminate(struct cmd* cmd)
{
    struct execcmd* ecmd;
    struct redrcmd* rcmd;
    struct pipecmd* pcmd;
    struct listcmd* lcmd;
    struct backcmd* bcmd;
    struct subscmd* scmd;
    int i;

    if(cmd == 0)
        return 0;

    switch(cmd->type)
    {
        case EXEC:
            ecmd = (struct execcmd*) cmd;
            for(i = 0; ecmd->argv[i]; i++)
                *ecmd->eargv[i] = 0;
            break;

        case REDR:
            rcmd = (struct redrcmd*) cmd;
            null_terminate(rcmd->cmd);
            *rcmd->efile = 0;
            break;

        case PIPE:
            pcmd = (struct pipecmd*) cmd;
            null_terminate(pcmd->left);
            null_terminate(pcmd->right);
            break;

        case LIST:
            lcmd = (struct listcmd*) cmd;
            null_terminate(lcmd->left);
            null_terminate(lcmd->right);
            break;

        case BACK:
            bcmd = (struct backcmd*) cmd;
            null_terminate(bcmd->cmd);
            break;

        case SUBS:
            scmd = (struct subscmd*) cmd;
            null_terminate(scmd->cmd);
            break;

        case INV:
        default:
            panic("%s: estructura `cmd` desconocida\n", __func__);
    }

    return cmd;
}


/******************************************************************************
 * Funciones para la ejecución de la línea de órdenes
 ******************************************************************************/


void exec_cmd(struct execcmd* ecmd)
{
    assert(ecmd->type == EXEC);
    if (ecmd->argv[0] == 0) exit(EXIT_SUCCESS);
    if (internal_cmd(ecmd) > 0) {
        exec_internal_cmd(ecmd);
    } else {
        execvp(ecmd->argv[0], ecmd->argv);
        panic("no se encontró el comando xd '%s'\n", ecmd->argv[0]);
    }
}

void free_cmd(struct cmd* cmd);


void run_cmd(struct cmd* cmd)
{
    struct execcmd* ecmd;
    struct redrcmd* rcmd;
    struct listcmd* lcmd;
    struct pipecmd* pcmd;
    struct backcmd* bcmd;
    struct subscmd* scmd;
    int p[2];
    int fd;

    pid_t  pid1;
    DPRINTF(DBG_TRACE, "STR\n");

    if(cmd == 0) return;

    switch(cmd->type)
    {
        case EXEC:
            ecmd = (struct execcmd*) cmd;
            if (internal_cmd(ecmd) > 0) {
                exec_internal_cmd(ecmd);
            } else {
                block_SIG_CHLD();
                if ((pid1 = fork_or_panic("fork EXEC")) == 0)
                    exec_cmd(ecmd);
                TRY( waitpid(pid1, NULL, 0) );                        //
                release_SIG_CHLD();
            }
            break;

        case REDR:
            rcmd = (struct redrcmd*) cmd;
            int terminal_fd = dup(rcmd->fd);
            int es_cmd_interno = 0;
            if (rcmd->cmd->type == EXEC)
                es_cmd_interno = internal_cmd((struct execcmd*) rcmd->cmd);
            if(es_cmd_interno > 0) {
                close(rcmd->fd);
                if ((fd = open(rcmd->file, rcmd->flags, rcmd->mode)) < 0) {
                    perror("open");
                    exit(EXIT_FAILURE);
                }
                if (es_cmd_interno == 1) {

                    close(fd);
                    dup2(terminal_fd, fd);
                    close(terminal_fd);

                } else {
                    exec_internal_cmd((struct execcmd*) rcmd->cmd);

                    int error = dup2(terminal_fd, fd);

                    if (error == -1) {
                        fprintf(stderr, "dup2 error \n");
                        exit(EXIT_FAILURE);
                    }
                }
            } else {
                if ((pid1 = fork_or_panic("fork REDR")) == 0) {
                    TRY(close(rcmd->fd));
                    if ((fd = open(rcmd->file, rcmd->flags, rcmd->mode)) < 0) {
                        perror("open");
                        exit(EXIT_FAILURE);
                    }

                    if (rcmd->cmd->type == EXEC) {
                        exec_cmd((struct execcmd *) rcmd->cmd);
                    } else
                        run_cmd(rcmd->cmd);
                    exit(EXIT_SUCCESS);
                }
                if(is_back_process(pid1) == 0) {
                    block_SIG_CHLD();
                    TRY(waitpid(pid1, NULL, 0));
                    release_SIG_CHLD();
                }
            }
            break;

        case LIST:
            lcmd = (struct listcmd*) cmd;
            run_cmd(lcmd->left);
            run_cmd(lcmd->right);
            break;

        case PIPE:
            pcmd = (struct pipecmd*)cmd;
            if (pipe(p) < 0)
            {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
            block_SIG_CHLD();
            // Ejecución del hijo de la izquierda
            if ((pid1 = fork_or_panic("fork PIPE left")) == 0)
            {
                TRY( close(1) );
                TRY( dup(p[1]) );
                TRY( close(p[0]) );
                TRY( close(p[1]) );
                if (pcmd->left->type == EXEC)
                    exec_cmd((struct execcmd*) pcmd->left);
                else
                    run_cmd(pcmd->left);
                exit(EXIT_SUCCESS);
            }

            pid_t pid2;
            int status2;

            // Ejecución del hijo de la derecha
            if ((pid2 =fork_or_panic("fork PIPE right")) == 0)
            {
                TRY( close(0) );
                TRY( dup(p[0]) );
                TRY( close(p[0]) );
                TRY( close(p[1]) );
                if (pcmd->right->type == EXEC)
                    exec_cmd((struct execcmd*) pcmd->right);
                else
                    run_cmd(pcmd->right);
                exit(EXIT_SUCCESS);
            }
            TRY( close(p[0]) );
            TRY( close(p[1]) );

            // Esperar a ambos hijos
            TRY( waitpid(pid1, NULL, 0) );          // TODO FALLA AQUI :S
            TRY( waitpid(pid2, NULL, 0) );
            release_SIG_CHLD();
            break;

        case BACK:
            bcmd = (struct backcmd*)cmd;
            block_SIG_CHLD();
            if ((pid1 = fork_or_panic("fork BACK")) == 0)
            {
                if (bcmd->cmd->type == EXEC)
                    exec_cmd((struct execcmd*) bcmd->cmd);
                else
                    run_cmd(bcmd->cmd);
                exit(EXIT_SUCCESS);
            }

            int contador = 0;
            while ((contador < MAX_PROCS) && (pids_procesos[contador] != -1)) {
                contador++;
            }

            //printf(">%d\n", pid1);

            if (pids_procesos[contador] == -1) {
                pids_procesos[contador] = pid1;
            }
            // printf("> %d\n", pid1);
            print_processid(pid1);
            release_SIG_CHLD();

            break;

        case SUBS:
            scmd = (struct subscmd*) cmd;
            block_SIG_CHLD();
            if ((pid1 = fork_or_panic("fork SUBS")) == 0)
            {
                run_cmd(scmd->cmd);
                exit(EXIT_SUCCESS);
            }
            TRY( waitpid(pid1, NULL, 0) );
            release_SIG_CHLD();
            break;

        case INV:
        default:
            panic("%s: estructura `cmd` desconocida\n", __func__);
    }

    DPRINTF(DBG_TRACE, "END\n");
}


void print_cmd(struct cmd* cmd)
{
    struct execcmd* ecmd;
    struct redrcmd* rcmd;
    struct listcmd* lcmd;
    struct pipecmd* pcmd;
    struct backcmd* bcmd;
    struct subscmd* scmd;

    if(cmd == 0) return;

    switch(cmd->type)
    {
        default:
            panic("%s: estructura `cmd` desconocida\n", __func__);

        case EXEC:
            ecmd = (struct execcmd*) cmd;
            if (ecmd->argv[0] != 0)
                printf("fork( exec( %s ) )", ecmd->argv[0]);
            break;

        case REDR:
            rcmd = (struct redrcmd*) cmd;
            printf("fork( ");
            if (rcmd->cmd->type == EXEC)
                printf("exec ( %s )", ((struct execcmd*) rcmd->cmd)->argv[0]);
            else
                print_cmd(rcmd->cmd);
            printf(" )");
            break;

        case LIST:
            lcmd = (struct listcmd*) cmd;
            print_cmd(lcmd->left);
            printf(" ; ");
            print_cmd(lcmd->right);
            break;

        case PIPE:
            pcmd = (struct pipecmd*) cmd;
            printf("fork( ");
            if (pcmd->left->type == EXEC)
                printf("exec ( %s )", ((struct execcmd*) pcmd->left)->argv[0]);
            else
                print_cmd(pcmd->left);
            printf(" ) => fork( ");
            if (pcmd->right->type == EXEC)
                printf("exec ( %s )", ((struct execcmd*) pcmd->right)->argv[0]);
            else
                print_cmd(pcmd->right);
            printf(" )");
            break;

        case BACK:
            bcmd = (struct backcmd*) cmd;
            printf("fork( ");
            if (bcmd->cmd->type == EXEC)
                printf("exec ( %s )", ((struct execcmd*) bcmd->cmd)->argv[0]);
            else
                print_cmd(bcmd->cmd);
            printf(" )");
            break;

        case SUBS:
            scmd = (struct subscmd*) cmd;
            printf("fork( ");
            print_cmd(scmd->cmd);
            printf(" )");
            break;
    }
}


void free_cmd(struct cmd* cmd)
{
    struct execcmd* ecmd;
    struct redrcmd* rcmd;
    struct listcmd* lcmd;
    struct pipecmd* pcmd;
    struct backcmd* bcmd;
    struct subscmd* scmd;

    if(cmd == 0) return;

    switch(cmd->type)
    {
        case EXEC:
            // free(ecmd);
            break;

        case REDR:
            rcmd = (struct redrcmd*) cmd;
            free_cmd(rcmd->cmd);

            free(rcmd->cmd);
            break;

        case LIST:
            lcmd = (struct listcmd*) cmd;

            free_cmd(lcmd->left);
            free_cmd(lcmd->right);

            free(lcmd->right);
            free(lcmd->left);
            break;

        case PIPE:
            pcmd = (struct pipecmd*) cmd;

            free_cmd(pcmd->left);
            free_cmd(pcmd->right);

            free(pcmd->right);
            free(pcmd->left);
            break;

        case BACK:
            bcmd = (struct backcmd*) cmd;
            free_cmd(bcmd->cmd);
            free(bcmd->cmd);
            break;

        case SUBS:
            scmd = (struct subscmd*) cmd;

            free_cmd(scmd->cmd);

            free(scmd->cmd);
            break;

        case INV:
        default:
            panic("%s: estructura `cmd` desconocida\n", __func__);
    }
}


/******************************************************************************
 * Lectura de la línea de órdenes con la biblioteca libreadline
 ******************************************************************************/


// `get_cmd` muestra un *prompt* y lee lo que el usuario escribe usando la
// biblioteca readline. Ésta permite mantener el historial, utilizar las flechas
// para acceder a las órdenes previas del historial, búsquedas de órdenes, etc.

char* get_cmd()
{
    char* buf;

    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (pw == NULL) {
        perror("getpwuid");
        exit(EXIT_FAILURE);
    }
    char ruta[PATH_MAX];
    if (!getcwd(ruta, PATH_MAX)) {
        perror("FALLO DE RUTA");
        exit(EXIT_FAILURE);
    }
    //run_cwd();
    char * dir_actual = basename(ruta);                                     // TODO: Comprobar si la funcion lanza excepciones
    size_t prompt_size = strlen(pw->pw_name) + strlen(dir_actual) + 4;
    char *prompt;
    prompt = malloc(prompt_size * sizeof(char));
    sprintf(prompt, "%s@%s> ", pw->pw_name, dir_actual);

    // Lee la orden tecleada por el usuario
    buf = readline(prompt);
    free(prompt);

    // Si el usuario ha escrito una orden, almacenarla en la historia.
    if(buf)
        add_history(buf);

    return buf;
}


/******************************************************************************
 * Bucle principal de `simplesh`
 ******************************************************************************/


void help(int argc, char **argv)
{
    info("Usage: %s [-d N] [-h]\n\
         shell simplesh v%s\n\
         Options: \n\
         -d set debug level to N\n\
         -h help\n\n",
         argv[0], VERSION);
}


void parse_args(int argc, char** argv)
{
    int option;

    // Bucle de procesamiento de parámetros
    while((option = getopt(argc, argv, "d:h")) != -1) {
        switch(option) {
            case 'd':
                g_dbg_level = atoi(optarg);
                break;
            case 'h':
            default:
                help(argc, argv);
                exit(EXIT_SUCCESS);
                break;
        }
    }
}


int main(int argc, char** argv)
{

    unsetenv("OLDPWD");

    // Aqui debemos inicializar los pids
    memset(pids_procesos,-1,(sizeof (*pids_procesos)*MAX_PROCS));
    // memset(secondProcess, -1, MAXBUF);
    for (int i = 0; i < MAX_PROCS; i++){
        pids_procesos[i]= -1;
    }

    sigemptyset(&signal_child);
    sigaddset(&signal_child, SIGCHLD);

    //Bloquear la señal SIGINIT
    sigset_t blocked_sig_int;
    sigemptyset(&blocked_sig_int);
    sigaddset(&blocked_sig_int, SIGINT);
    if (sigprocmask(SIG_BLOCK, &blocked_sig_int, NULL) == -1){
        perror(" SIGPROCMASK: SIGINIT");
        exit(EXIT_FAILURE);
    }

    //Bloquear la seña SIGQUIT
    sigset_t ignore_sig_int;
    sigemptyset(&ignore_sig_int);
    sigaddset(&ignore_sig_int, SIGQUIT);
    if (sigprocmask(SIG_BLOCK, &ignore_sig_int, NULL) == -1){
        perror(" SIGPROCMASK: SIGQUIT");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror(0);
        exit(EXIT_FAILURE);
    }

    char* buf;
    struct cmd* cmd;
    parse_args(argc, argv);
    DPRINTF(DBG_TRACE, "STR\n");

    // Bucle de lectura y ejecución de órdenes
    while ((buf = get_cmd()) != NULL)
    {
        // Realiza el análisis sintáctico de la línea de órdenes
        cmd = parse_cmd(buf);

        // Termina en `NULL` todas las cadenas de las estructuras `cmd`
        null_terminate(cmd);

        DBLOCK(DBG_CMD, {
            info("%s:%d:%s: print_cmd: ",
                 __FILE__, __LINE__, __func__);
            print_cmd(cmd); printf("\n"); fflush(NULL); } );

        // Ejecuta la línea de órdenes
        run_cmd(cmd);

        // Libera la memoria de las estructuras `cmd`
        free_cmd(cmd);
        free(cmd);
        // Libera la memoria de la línea de órdenes
        free(buf);

    }

    DPRINTF(DBG_TRACE, "END\n");
    return 0;
}
