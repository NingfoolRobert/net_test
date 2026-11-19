/**
 * @file ngx_rx_bsd_mcbc.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-02-20
 */
#pragma once
#include "ngx_rx.h"
class ngx_rx_bsd_mcbc : public ngx_rx<ngx_rx_bsd_mcbc> {

public:
    bool open() {


        return true;
    }

    void close();

    //
    template <typename F>
    int read(F &&f) {
        return 0;
    }
};
