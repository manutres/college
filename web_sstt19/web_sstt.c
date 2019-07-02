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

struct lista_cookies {
	char * key;
	char * value;
	struct lista_cookies * sig;
};

struct http_header {
	char * key;
	char * value;
};

struct http_request {
	char * method;
	char * path;
	char * http_version;
	struct http_header header[NUM_HEADERS];
	int num_headers;
	char * body;
};

struct http_response {
	char * http_version;
	char * status_code;
	char * status_string;
	struct http_header header[NUM_HEADERS];
	int next_header;
};

char * get_header_value(struct http_request * request, const char * header)
{
	for(int i = 0; i<NUM_HEADERS; i++)
	{
		if(strcmp(request->header[i].key, header))
			return request->header[i].value;
	}
	return NULL;
}

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

struct http_response * create_response(const char * code, const char * string)
{
	struct http_response * response = malloc(sizeof(struct http_response));
	response->http_version = strdup("HTTP/1.1");
	response->status_code = strdup(code);
	response->status_string = strdup(string);
	response->next_header = 0;
	return response;
}

void set_header(struct http_response * response, const char * key, const char * value)
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

struct http_request * parse_request(char * buffer)
{
	struct http_request * request = malloc(sizeof(struct http_request));
	char * offset = buffer;
	request->method = getToken(&offset, ' ');
	request->path = getToken(&offset, ' ');
	request->http_version = getToken(&offset, '\r');
	offset++; //jump \n
	
	char * token = getToken(&offset, ':'); //get first keyheader
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

void free_cookies(struct lista_cookies * cookies) {
	if(cookies->sig != NULL) 
		free_cookies(cookies->sig);

	free(cookies->key);
	free(cookies->value);
	free(cookies);
}

void get_cookies(struct lista_cookies * cookies, const struct http_request * req)
{
	char * offset;
	struct lista_cookies * lista_it = cookies;
	lista_it->key = NULL;
	lista_it->value = NULL;
	for(int i = 0; i<req->num_headers; i++)
	{
		if(!strcmp(req->header[i].key, "Cookie"))
		{
			offset = req->header[i].value;
			lista_it->key = getToken(&offset, '=');
			lista_it->value = strdup(offset);
			lista_it->sig = malloc(sizeof(struct lista_cookies));
			lista_it = lista_it->sig;
		}
	}
}


void process_web_request(int descriptorFichero)
{
	debug(LOG,"request","Ha llegado una peticion",descriptorFichero);

	int bytes_readed; 
	while(bytes_readed != 0) 
	{
		char buffer[BUFSIZE] = {0}; 	// rcv buffer

		bytes_readed = read(descriptorFichero, buffer, BUFSIZE);
		if(bytes_readed == -1)
			debug(LOG,"Socket","Error en la lectura del socket",descriptorFichero);
		else if (bytes_readed > 0) {
			struct http_request * request;
			int cookie_counter;
			struct http_response * response;
			char responseString[BUFSIZE] = {0};
			char full_path[200];
			char content_length[20];
			int requested_file_fd;
			struct stat file_stat;
			off_t offset = 0;

			buffer[bytes_readed] = 0;
			request = parse_request(buffer);

			struct lista_cookies * cookies = malloc(sizeof(struct lista_cookies));
			get_cookies(cookies, request);
			if(cookies->key != NULL)
			{
				cookie_counter = atoi(cookies->value);
				cookie_counter++;
			} 
			else 
				cookie_counter = 1;
			
			char cookie_value_str[100];
			sprintf(cookie_value_str, "cookie_counter=%d; Max-Age=10", cookie_counter);

			if(cookie_counter >= 10)
			{
				char * body = "420 Too Many Requests: Wait two minutes";
				sprintf(content_length,"%ld", strlen(body));

				response = create_response("420", "Too Many Requests");
				set_header(response, "Server", "web_sstt");
				set_header(response, "Content-Type", "text/html");
				set_header(response, "Connection", "keep-alive");
				set_header(response, "Content-Length", content_length);

				response_to_string(responseString,response);
				write(descriptorFichero, responseString, strlen(responseString));
				write(descriptorFichero, body, strlen(body));
			}
			else 
			{
				if(!strcmp(request->method, "GET"))
				{
					//DESDE AQUI#####################################################
					//extraemos los parametros del path si los hubiera
					char * path;
					char * query_params;
					if(strchr(request->path, '?')) {
						char * offsetAux = request->path;
						path = getToken(&offsetAux, '?');
						query_params = strdup(offsetAux);
					}
					else {
						path = request->path;
					}
					
					//asumiendo que siempre va a llevar una query este path
					if(!strcmp(path, "/checkmail")) 
					{
						char * offsetAux = query_params;
						char * key = getToken(&offsetAux, '=');
						char * value = strdup(offsetAux);

						if(!strcmp(EMAIL, value)) 
						{
							char * body = "EMAIL CORRECTO";
							sprintf(content_length,"%ld", strlen(body));

							response = create_response("200", "OK");
							set_header(response, "Server", "web_sstt");
							set_header(response, "Content-Type", "text/plain");
							set_header(response, "Connection", "keep-alive");
							set_header(response, "Keep-Alive", "timeout=5");
							set_header(response, "Content-Length", content_length);

							response_to_string(responseString,response);
							write(descriptorFichero, responseString, strlen(responseString));
							write(descriptorFichero, body, strlen(body));
						}
						else 
						{
							char body[] = "EMAIL INCORRECTO";
							sprintf(content_length,"%ld", strlen(body));

							response = create_response("200", "OK");
							set_header(response, "Server", "web_sstt");
							set_header(response, "Connection", "keep-alive");
							set_header(response, "Keep-Alive", "timeout=5");
							set_header(response, "Content-Type", "text/plain");
							set_header(response, "Content-Length", content_length);

							response_to_string(responseString,response);
							write(descriptorFichero, responseString, strlen(responseString));
							write(descriptorFichero, body, strlen(body));
						}
					}
					//HASTA AQUI#####################################################
					else
					{
						sprintf(full_path,"%s%s", WWW_PATH, path);
						requested_file_fd = open(full_path, O_RDONLY);			

						if(requested_file_fd != -1) 
						{
							fstat(requested_file_fd, &file_stat);
							sprintf(content_length, "%ld", file_stat.st_size);

							response = create_response("200", "OK");
							set_header(response, "Server", "web_sstt");
							set_header(response, "Content-Type", get_file_extension(path));
							set_header(response, "Connection", "keep-alive");
							set_header(response, "Keep-Alive", "timeout=5");
							set_header(response, "Set-Cookie", cookie_value_str);
							set_header(response, "Content-Length", content_length);

							response_to_string(responseString,response);
							write(descriptorFichero, responseString, strlen(responseString));
							sendfile(descriptorFichero, requested_file_fd, &offset, file_stat.st_size);

							free_cookies(cookies);
							close(requested_file_fd);
						} 
						else 
						{
							char * body = "<body><h1>404 NOT FOUND</h1></body>";
							sprintf(content_length,"%ld", strlen(body));

							response = create_response("404", "NOT FOUND");
							set_header(response, "Server", "web_sstt");
							set_header(response, "Content-Type", "text/html");
							set_header(response, "Connection", "keep-alive");
							set_header(response, "Content-Length", content_length);

							response_to_string(responseString,response);
							write(descriptorFichero, responseString, strlen(responseString));
							write(descriptorFichero, body, strlen(body));
						}
					}
				}

			}
			
		}
	}
	puts("timeout");
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
