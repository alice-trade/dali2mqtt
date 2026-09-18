target_sources(DaliMQTT-Core PRIVATE
        AppCoordinator.cxx
        ConfigStore.cxx
        SyslogService.cxx
        SystemControls.cxx
        SystemScheduler.cxx
        OtaService.cxx

        Application.hxx
        AppCoordinator.hxx
        ConfigStore.hxx
        ConfigStructure.hxx
        SyslogService.hxx
        SystemControls.hxx
        SystemScheduler.hxx
        OtaService.hxx
        WebUIController.hxx
        AppCoordinator.icc
        ConfigStore.icc
        SystemScheduler.icc
        OtaService.icc
        WebUIController.icc
)