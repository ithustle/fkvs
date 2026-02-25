#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include "../../core/hashtable.h"

unsigned char *construct_set_command(const char *key, const char *value,
                                     size_t *command_len);

unsigned char *construct_get_command(const char *key, size_t *command_len);

unsigned char *construct_incr_command(const char *key, size_t *command_len);

unsigned char *construct_incr_by_command(const char *key, const char *value,
                                         size_t *command_len);

unsigned char *construct_ping_command(const char *value, size_t *command_len);

unsigned char *construct_info_command(size_t *command_len);

unsigned char *construct_decr_command(const char *key, size_t *command_len);

unsigned char *construct_decr_by_command(const char *key, const char *value, size_t *command_len);

#endif // COMMAND_PARSER_H
