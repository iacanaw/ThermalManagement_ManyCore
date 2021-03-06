
////////////////////////////////////////////////////////////////////////////////
//
//                W R I T T E N   B Y   I M P E R A S   I G E N
//
//                             Version 20170201.0
//
////////////////////////////////////////////////////////////////////////////////


#ifdef _PSE_
#    include "peripheral/impTypes.h"
#    include "peripheral/bhm.h"
#    include "peripheral/bhmHttp.h"
#    include "peripheral/ppm.h"
#else
#    include "hostapi/impTypes.h"
#endif

#include "networkInterface.igen.h"

static ppmBusPort busPorts[] = {
    {
        .name            = "DMAC",
        .type            = PPM_SLAVE_PORT,
        .addrHi          = 0x7LL,
        .mustBeConnected = 1,
        .remappable      = 0,
        .description     = 0,
    },
    {
        .name            = "MREAD",
        .type            = PPM_MASTER_PORT,
        .addrBits        = 32,
        .mustBeConnected = 0,
        .description     = 0,
    },
    {
        .name            = "MWRITE",
        .type            = PPM_MASTER_PORT,
        .addrBits        = 32,
        .mustBeConnected = 0,
        .description     = 0,
    },
    { 0 }
};

static PPM_BUS_PORT_FN(nextBusPort) {
    if(!busPort) {
        busPort = busPorts;
    } else {
        busPort++;
    }
    return busPort->name ? busPort : 0;
}

static ppmNetPort netPorts[] = {
    {
        .name            = "INT_NI",
        .type            = PPM_OUTPUT_PORT,
        .mustBeConnected = 0,
        .description     = "NI Interrupt Request"
    },
    { 0 }
};

static PPM_NET_PORT_FN(nextNetPort) {
    if(!netPort) {
         netPort = netPorts;
    } else {
        netPort++;
    }
    return netPort->name ? netPort : 0;
}


//                     Shared data for packetnet interface

Uns8 dataPort_pnsd[4];
Uns8 controlPort_pnsd[8];

static ppmPacketnetPort packetnetPorts[] = {
    {
        .name            = "dataPort",
        .mustBeConnected = 0,
        .description     = 0,
        .sharedData      = dataPort_pnsd,
        .sharedDataBytes = 4,
        .handlePtr       = &handles.dataPort,
        .packetnetCB     = dataPortUpd,
        .userData        = (void*)0
    },
    {
        .name            = "controlPort",
        .mustBeConnected = 0,
        .description     = 0,
        .sharedData      = controlPort_pnsd,
        .sharedDataBytes = 8,
        .handlePtr       = &handles.controlPort,
        .packetnetCB     = controlPortUpd,
        .userData        = (void*)0
    },
    { 0 }
};

static PPM_PACKETNET_PORT_FN(nextPacketnetPort) {
    if(!port) {
         port = packetnetPorts;
    } else {
        port++;
    }
    return port->name ? port : 0;
}

ppmModelAttr modelAttrs = {

    .versionString    = PPM_VERSION_STRING,
    .type             = PPM_MT_PERIPHERAL,

    .busPortsCB       = nextBusPort,  
    .netPortsCB       = nextNetPort,  
    .packetnetPortsCB = nextPacketnetPort,

    .saveCB        = peripheralSaveState,
    .restoreCB     = peripheralRestoreState,

    .vlnv          = {
        .vendor  = "gaph",
        .library = "peripheral",
        .name    = "networkInterface",
        .version = "1.0"
    },

    .family    = "gaph",

    .releaseStatus = PPM_UNSET,
    .visibility     = PPM_VISIBLE,
    .saveRestore    = 0,

};
