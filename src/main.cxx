// Copyright (c) 2026 Alice-Trade Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/Application.hxx"

extern "C" void app_main(void) {
    static daliMQTT::Application app;
    app.coordinator.start();
    while (true) {
        app.coordinator.eventLoop(1000);
        //
    }
}