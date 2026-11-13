#include "eden_bridge.h"
#include "browser.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define CONFIG "/data/prosperoeden/config/prosperoeden.json"
#define ORIGIN "https://eden-rooms-ps5.pages.dev"
#define PORT 24879
#define LIMIT 131072
/* Fail closed until real PS5 config and browser transport are verified. */
#define EDEN_BRIDGE_ALLOW_WRITE 0

static int ip_ok(const char *ip) {
  struct in_addr addr;
  return inet_pton(AF_INET, ip, &addr) == 1 && strlen(ip) < 16;
}
static int skip_string(const char *s, int i, int end) {
  if (i >= end || s[i] != '"') return -1;
  for (i++; i < end; i++) {
    if (s[i] == '\\') { i++; continue; }
    if (s[i] == '"') return i + 1;
  }
  return -1;
}
static int match_brace(const char *s, int at, int end) {
  int depth = 0;
  for (int i = at; i < end; i++) {
    if (s[i] == '"') { i = skip_string(s, i, end) - 1; if (i < 0) return -1; continue; }
    if (s[i] == '{') depth++;
    if (s[i] == '}') { depth--; if (depth == 0) return i + 1; if (depth < 0) return -1; }
  }
  return -1;
}
static int space(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }
static int find_fields(const char *s, int from, int to, int *h1, int *h2, int *p1, int *p2) {
  int i = from;
  *h1 = *h2 = *p1 = *p2 = -1;
  while (i < to) {
    while (i < to && (space(s[i]) || s[i] == ',')) i++;
    if (i >= to || s[i] == '}') break;
    if (s[i] != '"') return -1;
    int ks = i + 1, ke = skip_string(s, i, to);
    if (ke < 0) return -1;
    i = ke;
    while (i < to && space(s[i])) i++;
    if (i >= to || s[i++] != ':') return -1;
    while (i < to && space(s[i])) i++;
    int v1 = i, v2;
    if (i < to && s[i] == '"') v2 = skip_string(s, i, to);
    else if (i < to && s[i] == '{') v2 = match_brace(s, i, to);
    else { while (i < to && s[i] != ',' && s[i] != '}' && !space(s[i])) i++; v2 = i; }
    if (v2 < 0) return -1;
    int keylen = ke - ks - 1;
    if (keylen == 4 && !memcmp(s + ks, "host", 4)) { *h1 = v1; *h2 = v2; }
    if (keylen == 4 && !memcmp(s + ks, "port", 4)) { *p1 = v1; *p2 = v2; }
    i = v2;
  }
  return *h1 >= 0 && *p1 >= 0 ? 0 : -1;
}
struct edit { int start, end; char value[64]; };
static int cmp_edit(const void *a, const void *b) {
  const struct edit *x = a, *y = b;
  return x->start - y->start;
}
static int replace_multiplayer(char *input, size_t n, const char *ip, int port, char **output, size_t *outn) {
  struct edit edits[64]; int count = 0;
  for (int i = 0; i < (int)n;) {
    if (input[i] != '"') { i++; continue; }
    int k = skip_string(input, i, (int)n);
    if (k < 0) return -1;
    if (k - i == 13 && !memcmp(input + i + 1, "multiplayer", 11)) {
      int j = k;
      while (j < (int)n && space(input[j])) j++;
      if (j < (int)n && input[j++] == ':') {
        while (j < (int)n && space(input[j])) j++;
        if (j < (int)n && input[j] == '{') {
          int end = match_brace(input, j, (int)n);
          int h1,h2,p1,p2;
          if (end < 0 || find_fields(input,j+1,end-1,&h1,&h2,&p1,&p2)) return -1;
          if (count + 2 > 64) return -1;
          edits[count].start=h1; edits[count].end=h2;
          snprintf(edits[count++].value,64,"\"%s\"",ip);
          edits[count].start=p1; edits[count].end=p2;
          snprintf(edits[count++].value,64,"%d",port);
          i=end; continue;
        }
      }
    }
    i=k;
  }
  if (!count) return -1;
  qsort(edits,count,sizeof(edits[0]),cmp_edit);
  size_t total=n+count*64+1;
  char *result=malloc(total);
  if (!result) return -1;
  size_t at=0, last=0;
  for (int e=0;e<count;e++) {
    if (edits[e].start < (int)last) { free(result); return -1; }
    size_t before=(size_t)edits[e].start-last;
    memcpy(result+at,input+last,before); at+=before;
    size_t len=strlen(edits[e].value);
    memcpy(result+at,edits[e].value,len); at+=len;
    last=edits[e].end;
  }
  memcpy(result+at,input+last,n-last); at+=n-last; result[at]=0;
  *output=result; *outn=at;
  return 0;
}
static int write_all(int fd,const char *data,size_t n) {
  while(n) { ssize_t r=write(fd,data,n); if(r<=0)return -1; data+=r; n-=r; }
  return 0;
}
static int save_connection(const char *ip,int port) {
  int fd=open(CONFIG,O_RDONLY); if(fd<0)return -1;
  struct stat st;
  if(fstat(fd,&st)||st.st_size<=0||st.st_size>LIMIT) {close(fd);return -1;}
  size_t n=(size_t)st.st_size;
  char *original=malloc(n+1); if(!original){close(fd);return -1;}
  size_t got=0;
  while(got<n) {ssize_t r=read(fd,original+got,n-got);if(r<=0){close(fd);free(original);return -1;}got+=r;}
  close(fd);original[n]=0;
  char *updated=NULL;size_t updated_len=0;
  if(replace_multiplayer(original,n,ip,port,&updated,&updated_len)){free(original);return -1;}
  /* Backup original before replacing the configuration. */
  const char *bak=CONFIG ".edenrooms.bak";
  int b=open(bak,O_WRONLY|O_CREAT|O_TRUNC,0600);
  if(b<0){free(original);free(updated);return -1;}
  int ok=write_all(b,original,n);if(fsync(b))ok=-1;close(b);free(original);
  if(ok){free(updated);return -1;}
  const char *tmp=CONFIG ".edenrooms.tmp";
  int t=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,st.st_mode&0777);
  if(t<0){free(updated);return -1;}
  ok=write_all(t,updated,updated_len);if(fsync(t))ok=-1;close(t);free(updated);
  if(ok){unlink(tmp);return -1;}
  if(rename(tmp,CONFIG)){unlink(tmp);return -1;}
  return 0;
}
static void reply(int fd,int status,const char *body) {
  char header[512];
  int n=snprintf(header,sizeof(header),
    "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
    "Access-Control-Allow-Origin: " ORIGIN "\r\n"
    "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n",
    status,status==200?"OK":"Error",strlen(body));
  write_all(fd,header,(size_t)n);write_all(fd,body,strlen(body));
}
static int parse_body(const char *body,char *ip,int *port) {
  /* Strict expected JSON body from our frontend; reject extra keys. */
  char tail=0;
  int p=0;
  if(sscanf(body," { \"host\" : \"%15[0-9.]\" , \"port\" : %d } %c",ip,&p,&tail)!=2)return -1;
  if(!ip_ok(ip)||p<1||p>65535)return -1;
  *port=p;return 0;
}
static void handle_client(int fd) {
  char request[4096+1];int n=0;
  struct timeval tv={3,0};
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
  while(n<3500) {
    ssize_t r=recv(fd,request+n,3500-n,0);
    if(r<=0)break;
    n+=(int)r;request[n]=0;
    if(strstr(request,"\r\n\r\n"))break;
  }
  if(n<=0)return;
  request[n]=0;
  /* Read-only probe: never touches ProsperoEden's configuration. */
  if(strncmp(request,"GET /v1/health HTTP/1.",22)==0) {
    reply(fd,200,"{\"ok\":true,\"service\":\"eden-bridge\",\"writeEnabled\":false}");
    return;
  }
  if(strncmp(request,"POST /v1/connection HTTP/1.",27)!=0) {reply(fd,404,"{\"ok\":false,\"error\":\"not found\"}");return;}
  /* A web page must be the exact trusted origin. */
  if(!strstr(request,"\r\nOrigin: " ORIGIN "\r\n") &&
     !strstr(request,"\r\norigin: " ORIGIN "\r\n")) {
    reply(fd,403,"{\"ok\":false,\"error\":\"origin denied\"}");return;
  }
  char *cl=strstr(request,"\r\nContent-Length:");
  if(!cl)cl=strstr(request,"\r\ncontent-length:");
  if(!cl){reply(fd,400,"{\"ok\":false,\"error\":\"length missing\"}");return;}
  long len=strtol(strchr(cl,':')+1,NULL,10);
  if(len<=0||len>512){reply(fd,400,"{\"ok\":false,\"error\":\"invalid length\"}");return;}
  char *body=strstr(request,"\r\n\r\n")+4;
  size_t have=(size_t)(request+n-body);
  if ((size_t)(body-request) + (size_t)len > sizeof(request)-1) {reply(fd,400,"{\"ok\":false,\"error\":\"request too large\"}");return;}
  while(have<(size_t)len) {
    ssize_t r=recv(fd,body+have,(size_t)len-have,0);
    if(r<=0){reply(fd,400,"{\"ok\":false,\"error\":\"incomplete body\"}");return;}
    have+=(size_t)r;
  }
  body[len]=0;
  char ip[16]={0};int port=0;
  if(parse_body(body,ip,&port)){reply(fd,400,"{\"ok\":false,\"error\":\"invalid address or port\"}");return;}
  if(!EDEN_BRIDGE_ALLOW_WRITE) {reply(fd,503,"{\"ok\":false,\"error\":\"write disabled pending PS5 validation\"}");return;}
  if(save_connection(ip,port)) {reply(fd,500,"{\"ok\":false,\"error\":\"config write failed\"}");return;}
  reply(fd,200,"{\"ok\":true}");
}
int eden_bridge_run(void) {
  int s=socket(AF_INET,SOCK_STREAM,0);
  if(s<0) { log_msg("[EDEN BRIDGE] socket failed errno=%d\n", errno); notify("Eden Bridge: socket error"); return -1; }
  struct sockaddr_in a;memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;a.sin_port=htons(PORT);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(bind(s,(struct sockaddr*)&a,sizeof(a)) != 0) {\n    log_msg("[EDEN BRIDGE] bind 127.0.0.1:%d failed errno=%d\\n", PORT, errno);\n    notify("Eden Bridge: port bind failed");\n    close(s); return -1;\n  }\n  if(listen(s,4) != 0) {\n    log_msg("[EDEN BRIDGE] listen failed errno=%d\\n", errno);\n    notify("Eden Bridge: listen failed");\n    close(s); return -1;\n  }
  log_msg("[EDEN BRIDGE] Listening on 127.0.0.1:%d\n",PORT);\n  notify("Eden Bridge listening on localhost:24879");
  for(;;) {
    int c=accept(s,NULL,NULL);
    if(c<0){if(errno==EINTR)continue;break;}
    handle_client(c);close(c);
  }
  close(s);return -1;
}
