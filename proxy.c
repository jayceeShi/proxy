#include "csapp.h"
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *acceptt = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding = "Accept-Encoding: gzip, deflate\r\n";
void *thread(void *vargp);
//针对cache的解说全部在struct cach中
struct cach {         //cache使用的是读者写者模型，存放着更新时间、url、该url对应的回复
	int tim;          //turn用来标记该记录是什么时候最后一次使用的，在换出的时候换出最早用到的那一条记录
	char url[MAXLINE];
	char response[MAX_OBJECT_SIZE];

	sem_t mutex;        //如果我们只想要阅读回复内容，那么只需要改变这里的状态，与此同时url, turn还可以阅读
	sem_t w;
	int readcnt;


	sem_t t_mutex;        //如果只是阅读更新时间，那么改变这里的状态
	sem_t t_w;
	int t_readcnt;


	sem_t url_mutex;      //如果是阅读url，改变这里的状态
	sem_t url_w;
	int url_readcnt;

};//如果我们需要写会一点东西，那么需要保证所有的锁都打开
struct cach cache[11];
struct connec {
	int tim;
	int connfd;
};
void ini() {
	int id;
	for (id = 0; id < 10; id++) {
		cache[id].tim = 0;
		cache[id].url[0] = '\0';      //所有字节都清0，但分配足够空间
		cache[id].response[0] = '\0';
		Sem_init(&cache[id].t_mutex, 0, 1);
		Sem_init(&cache[id].t_w, 0, 1);
		cache[id].t_readcnt = 0;
		Sem_init(&cache[id].url_mutex, 0, 1);
		Sem_init(&cache[id].url_w, 0, 1);
		cache[id].url_readcnt = 0;
		Sem_init(&cache[id].mutex, 0, 1);
		Sem_init(&cache[id].w, 0, 1);
		cache[id].readcnt = 0;
	}
}
void writecach(int id, char *url, char *response, int turn) {
	P(&cache[id].url_w);
	P(&cache[id].w);
	P(&cache[id].t_w);

	cache[id].tim = turn;
	strcpy(cache[id].response, response);
	strcpy(cache[id].url, url);

	V(&cache[id].t_w);
	V(&cache[id].w);
	V(&cache[id].url_w);

	//  printf("%s%s\n%d\n", cache[id].response, cache[id].url, cache[id].tim);
	return;
}
void readres(int id, char *dst, int turn) {
	P(&cache[id].mutex);
	cache[id].readcnt++;
	if (cache[id].readcnt == 1)
		P(&cache[id].w);
	V(&cache[id].mutex);
	P(&cache[id].t_w);

	cache[id].tim = turn;
	strcpy(dst, cache[id].response);

	V(&cache[id].t_w);
	P(&cache[id].mutex);
	cache[id].readcnt--;
	if (cache[id].readcnt == 0)
		V(&cache[id].w);
	V(&cache[id].mutex);

	return;
}
void readurl(int id, char *dst) {

	P(&cache[id].url_mutex);
	cache[id].url_readcnt++;
	if (cache[id].url_readcnt == 1)
		P(&cache[id].url_w);
	V(&cache[id].url_mutex);


	strcpy(dst, cache[id].url);


	P(&cache[id].url_mutex);
	cache[id].url_readcnt--;
	if (cache[id].url_readcnt == 0)
		V(&cache[id].url_w);
	V(&cache[id].url_mutex);

	return;
}
int readtime(int id) {

	int t;
	P(&cache[id].t_mutex);
	cache[id].t_readcnt++;
	if (cache[id].t_readcnt == 1)
		P(&cache[id].t_w);
	V(&cache[id].t_mutex);

	t = cache[id].tim;

	P(&cache[id].t_mutex);
	cache[id].t_readcnt--;
	if (cache[id].t_readcnt == 0)
		V(&cache[id].t_w);
	V(&cache[id].t_mutex);

	return t;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{//模仿书本内容
	char buf[MAXLINE], body[MAXBUF];

	/* Build the HTTP response body */
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

	/* Print the HTTP response */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
}

sem_t mutex;
sem_t mutex1;

int main(int argc, char**argv)
{
	Signal(SIGPIPE, SIG_IGN);
	pthread_t tid;
	Sem_init(&mutex, 0, 1);
	Sem_init(&mutex1, 0, 1);
	struct sockaddr_in clientaddr;
	int listenfd, port;
	int clientlen;
	struct connec* connfdp;
	int tim = 1;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}



	ini();

	//  printf("cache ready\n");

	port = atoi(argv[1]);
	listenfd = Open_listenfd(port);
	if (listenfd < 0) {
		fprintf(stderr, "cannot listen %d\n", port);
		exit(1);
	}

	//  printf("listen fd ready\n");

	clientlen = sizeof(clientaddr);


	while (1) {
		connfdp = (int*)Malloc(sizeof(struct connec));
		if (connfdp == NULL)continue;
		connfdp->connfd = Accept(listenfd, (SA*)&clientaddr, (socklen_t*)&clientlen);
		connfdp->tim = tim++;
		Pthread_create(&tid, NULL, thread, (void*)connfdp);

	}


	return 0;
}


void parse_url(char *url, char *filename, char *cgiargs, int* port) { //模仿书本内容 将url中的host path port分离
	char *ptr;
	char *p;

	char urll[MAXLINE];
	urll[0] = '\0';
	strcat(urll, url);

	filename[0] = '\0';
	cgiargs[0] = '\0';

	printf("url: %s\n", urll);
	ptr = strstr(urll, "//");
	if (ptr != NULL)
		ptr += 2;
	else
		ptr = urll;
	//http:// 和 https://

	p = strstr(ptr, ":");
	//port
	if (p != NULL) {
		*p = '\0';
		p++;
		sscanf(ptr, "%s", filename);
		sscanf(p, "%d%s", port, cgiargs);
	}
	else {
		p = strstr(ptr, "/");
		if (p != NULL) {
			*p = '\0';
			sscanf(ptr, "%s", filename);
			*p = '/';
			sscanf(p, "%s", cgiargs);
		}
		else
			sscanf(ptr, "%s", filename);

		*port = 80;
	}
	if (strlen(cgiargs) <= 1)
		strcpy(cgiargs, "/index.html");
	//  printf("it's parse url and\n port: %d\n cigargs: %s\n filename: %s\n", *port, cgiargs, filename);

	return;
}

int Openclientfd(char *hostname, int port) {//由于gethostbyname不安全，重写openclient函数
	int clientfd;
	struct hostent *hp;
	struct sockaddr_in serveraddr;

	if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1;

	P(&mutex);
	if ((hp = gethostbyname(hostname)) == NULL)
		return -2;
	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((char *)hp->h_addr_list[0], (char *)&serveraddr.sin_addr.s_addr, hp->h_length);


	serveraddr.sin_port = htons(port);

	V(&mutex);

	if (connect(clientfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0)
		return -1;
	return clientfd;
}



void *thread(void *vargp) {
	Pthread_detach(pthread_self());
	int connfd = ((struct connec*)vargp)->connfd;
	int tim = ((struct connec*)vargp)->tim;

	//  printf("%d\n%d\n", connfd, tim);

	Free(vargp);

	int port;
	char buf[MAXLINE];
	char method[MAXLINE], version[MAXLINE], url[MAXLINE];
	char filename[MAXLINE], cgiargs[MAXLINE];

	rio_t rio;
	Rio_readinitb(&rio, connfd);

	if (Rio_readlineb(&rio, buf, MAXLINE) == 0)return NULL;
	sscanf(buf, "%s %s %s", method, url, version);

	//  printf("buf1: %s\n", buf);

	if (strcasecmp(method, "GET")) {
		printf("NOT GET\r\n");
		clienterror(connfd, method, "501", "Not Implement", "proxy does not implenment this method");
		return NULL;
	}


	int i;
	char turl[MAXLINE];
	//判断是否在cache内
	for (i = 0; i < 10; i++) {
		readurl(i, turl);
		//    printf("read %d cache ok\n", i);
		if (!strcmp(url, turl))
			//the uri we want to find is already in th cache
			break;
	}
	//  printf("%d\n", i);

	char* tdata;
	tdata = (char*)Malloc(MAX_OBJECT_SIZE);
	tdata[0] = '\0';

	char send[MAXLINE];
	if (i < 10) {
		readres(i, tdata, tim);
		Rio_writen(connfd, tdata, strlen(tdata));
		Close(connfd);
		Free(tdata);
		return NULL;
	}

	parse_url(url, filename, cgiargs, &port);


	sprintf(send, "GET %s HTTP/1.0\r\nHost: %s\r\n", cgiargs, filename);
	int num = Rio_readlineb(&rio, buf, MAXLINE);

	//  printf("%d\n%s", num, buf);
	//这部分用于写request head，主要是改正accept等内容，其余内容照原样复制
	if (num <= 0) {
		printf("rio fail\n");
		return NULL;
	}
	int flagpro = 0;
	int flagcon = 0;
	int flaguser = 0;
	while (strcmp(buf, "\r\n")) {
		if (strstr(buf, "Host")) {
			strcat(send, "Host: ");
			strcat(send, filename);
			strcat(send, "\r\n");
		}
		else if (strstr(buf, "Accept:"))
			strcat(send, acceptt);
		else if (strstr(buf, "Accept-Encoding:"))
			strcat(send, accept_encoding);
		else if (strstr(buf, "User-Agent:")) {
			strcat(send, user_agent);
			flaguser = 1;
		}
		else if (strstr(buf, "Proxy-Connection:")) {
			strcat(send, "Proxy-Connection: close\r\n");
			flagpro = 1;
		}
		else if (strstr(buf, "Connection:")) {
			strcat(send, "Connection: close\r\n");
			flagcon = 1;
		}
		else if (!strstr(buf, "Cookie:"))
			// append addtional header except above listed headers
			strcat(send, buf);

		//    printf("buff:\n %s\n send:\n%s\n", buf, send);

		bzero(buf, MAXLINE);
		num = Rio_readlineb(&rio, buf, MAXLINE);
		if (num <= 0)break;
	}
	if (!flaguser) {
		strcat(send, user_agent);
	}
	if (!flagcon) {
		strcat(send, "Connection: close\r\n");
	}
	if (!flagpro)
		strcat(send, "Proxy-Connection: close\r\n");
	strcat(send, "\r\n");

	printf("send:\n%s\n", send);

	int serverfd = Openclientfd(filename, port);

	if (serverfd < 0) {
		printf("cannot connect server\n");
		Free(tdata);
		Close(connfd);
		return NULL;
	}
	char buff[MAXLINE];
	buff[0] = '\0';


	//  printf("has sent %s\n", send);

	Rio_writen(serverfd, send, strlen(send));
	//  printf("has sent %d\n", serverfd);

	//  printf("send %s\n",send);

	rio_t hostrio;
	Rio_readinitb(&hostrio, serverfd);

	int l = 0;
	//  printf("before while\n");
	while (1) {//server发送的东西全部按照原样抄写下来，不作改动；同时记录长度，判断是否可以存在cache内
		bzero(buf, MAXLINE);
		int num = Rio_readnb(&hostrio, buf, MAXLINE); //这里不可以使用readlineb，可能忽略换行符（血与泪的教训）
		printf("this time %d read out %d\n", connfd, num);
		if (num <= 0)break;
		l += num;

		if (l < MAX_OBJECT_SIZE)
			strncat(tdata, buf, num);

		printf("\n\n%d ready to write\n", connfd);
		printf("\n%d\n%s", num, buf);
		Rio_writen(connfd, buf, num);
		printf("thiss time %d read out %d\n", connfd, num);
		//    printf("l %d\n num %d\n ok\n", l, num);

	}
	//	printf("%d\n", strlen(tdata));

	//  printf("%s", tdata);
	//  printf("%d\n%d\n%d\n", contentLen, strlen(tdata), contentLen + strlen(tdata));

	if (l < MAX_OBJECT_SIZE) {
		printf("can cache\n");
		int id;
		int tmp = 0;
		for (id = 1; id < 10; id++) {
			if (readtime(id) < readtime(tmp))
				tmp = id;
		}
		//    printf("%d\n%s\n%d\n", tmp, url, tim);
		writecach(tmp, url, tdata, tim);
		//    printf("ok\n");
	}
	//  printf("is ok?\n");
	if (tdata != NULL)Free(tdata);
	shutdown(serverfd, SHUT_RDWR);
	shutdown(connfd, SHUT_RDWR);
	printf("this maybe ok\n");
	return NULL;
}




