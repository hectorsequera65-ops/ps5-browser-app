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
  /* Never overwrite an existing recovery backup. */
  int b=open(bak,O_WRONLY|O_CREAT|O_EXCL,0600);
  if(b<0){free(original);free(updated);return -1;}
  int ok=write_all(b,original,n);if(fsync(b))ok=-1;
  if(close(b))ok=-1;
  free(original);
  if(ok){unlink(bak);free(updated);return -1;}
  const char *tmp=CONFIG ".edenrooms.tmp";
  int t=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,st.st_mode&0777);
  if(t<0){free(updated);return -1;}
  ok=write_all(t,updated,updated_len);
  if(fsync(t))ok=-1;
  if(close(t))ok=-1;
  if(ok){unlink(tmp);free(updated);return -1;}
  /* Verify the entire temporary file BEFORE replacing the real config. */
  int verify=open(tmp,O_RDONLY);
  if(verify<0){unlink(tmp);free(updated);return -1;}
  struct stat vst;
  if(fstat(verify,&vst)||vst.st_size!=(off_t)updated_len)ok=-1;
  char chunk[4096];
  size_t pos=0;
  while(!ok && pos<updated_len) {
    size_t want=updated_len-pos;
    if(want>sizeof(chunk))want=sizeof(chunk);
    ssize_t r=read(verify,chunk,want);
    if(r<=0 || memcmp(chunk,updated+pos,(size_t)r)!=0){ok=-1;break;}
    pos+=(size_t)r;
  }
  if(close(verify))ok=-1;
  if(ok || pos!=updated_len){unlink(tmp);free(updated);return -1;}
  /* Rename only after byte-for-byte verification. */
  if(rename(tmp,CONFIG)){unlink(tmp);free(updated);return -1;}
  free(updated);
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
static void check_config(int fd) {
  int file=open(CONFIG,O_RDONLY);
  if(file<0){reply(fd,404,"{\"ok\":false,\"error\":\"config not readable\"}");return;}
  struct stat st;
  if(fstat(file,&st)||st.st_size<=0||st.st_size>LIMIT){close(file);reply(fd,400,"{\"ok\":false,\"error\":\"invalid config size\"}");return;}
  size_t n=(size_t)st.st_size;char *data=malloc(n+1);
  if(!data){close(file);reply(fd,500,"{\"ok\":false,\"error\":\"out of memory\"}");return;}
  size_t got=0;
  while(got<n){ssize_t r=read(file,data+got,n-got);if(r<=0){close(file);free(data);reply(fd,500,"{\"ok\":false,\"error\":\"read failed\"}");return;}got+=(size_t)r;}
  close(file);data[n]=0;
  char *result=NULL;size_t result_len=0;
  int valid=replace_multiplayer(data,n,"203.0.113.10",24872,&result,&result_len)==0;
  free(data);free(result);
  if(!valid){reply(fd,422,"{\"ok\":false,\"error\":\"multiplayer fields not safely identified\",\"writeEnabled\":false}");return;}
  reply(fd,200,"{\"ok\":true,\"configReadable\":true,\"multiplayerFieldsFound\":true,\"writeEnabled\":false}");
}
static void reply_html(int fd,const char *body) {
  char header[320];
  int n=snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n",strlen(body));
  write_all(fd,header,(size_t)n);write_all(fd,body,strlen(body));
}
static int parse_room_query(const char *request,char *ip,int *port) {
  char path[256]={0};int p=0;char tail=0;
  if(sscanf(request,"GET %255s HTTP/1.",path)!=1)return -1;
  if(sscanf(path,"/v1/prepare?host=%15[0-9.]&port=%d%c",ip,&p,&tail)!=2)return -1;
  if(!ip_ok(ip)||p<1||p>65535)return -1;
  *port=p;return 0;
}
static void prepare_room(int fd,const char *request) {
  char ip[16]={0};int port=0;
  if(parse_room_query(request,ip,&port)) {
    reply(fd,400,"{\"ok\":false,\"error\":\"invalid room\"}");return;
  }
  char page[2200];
  snprintf(page,sizeof(page),
    "<!doctype html><html lang=\"es\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Eden Bridge PS5</title><style>body{font:16px system-ui;background:#101b2c;color:white;max-width:460px;margin:12vh auto;padding:22px}"
    "button{padding:15px;border:0;border-radius:10px;background:#3b82f6;color:white;font-size:16px;width:100%%}"
    "p{line-height:1.6}</style><h2>Eden Bridge PS5</h2><p>Sala: <b>%s:%d</b></p>"
    "<p>Confirmar solo la recepcion de prueba. No se modificara ProsperoEden.</p>"
    "<form method=\"POST\" action=\"/v1/preview\">"
    "<input type=\"hidden\" name=\"host\" value=\"%s\"><input type=\"hidden\" name=\"port\" value=\"%d\">"
    "<button type=\"submit\">Enviar sala a PS5</button></form></html>",ip,port,ip,port);
  reply_html(fd,page);
}
static void preview_form(int fd,const char *body) {
  char ip[16]={0};int port=0;char tail=0;
  if(sscanf(body,"host=%15[0-9.]&port=%d%c",ip,&port,&tail)!=2 ||
     !ip_ok(ip)||port<1||port>65535) {
    reply(fd,400,"{\"ok\":false,\"error\":\"invalid room\"}");return;
  }
  notify("Eden Bridge prueba: sala %s:%d recibida",ip,port);
  reply_html(fd,"<!doctype html><html lang=\"es\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><body style=\"font:18px system-ui;padding:24px;background:#101b2c;color:white\"><h2>Sala recibida en PS5</h2><p>Prueba completada. No se modifico la configuracion de ProsperoEden.</p></body></html>");
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
  if(strncmp(request,"GET /v1/config-check HTTP/1.",28)==0) {check_config(fd);return;}
  if(strncmp(request,"GET /v1/prepare?",16)==0) {prepare_room(fd,request);return;}
  if(strncmp(request,"POST /v1/preview HTTP/1.",24)==0) {
    char *cl=strstr(request,"\r\nContent-Length:");
    if(!cl)cl=strstr(request,"\r\ncontent-length:");
    char *sep=strstr(request,"\r\n\r\n");
    if(!cl||!sep){reply(fd,400,"{\"ok\":false,\"error\":\"missing body\"}");return;}
    long len=strtol(strchr(cl,':')+1,NULL,10);
    if(len<=0||len>128){reply(fd,400,"{\"ok\":false,\"error\":\"invalid length\"}");return;}
    char *body=sep+4;size_t have=(size_t)(request+n-body);
    while(have<(size_t)len) {
      ssize_t r=recv(fd,body+have,(size_t)len-have,0);
      if(r<=0){reply(fd,400,"{\"ok\":false,\"error\":\"incomplete body\"}");return;}
      have+=(size_t)r;
    }
    body[len]=0;preview_form(fd,body);return;
  }
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
  if(!EDEN_BRIDGE_ALLOW_WRITE) {
    log_msg("[EDEN BRIDGE] Preview received: %s:%d (no configuration changes)\n",ip,port);
    notify("Eden Bridge prueba: sala %s:%d recibida",ip,port);
    reply(fd,200,"{\"ok\":true,\"received\":true,\"saved\":false,\"mode\":\"preview\"}");
    return;
  }
  if(save_connection(ip,port)) {reply(fd,500,"{\"ok\":false,\"error\":\"config write failed\"}");return;}
  reply(fd,200,"{\"ok\":true}");
}
int eden_bridge_run(void) {
  int s=socket(AF_INET,SOCK_STREAM,0);
  if(s<0) {
    log_msg("[EDEN BRIDGE] socket failed errno=%d\n",errno);
    notify("Eden Bridge: socket error");
    return -1;
  }
  struct sockaddr_in a;memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;a.sin_port=htons(PORT);a.sin_addr.s_addr=htonl(INADDR_ANY);
  if(bind(s,(struct sockaddr*)&a,sizeof(a)) != 0) {
    log_msg("[EDEN BRIDGE] bind failed errno=%d\n",errno);
    notify("Eden Bridge: port bind failed");
    close(s);return -1;
  }
  if(listen(s,4) != 0) {
    log_msg("[EDEN BRIDGE] listen failed errno=%d\n",errno);
    notify("Eden Bridge: listen failed");
    close(s);return -1;
  }
  log_msg("[EDEN BRIDGE] Listening on LAN port %d\n",PORT);
  notify("Eden Bridge LAN test on port 24879");
  for(;;) {
    int c=accept(s,NULL,NULL);
    if(c<0){if(errno==EINTR)continue;break;}
    handle_client(c);close(c);
  }
  close(s);return -1;
}
