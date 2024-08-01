#include "ctype.h"

bool islower(char chr)
{
    return chr >= 'a' && chr <= 'z';
}

char toupper(char chr)
{
    return islower(chr) ? (chr - 'a' + 'A') : chr;
}

int min(int a, int b){
    return (a > b) ? b : a;
}

int max(int a, int b){
    return (a > b) ? a : b;
}