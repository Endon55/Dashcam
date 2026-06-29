#include "Utils.h"

bool Utils::str_starts_with(const char* base_string, const char* condition_string)
{
    const char * base = base_string;
    const char * cond = condition_string;

    while (*base != '\0')
    {
        if (*cond == '\0')
        {
            return true;
        }

        if(*base != *cond)
        {
            return false;
        }
        *base++;
        *cond++;
    }
    return false;
}