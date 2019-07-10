#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <error.h>
#include <errno.h>

#include <sys/sendfile.h>
#include <sys/stat.h>

#define VERSION		24
#define BUFSIZE		8096
#define ERROR		42
#define LOG			44
#define PROHIBIDO	403
#define NOENCONTRADO	404
#define NUM_HEADERS 20
#define WWW_PATH "../www"
#define NUM_EXTENSION 11
#define EMAIL "manu%40um.es"
#define COOKIE_MAX_AGE 10
#define TIMEOUT 15
#define REQ_MAX 3
#define MAX_COOKIES 10

struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{"gif", "image/gif" },
	{"jpg", "image/jpg" },
	{"jpeg","image/jpeg"},
	{"png", "image/png" },
	{"ico", "image/ico" },
	{"zip", "image/zip" },
	{"gz",  "image/gz"  },
	{"tar", "image/tar" },
	{"htm", "text/html" },
	{"html","text/html" },
	{0,0} };

void debug(int log_message_type, char *message, char *additional_info, int socket_fd)
{
	int fd ;
	char logbuffer[BUFSIZE*2];
	
	switch (log_message_type) {
		case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",message, additional_info, errno,getpid());
			break;
		case PROHIBIDO:
			// Enviar como respuesta 403 Forbidden
			(void)sprintf(logbuffer,"FORBIDDEN: %s:%s",message, additional_info);
			break;
		case NOENCONTRADO:
			// Enviar como respuesta 404 Not Found
			(void)sprintf(logbuffer,"NOT FOUND: %s:%s",message, additional_info);
			break;
		case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",message, additional_info, socket_fd); break;
	}

	if((fd = open("webserver.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		(void)write(fd,logbuffer,strlen(logbuffer));
		(void)write(fd,"\n",1);
		(void)close(fd);
	}
	if(log_message_type == ERROR || log_message_type == NOENCONTRADO || log_message_type == PROHIBIDO) exit(3);
}

struct cookie_list {
	char * key;
	char * value;
	struct cookie_list * sig;
};

struct http_header {
	char * key;
	char * value;
};

void free_header(struct http_header * header) {
	free(header->key);
	free(header->value);
}

struct http_request {
	char * method;
	char * url;
	char * http_version;
	struct http_header header[NUM_HEADERS];
	int num_headers;
	char * body;
};

void free_http_request(struct http_request * req) {
	free(req->method);
	free(req->url);
	free(req->http_version);
	free(req->body);
	for(int i = 0; i<req->num_headers; i++)
		free_header(&req->header[i]);
}

struct http_response {
	char * http_version;
	char * status_code;
	char * status_string;
	struct http_header header[NUM_HEADERS];
	int next_header;
};

/**
 * @brief Given a string the function look up the next delimiter
 * returning a copy of the string rigth before that delimiter
 * and moving the src_str pointer to the next character right after delimiter
 * NULL if there is none.
 * 
 * @param src_str A pointer to string we are looking in
 * @param delim Delimiter we are looking for
 * @return char* String right after the found delimiter
 */
char * getToken(char ** src_str, int delim)
{
    char * str_aux;
    char * end_str = strchr(*src_str, delim);
	if(end_str != NULL)
	{
		size_t token_len = end_str - *src_str;
		str_aux = malloc(sizeof(char) * token_len);
		strncpy(str_aux, *src_str, token_len);
		str_aux[token_len] = 0;
		*src_str = end_str+1;
		return str_aux;
	}
	return NULL;
}

char * get_file_extension(char * file)
{
	char * file_extension = strrchr(file, '.')+1;
	for(int i = 0; i<NUM_EXTENSION; i++)
	{
		if(!strcmp(extensions[i].ext, file_extension))
			return extensions[i].filetype;
	}
	return 0;
}

struct http_response * create_response(const char * code, const char * string)
{
	struct http_response * response = malloc(sizeof(struct http_response));
	response->http_version = strdup("HTTP/1.1");
	response->status_code = strdup(code);
	response->status_string = strdup(string);
	response->next_header = 0;
	return response;
}

void response_set_header(struct http_response * response, const char * key, const char * value)
{
	int found = 0;
	for(int i = 0; i<response->next_header && !found; i++)
	{
		if(!strcmp(response->header[i].key, key))
		{
			found = 1;
			free(response->header[i].value);
			response->header[i].value = strdup(value);
		}
	}
	if(!found)
	{
		response->header[response->next_header].key = strdup(key);
		response->header[response->next_header].value = strdup(value);
		response->next_header++;
	}
}

void response_to_string(char * buffer, const struct http_response * response) 
{
	int count = 0;
	count += sprintf(buffer,"%s %s %s\r\n", 
	response->http_version, 
	response->status_code, 
	response->status_string);
	for(int i = 0; i< response->next_header; i++)
	{
		count += sprintf(buffer+count,"%s: %s\r\n", response->header[i].key, response->header[i].value);
	}
	count += sprintf(buffer + count, "\r\n");
	buffer[count] = 0;
}

char * get_header_value(struct http_request * request, const char * header)
{
	for(int i = 0; i<NUM_HEADERS; i++)
	{
		if(strcmp(request->header[i].key, header))
			return request->header[i].value;
	}
	return NULL;
}

struct http_request * parse_request(char * buffer)
{
	struct http_request * request = malloc(sizeof(struct http_request));
	request->method = NULL;
	request->url = NULL;
	request->http_version = NULL;

	char * offset = buffer;
	request->method = getToken(&offset, ' ');
	request->url = getToken(&offset, ' ');
	request->http_version = getToken(&offset, '\r');
	offset++; //jump \n

	// Es alguno nulo?
	if(request->method == NULL || request->url == NULL || request->http_version == NULL)
		return NULL;
	// Es GET y solo GET (de momento solo soportamos GET)
	if(strcmp(request->method, "GET"))
		return NULL;

	// Contiene la url algun doble punto?
	char * dot;
	if((dot = strchr(request->url, '.')))
		if(*(++dot) == '.')
			return NULL;

	// // Es HTTP/1.1 y solo HTTP/1.1
	if(strcmp(request->http_version, "HTTP/1.1"))
		return NULL;
	
	
	char * token = getToken(&offset, ':'); //get first keyheader
	if(token != NULL) 
	{
		int header_cont = 0;
		while(token != NULL && strlen(token) > 0)
		{
			request->header[header_cont].key = token;
			offset++; //jump space between header key and value

			request->header[header_cont].value = getToken(&offset, '\r');
			offset++; //jump \n terminator
			
			header_cont++;
			token = getToken(&offset,':'); //get next key header
		}
		request->num_headers = header_cont;
		offset += 2; //jump last \r\n pointing to body's first byte
		int bodyLen = atoi(get_header_value(request, "Content-Length"));
		if(bodyLen > 0)
			strncpy(request->body, offset, bodyLen+1); //+1 for copying null terminator 
		return request;
	}
	return request;
}

char * get_request_path(struct http_request * req) {
	char * urldup = strdup(req->url);
	char * offset;
	if(offset = strchr(urldup, '?')) {
		*offset = 0;
		return strdup(urldup);
	}
	return strdup(urldup);
}

char * get_request_query_params(struct http_request * req) {
	char * offset;
	if(offset = strchr(req->url, '?')) {
		return strdup(offset+1);
	}
	return NULL;
}

struct cookie_list * get_cookies(const struct http_request * req)
{
	struct cookie_list * cookies = malloc(sizeof(struct cookie_list));
	struct cookie_list * lista_it = cookies;
	char * offset;
	lista_it->key = NULL;
	lista_it->value = NULL;
	for(int i = 0; i<req->num_headers; i++)
	{
		if(!strcmp(req->header[i].key, "Cookie"))
		{
			offset = req->header[i].value;
			lista_it->key = getToken(&offset, '=');
			lista_it->value = strdup(offset);
			lista_it->sig = malloc(sizeof(struct cookie_list));
			lista_it = lista_it->sig;
		}
	}
	return cookies;
}

void free_cookies(struct cookie_list * cookies) {
	if(cookies->sig != NULL) 
		free_cookies(cookies->sig);

	free(cookies->key);
	free(cookies->value);
	free(cookies);
}

void send_response_str(char * state_code, char * state_text, char * body, int descriptorFichero) {
	char content_length[20];
	struct http_response * response;
	char response_str[BUFSIZE] = {0};
	char keep_alive_value[100] = {0};

	sprintf(content_length,"%ld", strlen(body));

	response = create_response(state_code, state_text);
	response_set_header(response, "Server", "web_sstt");
	response_set_header(response, "Content-Type", "text/html");
	response_set_header(response, "Connection", "keep-alive");
	response_set_header(response, "Content-Length", content_length);

	response_to_string(response_str,response);
	write(descriptorFichero, response_str, strlen(response_str));
	write(descriptorFichero, body, strlen(body));
	free(response);
}

void send_response_file(char * state_code, char * state_text, int req_fd, char * ext, int descriptorFichero, int cookie_counter) {
	struct http_response * response;
	char content_length[20];
	char response_str[BUFSIZE] = {0};
	char cookie_value_str[100];
	char keep_alive_value[100] = {0};
	struct stat file_stat;
	off_t offset = 0;

	fstat(req_fd, &file_stat);
	sprintf(content_length, "%ld", file_stat.st_size);
	sprintf(cookie_value_str, "cookie_counter=%d; Max-Age=%d", cookie_counter, COOKIE_MAX_AGE);

	response = create_response(state_code, state_text);
	response_set_header(response, "Server", "web_sstt");
	response_set_header(response, "Content-Type", ext);
	response_set_header(response, "Connection", "keep-alive");
	response_set_header(response, "Set-Cookie", cookie_value_str);
	response_set_header(response, "Content-Length", content_length);

	response_to_string(response_str,response);
	write(descriptorFichero, response_str, strlen(response_str));
	sendfile(descriptorFichero, req_fd, &offset, file_stat.st_size);
	free(response);
}

void process_web_request(int descriptorFichero)
{
	debug(LOG,"request","Ha llegado una peticion",descriptorFichero);
	int bytes_readed = -1; 
	int connection = -1;
	int num_req = 0;
	struct http_request * request;

	fd_set rfds;
	struct timeval tv; 
	int retval = 1;
	FD_ZERO(&rfds); 
	FD_SET(descriptorFichero, &rfds); 
	tv.tv_sec = TIMEOUT; 
	tv.tv_usec = 0;

	//mientras no se cierre la conexion
	while(connection && (num_req < REQ_MAX)) 
	{
		tv.tv_sec = TIMEOUT;
		retval = select(descriptorFichero+1, &rfds, NULL, NULL, &tv);
		if(retval) 
		{
			num_req++;
			char buffer[BUFSIZE] = {0};
			bytes_readed = read(descriptorFichero, buffer, BUFSIZE);

			if(bytes_readed == -1)
				debug(LOG,"Socket","Error en la lectura del socket",descriptorFichero);

			else if (bytes_readed > 0) {
				int cookie_counter;
				char full_path[200];
				int requested_file_fd;

				buffer[bytes_readed] = 0;
				request = parse_request(buffer);

				if(request != NULL) 
				{
					struct cookie_list * cookies = get_cookies(request);
					if(cookies->key != NULL)
					{
						cookie_counter = atoi(cookies->value);
						cookie_counter++;
					} 
					else 
						cookie_counter = 1;

					if(cookie_counter >= MAX_COOKIES)
					{
						send_response_str("420", "too many requests", "<h1>420 Too Many Requests: Wait two minutes</h1>", 
						descriptorFichero);
					}
					else 
					{
						if(!strcmp(request->method, "GET"))
						{
							// extraemos el path y los params si los hubiera
							char * path = get_request_path(request);
							char * query_params = get_request_query_params(request);

							if(!strcmp(path, "/checkmail")) 
							{
								//asumiendo que siempre va a llevar una query este path extremos el keyvalue
								//get_value(query_params)
								char * offsetAux = query_params;
								char * key = getToken(&offsetAux, '=');
								char * value = strdup(offsetAux);

								if(!strcmp(EMAIL, value)) 
								{
									send_response_str("200", "OK", "<h1>EMAIL CORRECTO</h1>", descriptorFichero);
								}
								else 
								{
									send_response_str("200", "OK", "<h1>EMAIL INCORRECTO</h1>", descriptorFichero);
								}
							}
							else
							{
								//build_fullpath()
								sprintf(full_path,"%s%s", WWW_PATH, path);
								requested_file_fd = open(full_path, O_RDONLY);			

								if(requested_file_fd != -1) 
								{
									//no me convence mucho lo de pasarle la extension y el contador de cookies
									//versión futura pasandole la lista de cooquies de la request y el full_path?
									send_response_file("200", "OK", requested_file_fd, get_file_extension(full_path),
									descriptorFichero, cookie_counter);
									free_cookies(cookies);
									close(requested_file_fd);
								} 
								else 
								{
									send_response_str("404", "NOT FOUND", "<h1>404 NOT FOUND</h1>", descriptorFichero);
								}
							}
						}
					}
				}
				else {
					send_response_str("400", "BAD REQUEST", "<h1>BAD REQUEST</h1>\n", descriptorFichero);
					connection = close(descriptorFichero);
					debug(LOG,"conexión","bad request",descriptorFichero);
				}	
			}
		}
		else {
			connection = close(descriptorFichero);
			debug(LOG,"conexión","cerrada",descriptorFichero);
		}
		free_http_request(request);
	}
	exit(1);
}

int main(int argc, char **argv)
{
	int i, port, pid, listenfd, socketfd;
	socklen_t length;
	static struct sockaddr_in cli_addr;		// static = Inicializado con ceros
	static struct sockaddr_in serv_addr;	// static = Inicializado con ceros
	
	//  Argumentos que se esperan:
	//
	//	argv[1]
	//	En el primer argumento del programa se espera el puerto en el que el servidor escuchara
	//
	//  argv[2]
	//  En el segundo argumento del programa se espera el directorio en el que se encuentran los ficheros del servidor
	//
	//  Verficiar que los argumentos que se pasan al iniciar el programa son los esperados
	//

	//
	//  Verficiar que el directorio escogido es apto. Que no es un directorio del sistema y que se tienen
	//  permisos para ser usado
	//

	if(chdir(argv[2]) == -1){ 
		(void)printf("ERROR: No se puede cambiar de directorio %s\n",argv[2]);
		exit(4);
	}
	// Hacemos que el proceso sea un demonio sin hijos zombies
	if(fork() != 0)
		return 0; // El proceso padre devuelve un OK al shell

	(void)signal(SIGCHLD, SIG_IGN); // Ignoramos a los hijos
	(void)signal(SIGHUP, SIG_IGN); // Ignoramos cuelgues
	
	debug(LOG,"web server starting...", argv[1] ,getpid());
	
	/* setup the network socket */
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
		debug(ERROR, "system call","socket",0);
	
	port = atoi(argv[1]);
	
	if(port < 0 || port >60000)
		debug(ERROR,"Puerto invalido, prueba un puerto de 1 a 60000",argv[1],0);
	
	/*Se crea una estructura para la información IP y puerto donde escucha el servidor*/
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); /*Escucha en cualquier IP disponible*/
	serv_addr.sin_port = htons(port); /*... en el puerto port especificado como parámetro*/
	
	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
		debug(ERROR,"system call","bind",0);
	
	if( listen(listenfd,64) <0)
		debug(ERROR,"system call","listen",0);
	
	while(1){
		length = sizeof(cli_addr);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			debug(ERROR,"system call","accept",0);
		if((pid = fork()) < 0) {
			debug(ERROR,"system call","fork",0);
		}
		else {
			if(pid == 0) { 	// Proceso hijo
				(void)close(listenfd);
				process_web_request(socketfd); // El hijo termina tras llamar a esta función
			} else { 	// Proceso padre
				(void)close(socketfd);
			}
		}
	}
}
