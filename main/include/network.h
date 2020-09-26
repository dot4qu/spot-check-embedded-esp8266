#ifndef NETWORK_H
#define NETWORK_H

#define URL_BASE "http://spotcheck.brianteam.dev/"

typedef struct {
    char* key;
    char* value;
} query_param;

typedef struct {
    char *url;
    query_param *params;
    uint8_t num_params;
} request;

bool http_client_inited;

void init_wifi();
void init_http();
int perform_request(request *request_obj, char **read_buffer);
request build_request(char* endpoint, char *spot, char *days, char *url_buf, query_param *params);

#endif
