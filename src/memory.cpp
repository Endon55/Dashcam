#include "memory.h"
#include <stdlib.h>


static int REFERENCE_COUNT = 0;

void* dc_malloc(int bytes)
{
    void* mem = malloc(bytes);
    if(mem != nullptr)
    {
        REFERENCE_COUNT++;
    }
    return mem;
}

void* dc_calloc(int size, int bytes)
{
    void *mem = calloc(size, bytes);
    if(mem != nullptr)
    {
        REFERENCE_COUNT++;
    }
    return mem;
}

void dc_free(void* ptr)
{
    if(ptr != nullptr)
    {
        REFERENCE_COUNT--;
    }
    free(ptr);
    ptr = nullptr;
}

char* dc_strdup(const char* str1)
{
    int len = 0;
    while(str1[len] != '\0')
    {
        len++;
    }
    char* str2 = (char* )dc_malloc(sizeof(char) * len);
    for(int i = 0; i < len; i++)
    {
        str2[i] = str1[i];
    }
    return str2;
}

int * new_int(int value)
{
    int * num = (int*)dc_malloc(sizeof(int));
    *num = value;
    return num;
}
int outstanding_references()
{
    return REFERENCE_COUNT;
}
