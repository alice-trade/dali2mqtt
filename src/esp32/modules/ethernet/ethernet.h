//
// Created by danil on 23.02.2025.
//

#ifndef ETHERNET_H
#define ETHERNET_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif


    esp_err_t ethernet_manager_init(void);
    esp_err_t ethernet_manager_start(void);
    esp_err_t ethernet_manager_stop(void);


#ifdef __cplusplus
}
#endif
#endif //ETHERNET_H
