#ifndef JSON_H
#define JSON_H

#define START_LIST_TRANSMISSION_COMMAND "START_LIST%"
#define END_LIST_TRANSMISSION_COMMAND "END_LIST%"

cJSON* parse_json(char *server_response);
int send_json_list(cJSON *list_json);

#endif
