#include "procesy_internal.h"

int price_to_index(int cena)
{
    switch (cena)
    {
    case p10:
        return 0;
    case p15:
        return 1;
    case p20:
        return 2;
    case p40:
        return 3;
    case p50:
        return 4;
    case p60:
        return 5;
    default:
        return -1;
    }
}
