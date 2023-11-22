#include <stdio.h>
#include "csapp.h"
#include "hash.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void proxy(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *host, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *vargp);

int main(int argc, char **argv) {
	int listenfd, *connfdp;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	//스레드 식별자
	pthread_t tid;

	hashmap_init(&cache_map, INITIAL_HASHMAP_SIZE);
	
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	listenfd = Open_listenfd(argv[1]);

	while (1) {

		clientlen = sizeof(clientaddr);
		connfdp = malloc(sizeof(int));
		*connfdp = Accept(listenfd, (SA *)&clientaddr,&clientlen);  // line:netp:tiny:accept
		Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);
		// 클라이언트와의 연결을 처리하는 새로운 스레드를 생성하고 실행
		// 이렇게 처리하는 이유는 다중 클라이언트 동시 처리 때문
		Pthread_create(&tid, NULL, thread, connfdp);
	}
	return 0;
}

void *thread(void *vargp){ //p.954 12.14
	
	int connfd = *((int *)vargp);
	// 스레드를 분리
	// 스레드를 분리하면 종료될때 자동으로 자원을 회수하므로 메인 스레드나 다른 스레드에서 따로
	// 자원을 해제할 필요가 없다.
	Pthread_detach(pthread_self());
	// 동적으로 할다오딘 메모리를 해제
	Free(vargp);
	proxy(connfd);   // line:netp:tiny:doit
	Close(connfd);  // line:netp:tiny:close
	return NULL;
}

void proxy(int fd){
	int target_serverfd; 
	struct stat sbuf;
	char buf[MAXLINE];
	char buf_res[MAXLINE];
	char version[MAXLINE];
	//URI parse value
	char method[MAXLINE], uri[MAXLINE], host[MAXLINE], path[MAXLINE], port[MAXLINE];
	char filename[MAXLINE], cgiargs[MAXLINE];
	rio_t rio;



	Rio_readinitb(&rio, fd);
	Rio_readlineb(&rio, buf, MAXLINE);
	printf("Request headers:\n");
	printf("%s", buf);
	sscanf(buf, "%s %s %s", method, uri, version);

	parse_uri(uri, host, port, path);
	printf("-----------------------------\n");	
	printf("\nClient Request Info : \n");
	printf("mothed : %s\n", method);
	printf("URI : %s\n", uri);
	printf("hostName : %s\n", host);
	printf("port : %s\n", port);
	printf("path : %s\n", path);
	printf("-----------------------------\n");

	cache_entry *cached_content = hashmap_search(&cache_map, uri);
	
	if(cached_content != NULL){ //cache hit!
		cached_content->last_access = time(NULL);
		//set Header 
		sprintf(buf_res, "HTTP/1.0 200 OK\r\n");
		sprintf(buf_res, "%sServer: Tiny Web Server\r\n", buf_res);
		sprintf(buf_res, "%sConnection: close\r\n", buf_res);
		sprintf(buf_res, "%sContent-length: %d\r\n\r\n", buf_res, cached_content->size);
		Rio_writen(fd, buf_res, strlen(buf_res));

		Rio_writen(fd, cached_content->data, cached_content->size);
	} else { //cache miss...
		target_serverfd = Open_clientfd(host, port);
		request(target_serverfd, host, path);
		response(target_serverfd, fd, uri);
		Close(target_serverfd);
	}
}

void request(int target_fd,char *host,char *path){

	char *version = "HTTP/1.0";
	char buf[MAXLINE];
	sprintf(buf, "GET %s %s\r\n", path, version);
	sprintf(buf, "%sHost: %s\r\n", buf, host);
	sprintf(buf, "%s%s", buf, user_agent_hdr);
	sprintf(buf, "%sConnections: close\r\n", buf);
	sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf);
	printf("%s",buf);
	Rio_writen(target_fd, buf, (size_t)strlen(buf));
}

void response(int target_fd, int fd, char* uri){
	char buf[MAX_CACHE_SIZE];
	rio_t rio;
	int content_length;

	char *ptr, *cached_data;
	int total_size = 0;

	Rio_readinitb(&rio, target_fd);
	//헤더 만들기
	while (strcmp(buf, "\r\n")){
		Rio_readlineb(&rio, buf, MAX_CACHE_SIZE);
		// 헤더에서 사이즈 뽑아내기
		if (strstr(buf, "Content-length")) 
			content_length = atoi(strchr(buf, ':') + 1);
		Rio_writen(fd, buf, strlen(buf));
	}
	
	total_size = content_length;
	//사이즈 만큼 캐시에 넣는다
	cached_data = malloc(total_size);
	ptr = cached_data;
	
	// 바디를 클라이언트한테 전달과 동시에 캐시에 저장
	while (total_size > 0) {
		// 함수는 지정된 크기만큼 안전하게 읽어들이기 위해 사용되고 있습니다. 
		// 읽어들인 데이터는 Rio_writen 함수를 통해 클라이언트에게 전송됩니다.
		int bytes_read = Rio_readnb(&rio, ptr, total_size);
		// 클라이언트에게 전송
		Rio_writen(fd, ptr, bytes_read);
		// 포인터를 bytes_read 만큼 이동
		ptr += bytes_read;
		total_size -= bytes_read;
	}
	
	//사이즈 작으면 캐시해줌
	// 서버응답 헤더에서 Content-Length를 통해 얻은 응답 본문의 크기입니다.
	// MAX_OBJECT_SIZE: 정의된 최대 객체 크기입니다. 이 값보다 작은 응답 본문 크기인 경우에만 캐시에 저장됩니다.
	if(content_length <= MAX_OBJECT_SIZE){
		// new_entry: cache_entry 구조체를 나타내는 포인터입니다. cache_entry는 캐시에 저장할 각 엔트리를 나타냅니다	
		cache_entry *new_entry = malloc(sizeof(cache_entry));
		new_entry->url = strdup(uri);
		// cached_data: Rio_readnb를 통해 읽은 응답 데이터가 저장된 메모리 블록을 가리키는 포인터입니다.
		new_entry->data = cached_data;
		new_entry->size = content_length + total_size;
		// hashmap_insert(&cache_map, new_entry): new_entry를 캐시 맵에 삽입합니다. 이 삽입은 해당 URL에 대한 응답 데이터를 캐시에 저장하는 작업입니다.
		hashmap_insert(&cache_map, new_entry);	
	}
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
	char buf[MAXLINE], body[MAXBUF];

	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp){
	char buf[MAXLINE];

	Rio_readlineb(rp, buf, MAXLINE);
	while(strcmp(buf, "\r\n")){
		Rio_readlineb(rp, buf, MAXLINE);
		printf("%s", buf);
	}
	return;
}

int parse_uri(char *uri, char *host, char *port, char *path){
	char *parse_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri;

	//www.github.com:80/bckim9489.html
	strcpy(host, parse_ptr);

	strcpy(path, "/"); //path = /

	parse_ptr = strchr(host, '/');
	if(parse_ptr){
		//path = /bckim9489.html
		*parse_ptr = '\0';
		parse_ptr += 1;
		strcat(path, parse_ptr);
	}

	//www.github.com:80
	parse_ptr = strchr(host, ':');
	if(parse_ptr){
		//port = 80
		*parse_ptr = '\0';
		parse_ptr += 1;
		strcpy(port, parse_ptr);
	} else {
		strcpy(port, "80");
	}

	return 0;
}
