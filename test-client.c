#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "kiro-client.h"


int main(void)
{
    KiroClient *client = g_object_new(KIRO_TYPE_CLIENT, NULL);
    kiro_client_connect(client, "192.168.11.61", "60010", 5000);
    return 0; 
}