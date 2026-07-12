#pragma once


void* dc_malloc(int bytes);

void* dc_calloc(int init_value, int bytes);

void dc_free(void* ptr);

char* dc_strdup(const char* str1);

int * new_int(int value);

int outstanding_references();
