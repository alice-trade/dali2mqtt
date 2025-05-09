//
// Unity tests entrypoint
//
#include <stdio.h>
#include <string.h>
#include "unity.h"

static void print_banner(const char* text);


void app_main(void) {
    print_banner("Test subject: Config");
    UNITY_BEGIN();
    // Можно вызывать отдельные тестовые функции или register_config_manager_tests()
    unity_run_tests_by_tag("[config]", false);
    UNITY_END();

    print_banner("Test subject: DALI Interface");
    UNITY_BEGIN();
    // Можно вызывать отдельные тестовые функции или register_config_manager_tests()
    unity_run_tests_by_tag("[dali]", false);
    UNITY_END();
}

static void print_banner(const char* text)
{
    printf("\n#### %s #####\n\n", text);
}