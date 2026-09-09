target_sources(DaliMQTT-Core PRIVATE
        RmtDaliTransceiver.cxx
        DaliBusEngine.cxx
        DaliDeviceRegistry.cxx

        RmtDaliTransceiver.hxx
        DaliBusEngine.hxx
        DaliDeviceRegistry.hxx
        DaliService.hxx

        RmtDaliTransceiver.icc
        DaliBusEngine.icc
        DaliService.icc
        DaliDeviceRegistry.icc

        DaliFrame.hxx
        DaliDT8OpCodes.hxx
        DaliColor.hxx
        DaliOpCodes.hxx
        DaliSpecialOpCodes.hxx
        DaliAddress.hxx
        DaliDeviceIdentity.hxx
        DaliDevice.hxx
        DaliControlGear.hxx
        DaliInputDevice.hxx
        DaliDeviceEvent.hxx
        DaliGroupEvent.hxx
        DaliInputEvent.hxx

        DaliTransceiverConcept.hxx
        DaliBusConcept.hxx
)