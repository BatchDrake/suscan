//
// Created by happycactus on 21/06/23.
//
#include <suscan/suscan.h>
#include <suscan/analyzer/version.h>

#include <stdio.h>

int main(int argc, char *argv[])
{
    printf("suscan version: %s ABI: %d\n", suscan_api_version(),suscan_abi_version());

    return 0;
}