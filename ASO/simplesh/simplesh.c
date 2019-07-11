/*
 * Shell `simplesh` (basado en el shell de xv6)
 *
 * Ampliación de Sistemas Operativos
 * Departamento de Ingeniería y Tecnología de Computadores
 * Facultad de Informática de la Universidad de Murcia
 *
 * Alumnos: MORENO GUILLAMON, MANUEL (G2)
 *
 * Convocatoria: JULIO
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
#define MAX_PROCS 8
#define MAX_CMD_BUFF_LENGTH 4096

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

struct cmd * parse_cmd(char * start_of_cmd);
void free_cmd(struct cmd * cmd);
struct cmd *null_terminate(struct cmd * cmd);
void run_cmd(struct cmd * cmd);
       
/**
 * COMANDO INTERNO cwd (idéntico al pwd de bash)
 *
 * FUNCIONALIDAD: Muestra el directorio actual de trabajo
*/

void _getcwd(char *buf){
  if (!getcwd(buf, PATH_MAX)) {
    perror("Error de getcwd()\n");
    exit(EXIT_FAILURE);
  }
}

void run_cwd() {
    char ruta[PATH_MAX];
    _getcwd(ruta);
    printf("cwd: %s\n", ruta);
}

/**
 * COMANDO INTERNO exit (idéntico al exit de bash)
 *
 * FUNCIONALIDAD: termina el proceso que lo invoca
*/

void run_exit(struct execcmd * ecmd) {
    // LIMPIAR LA MEMORIA
    free(ecmd);
    exit(EXIT_SUCCESS);
}

/**
 * COMANDO INTERNO cd (idéntico al cd de bash)
 *
 * FUNCIONALIDAD: cambia el directorio actual a otro dado
*/

void _chdir(const char *path){
  if (chdir(path) < 0)
    perror("Error de chdir()\n");
}

void _setenv(const char *name, const char *value, int overwrite){
  if (setenv(name, value, overwrite) < 0)
    perror("Error de setenv()\n");
}

char * get_cwd_str(){
  char * cwd = malloc(sizeof(char)*PATH_MAX);
  _getcwd(cwd);

  return cwd;
}

void run_cd(struct execcmd* ecmd){

  char * path = ecmd->argv[1];
  char * pathActual = get_cwd_str();


  // Si la llamada contiene el parámetro "-"
  if (path != NULL && !strcmp(path, "-")){

    // Si $OLDPWD esta definido:
    if (getenv("OLDPWD") != NULL)
      _chdir(getenv("OLDPWD"));
    else{
      fprintf(stderr,"run_cd: Variable OLDPWD no definida\n");
      _setenv("OLDPWD", getenv("HOME"), 1);
    }
  // Si la llamada no contiene el parámetro "-"
  }else{

    if (path != NULL){
      if(chdir(path) == -1){
        fprintf(stderr, "run_cd: No existe el directorio '%s'\n",path );
      }
    }else {
      path = getenv("HOME");
      _chdir(path);
      _setenv("OLDPWD", path, 1);
    }
  }

  //guardamos el path actual
  if (pathActual){
    setenv("OLDPWD", pathActual, 1);
    free(pathActual);
  }

}

/**
 * COMANDO INTERNO hd
 *
 * FUNCIONALIDAD: muestra las -l [lineas] o -b [bytes] de un fichero dado o lo que reciba
 * por le entrada estandar usando un buffer de tamamño -t [bytes]
*/

#define HD_DEFAULT_LINES 3
#define DEFAULT_BSIZE 1024
#define HD_MAX_BSIZE 1048576
#define HD_NO_FLAGS 0
#define HD_LINES_FLAG 1         
#define HD_BYTES_FLAG 2  

//DUPLICADO DE LA USADA EN SRC PARA AÑADIRLE FUNCIONALIDAD SIN ROMPER LA DE SRC
int readline_from_file_hd(char ** actualcmd, int fd) {

    char buffer[MAX_CMD_BUFF_LENGTH] = {0};
    int i = 0;

    int n = read(fd, &buffer[i], 1);
    while(n > 0) {
        if(buffer[i] == '\n') {
            buffer[i] = 0;
            *actualcmd = malloc(sizeof(char) * (i+1));
            memcpy(*actualcmd, buffer, i+1);
            return i;
        }
        else {
            i++;
            n = read(fd, &buffer[i], 1);
        }
    }
    if(i > 0) {
        buffer[i+1] = 0;
        *actualcmd = malloc(sizeof(char) * (i+2));
        memcpy(*actualcmd, buffer, i+2);
        return i;
    }
    return 0;
}

void print_hd_help() {
    printf("Uso: hd [-l NLINES] [-b NBYTES] [-t BSIZE] [FILE1] [FILE2]...\n"
                   "\tOpciones:\n"
                   "\t-l NLINES Numero maximo de lineas a mostrar.\n"
                   "\t-b NBYTES Numero maximo de bytes a mostrar.\n"
                   "\t-t BSIZE Tamano en bytes de los bloques leidos de [FILEn] o stdin.\n"
                   "\t-h help\n");
}

void hd_lines(int bsize, int nlines, int fd) {
    int contlines = 0;
    char * line;
    int n;
    while((n = readline_from_file_hd(&line, fd)) > 0 && contlines < nlines){
        write(STDOUT_FILENO, line, n);
        write(STDOUT_FILENO, "\n", 1);
        contlines++;
    }
}

void hd_bytes(int bsize, int nbytes, int fd) {
    int bytes_readed = 0;
    int writed = 0;
    char * line;
    int n;
    while((n = readline_from_file_hd(&line, fd)) > 0 && bytes_readed < nbytes) {

        bytes_readed += n;
        if(bytes_readed < nbytes) {
            write(STDOUT_FILENO, line, n);
            write(STDOUT_FILENO, "\n", 1);
            writed += n;
        } 
        else {
            write(STDOUT_FILENO, line, nbytes-writed);
        }
    }
}

void hd(int bsize, int flag, int arg, int fd) {
    switch (flag)
    {
    case HD_LINES_FLAG:
        hd_lines(bsize, arg, fd);
        break;
    case HD_BYTES_FLAG:
        hd_bytes(bsize, arg, fd);
        break;
    
    default:
        break;
    }
}

void run_hd(struct execcmd * ecmd) {
    int flag = HD_NO_FLAGS;          
    int tvalue = DEFAULT_BSIZE;          
    int lb_value = HD_DEFAULT_LINES;
    optind = 1;
    int opt;
    while ((opt = getopt(ecmd->argc, ecmd->argv, "l:b:t:h")) != -1) {
        
        switch (opt) {
            case 'l':
                lb_value = atoi(optarg);
                //printf("lb_value: %d\n", lb_value);
                flag |= HD_LINES_FLAG;
                //printf("flag: %d\n", flag);
                break;
            case 'b':
                lb_value = atoi(optarg);
                flag |= HD_BYTES_FLAG;
                break;
            case 't':
                tvalue = atoi(optarg);
                break;
            case 'h':
                print_hd_help();
                break;
            case '?':
                if(optopt == 'b' || optopt == 'l' || optopt == 't') {
                    fprintf(stderr, "Option '%c' requies an argument.\n", optopt);
                    return;
                }
                break;
            default:
                fprintf(stderr, "Usage: %s [-f] [-n NUM]\n", ecmd->argv[0]);
                return;
                break;
        }
    }

    if(flag == HD_NO_FLAGS)
        flag = HD_LINES_FLAG;

    if( flag == (HD_BYTES_FLAG | HD_LINES_FLAG) ) {
        fprintf(stderr, "hd: Opciones incompatibles\n");
        return;
    }

    if ( tvalue <= 0 || tvalue >= HD_MAX_BSIZE) {
        printf("hd: Opción no válida\n");
        return;
    }
    // si hay argumentos pertenecientes a ficheros
    if(optind == ecmd->argc) {
        hd(tvalue, flag, lb_value, STDIN_FILENO);
    } else {
        
        for (int i = optind; i < ecmd->argc; ++i) {
            int fd = open(ecmd->argv[i], O_RDONLY);
            if (fd < 0)
                printf("hd: No se encontró el archivo '%s'\n", ecmd->argv[i]);
            else{
                hd(tvalue, flag, lb_value, fd);
                close(fd);
            }
        }
    }
}


/**
 * COMANDO INTERNO src (similar al "source" o "." de linux)
 *
 * FUNCIONALIDAD: Ejecuta los comandos que se encuentran dentro del fichero que se le pasa como parametro
 * o se redirecciona desde la salida estandard de el anterior comando mediante tuberias.
*/

int readline_from_file(char ** actualcmd, int fd) {

    char buffer[MAX_CMD_BUFF_LENGTH] = {0};
    int i = 0;

    int n = read(fd, &buffer[i], 1);
    while(n > 0) {
        if(buffer[i] == '\n') {
            buffer[i] = 0;
            *actualcmd = malloc(sizeof(char) * (i+1));
            memcpy(*actualcmd, buffer, i+1);
            return i;
        }
        else {
            i++;
            n = read(fd, &buffer[i], 1);
        }
    }
    if(i > 0) {
        buffer[i+1] = 0;
        *actualcmd = malloc(sizeof(char) * (i+2));
        memcpy(*actualcmd, buffer, i+2);
        return i;
    }
    return 0;
}

void src_from_file(int fd, char commentchar) {
    char  * linea = NULL;
    struct cmd * cmd = NULL;
    int bytesreaded;

    while((bytesreaded = readline_from_file( &linea , fd )) > 0) {
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

void print_src_help()
{
    printf("Uso: src [-d DELIM] [FILE 1] [FILE 2]\n");
    printf("\tOptions:\n");
    printf("\t-d DELIM: initial commentary character\n");
    printf("\t-h: show command functioning\n");
}


void run_src(struct execcmd * ecmd)
{
    optind = 1;
    int c;
    char * dvalue = "\%";
    while((c = getopt(ecmd->argc, ecmd->argv, "hd:")) != -1) {
        switch(c) {
            case 'h':
                print_src_help();
                return;
            case 'd':
                if(optarg[1] != 0) {
                    fprintf(stderr, "src: Opcion no válida.\n");
                    return;
                } else {
                    dvalue = optarg;
                }
                break;
            case '?':
                if(optopt == 'd') {
                    fprintf(stderr, "Option '%c' requies an argument.\n", optopt);
                    return;
                }
                break;
            default:
                fprintf(stderr, "src: Invalid option.\n");
                return;
                break;
        }
    }
    //si no tiene ningun fichero leemos de la entrada standard
    if(optind == ecmd->argc) {
        src_from_file(STDIN_FILENO, dvalue[0]);
    } else {
        for(int i = optind; i<ecmd->argc; i++) {
            int fd = open(ecmd->argv[i], O_RDONLY);
            if(fd < 0) {
                fprintf(stderr, "src: No se encontró el archivo '%s'\n", ecmd->argv[i]);
            }
            else {
                src_from_file(fd, dvalue[0]);
                close(fd);
            }
        }
    }
}


/******************************************************************************
 * SEÑALES Y PROCESOS EN SEGUNDO PLANO
 ******************************************************************************/

int backpids[MAX_PROCS] = {-1};
int backcounter = 0;
int status = 0; // termination child status
static sigset_t signal_child;

/**
 * Esta función se utiliza para bloquear las señal signal_child
 */
void block_SIG_CHLD() {
    if ( sigprocmask ( SIG_BLOCK , &signal_child , NULL ) == -1) {
        perror ( " sigprocmask " ) ;
        exit ( EXIT_FAILURE ) ;
    }
}

void release_SIG_CHLD() {
    if ( sigprocmask ( SIG_UNBLOCK , &signal_child , NULL ) == -1) {
        perror ( " sigprocmask " ) ;
        exit ( EXIT_FAILURE ) ;
    }
}

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
void itoa(int value, char *str)
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
        if(pid == backpids[i]) return 0;
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
    itoa(pid, pid_str);
    write(STDOUT_FILENO, pid_str,longitud);

    char * barra2 = "]\n";
    write(STDOUT_FILENO, barra2, 2);
}

void handle_sigchld(int signal) {
    int saved_errno = errno;
    int pid;
    // -1: for arbitrary child (not waiting any concrete child)
    // 0: 
    // WNOHANG: for not blocking parent proccess
    while ((pid = waitpid((pid_t)(-1), &status, WNOHANG)) > 0) {
        for(int i = 0; i < MAX_PROCS; i++){
            if(pid == backpids[i]){
                backpids[i] = -1;
                break;
            }
        }
        print_processid(pid);
    }
    errno = saved_errno;
}

/**
 * COMANDO INTERNO bjobs (similar al "source" o "." de linux)
 *
 * FUNCIONALIDAD: Ejecuta los comandos que se encuentran dentro del fichero que se le pasa como parametro
 * o se redirecciona desde la salida estandard de el anterior comando mediante tuberias.
*/


void print_bjobs_help() {
    printf("Uso: bjobs [-s] [-c] [-h]\n"
                   "\tOpciones:\n"
                   "\t-s Suspende todos los procesos en segundo plano.\n"
                   "\t-b Reanuda todos los procesos en segundo plano.\n"
                   "\t-h help\n");
}

void run_bjobs(struct execcmd * ecmd) {
    int opt;
    optind = 1;
    int hay_args = 0;
    while ((opt = getopt(ecmd->argc, ecmd->argv, "hsc")) != -1) {
        switch (opt) {
            case 's':
                if (hay_args > 0) {
                    fprintf(stderr, "bjobs: opciones no compatibles.\n");
                } else {
                    for (int i = 0; i < MAX_PROCS; ++i) {
                        if (is_back_process(backpids[i]) == 0) {
                            kill(backpids[i], SIGSTOP);
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
                        if (is_back_process(backpids[i]) == 0) {
                            kill(backpids[i], SIGCONT);
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
                fprintf(stderr, "Usage: %s [-f] [-n NUM]\n", ecmd->argv[0]);
                break;
        }
    }
    if (hay_args == 0) {
        for (int i = 0; i < MAX_PROCS; ++i) {               
            if(backpids[i] != -1)
                printf("[%d]\n",backpids[i]);
        }
    }
}

/**
 * Funciones auxiliares
 */

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

/******************************************************************************
 * Funciones para realizar el análisis sintáctico de la línea de órdenes
 ******************************************************************************/

// Delimitadores
static const char WHITESPACE[] = " \t\r\n\v";
// Caracteres especiales
static const char SYMBOLS[] = "<|>&;()";

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

bool internal_cmd(struct execcmd* ecmd){

    if (strcmp(ecmd->argv[0], "exit") == 0)
    {
        run_exit(ecmd);
        return true;
    }

	if (strcmp(ecmd->argv[0], "cwd") == 0) 
    {
        run_cwd();
        return true;
    }

    if (strcmp(ecmd->argv[0], "cd") == 0) 
    {
      run_cd(ecmd);
      return true;
    }

    if (strcmp(ecmd->argv[0], "hd") == 0) 
    {
        run_hd(ecmd);
        return true;
    }

    if (strcmp(ecmd->argv[0], "bjobs") == 0) 
    {
        run_bjobs(ecmd);
        return true;
    }

    if (strcmp(ecmd->argv[0], "src") == 0) 
    {
        run_src(ecmd);
        return true;
    }

  return false;

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
    

    if (!internal_cmd(ecmd)) 
    {
        execvp(ecmd->argv[0], ecmd->argv);
        panic("no se encontró el comando xd '%s'\n", ecmd->argv[0]);
    }

    free(ecmd);
    
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
    pid_t pid1 = 0;
    pid_t pid2 = 0;
    
    DPRINTF(DBG_TRACE, "STR\n");

    if(cmd == 0) return;

    switch(cmd->type)
    {
        case EXEC:

            ecmd = (struct execcmd*) cmd;
            /**
            si no es un comando interno:
            1. Crea un hijo
            2. Copia el codigo padre-hijo con el exec
            3. El padre espera al hijo

            si es un comando interno, directamente en la comprobación se realiza el
            run correspondiente.
            **/
            if (ecmd->argv[0] != NULL){
                if (!internal_cmd(ecmd))
                {   block_SIG_CHLD();
                    if ((pid1 = fork_or_panic("fork EXEC")) == 0){
                        exec_cmd(ecmd);
                    }
                    TRY( waitpid(pid1,&status,0) );
                    release_SIG_CHLD();
                }
            }
            break;

        case REDR:
            rcmd = (struct redrcmd*) cmd;

                if ((pid1 = fork_or_panic("fork REDR")) == 0)
                {
                    TRY( close(rcmd->fd) );
                    if ((fd = open(rcmd->file, rcmd->flags, rcmd->mode)) < 0)
                    {
                        perror("open");
                        exit(EXIT_FAILURE);
                    }
                  if(!internal_cmd((struct execcmd*) rcmd->cmd)){
                    if (rcmd->cmd->type == EXEC)
                        exec_cmd((struct execcmd*) rcmd->cmd);
                    else
                        run_cmd(rcmd->cmd);


                      }
                      exit(EXIT_SUCCESS);

                }
                TRY( waitpid(pid1,&status,0) );

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

            // Ejecución del hijo de la derecha
            if ((pid2 =fork_or_panic("fork PIPE right")) == 0)
            {
                TRY( close(0) );
                TRY( dup(p[0]) );
                TRY( close(p[0]) );
                TRY( close(p[1]) );
                if (!internal_cmd((struct execcmd*) pcmd->right)) {
                    if (pcmd->right->type == EXEC)
                        exec_cmd((struct execcmd*) pcmd->right);
                    else
                        run_cmd(pcmd->right);
                }
                exit(EXIT_SUCCESS);
            }
            TRY( close(p[0]) );
            TRY( close(p[1]) );

            TRY( waitpid(pid1, &status, 0) );
            TRY( waitpid(pid2, &status, 0) );
            release_SIG_CHLD();
            break;

        case BACK:
            bcmd = (struct backcmd*)cmd;
            if ((pid1 = fork_or_panic("fork BACK")) == 0)
            {
                if (bcmd->cmd->type == EXEC)
                    exec_cmd((struct execcmd*) bcmd->cmd);
                else
                    run_cmd(bcmd->cmd);
                exit(EXIT_SUCCESS);
            }

            
            // lookin for a gap where store the child pid
            while ((backcounter < MAX_PROCS) && (backpids[backcounter % MAX_PROCS] != -1)) {
                backcounter++;
            }

            if (backpids[backcounter % MAX_PROCS] == -1) {
                backpids[backcounter % MAX_PROCS] = pid1;
            }

            print_processid(pid1);

            break;

        case SUBS:
            scmd = (struct subscmd*) cmd;
            block_SIG_CHLD();
            if ((pid1 = fork_or_panic("fork SUBS")) == 0)
            {
                run_cmd(scmd->cmd);
                exit(EXIT_SUCCESS);
            }
            TRY( waitpid(pid1, &status, 0) );
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
            // ecmd = (struct execcmd*) cmd;
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
    sprintf(prompt, "%s@%s> ", "alumno", dir_actual);
    //sprintf(prompt, "%s@%s> ", pw->pw_name, dir_actual);
    //sprintf(prompt, "simplesh> ");

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

    //Ignorar la seña SIGQUIT
    struct sigaction newactionsigquit;
    sigemptyset(&newactionsigquit.sa_mask);
    newactionsigquit.sa_handler = SIG_IGN;
    newactionsigquit.sa_flags = 0;
    sigaction(SIGQUIT, &newactionsigquit, NULL);

    //Controlar que los proceso hijo no interfieran
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &handle_sigchld;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
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

        run_cmd(cmd);
        free_cmd(cmd);
        free(cmd);
        free(buf);

    }

    DPRINTF(DBG_TRACE, "END\n");
    return 0;
}
