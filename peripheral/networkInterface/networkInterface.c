
////////////////////////////////////////////////////////////////////////////////
//
//                W R I T T E N   B Y   I M P E R A S   I G E N
//
//                             Version 20170201.0
//
////////////////////////////////////////////////////////////////////////////////

#include "networkInterface.igen.h"
#include "peripheral/impTypes.h"
#include "peripheral/bhm.h"
#include "peripheral/bhmHttp.h"
#include "peripheral/ppm.h"
#include "../whnoc_dma/noc.h"


// BIG ENDIAN/LITTLE ENDIAN
#define __bswap_constant_32(x) \
     ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |		      \
      (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))

unsigned int htonl(unsigned int x){
    return __bswap_constant_32(x);
}
////////////////////////////////////////////////////////////////////////////////

unsigned int internalStatus = IDLE;     // IDLE - waiting for a TX or a RX
                                        // TX   - transmitting a packet
                                        // RX   - receiving a packet --- (i) actived by processor command -pulling- (ii) service packet -interruption-
unsigned int myStatus = GO;             // the availability to receive something from the NoC and write it in the memory
unsigned int auxAddress;                // Auxiliar address to store temporarily the address incomming address from the processor
unsigned int intTypeAddr = 0xFFFFFFFF;  // Stores the address to write the interruptionType

// RX Variables
unsigned int addressStart = 0xFFFFFFFF;
unsigned int receivingAddress = 0;
unsigned int receivingField = HEADER;   // Control variable to identify which packet field is the next to be read
unsigned int receivingCount;            // Counts the amount of remaining flits to be received

// TX Variables
unsigned int transmittingAddress = 0;   // Stores TX packet address
unsigned int transmittionEnd = FALSE;
unsigned int transmittingCount = HEADER;// Counts the amount of remaining flits to be transmitted
unsigned int control_in_STALLGO = GO;   // Stores the router input buffer status

// Status control variables
unsigned int control_TX = NI_STATUS_OFF;
unsigned int control_RX = NI_STATUS_OFF;

//
char chFlit[4];
unsigned int usFlit;

void vec2usi(){
    unsigned int auxFlit = 0x00000000;
    unsigned int aux;
    aux = 0x000000FF & chFlit[3];
    auxFlit = ((aux << 24) & 0xFF000000);

    aux = 0x000000FF & chFlit[2];
    auxFlit = auxFlit | ((aux << 16) & 0x00FF0000);

    aux = 0x000000FF & chFlit[1];
    auxFlit = auxFlit | ((aux << 8) & 0x0000FF00);

    aux = 0x000000FF & chFlit[0];
    auxFlit = auxFlit | ((aux) & 0x000000FF);

    usFlit = auxFlit;
    return;
}

void usi2vec(){
    chFlit[3] = (usFlit >> 24) & 0x000000FF;
    chFlit[2] = (usFlit >> 16) & 0x000000FF;
    chFlit[1] = (usFlit >> 8) & 0x000000FF;
    chFlit[0] = usFlit & 0x000000FF;
}

void setGO(){
    myStatus = GO;
    ppmPacketnetWrite(handles.controlPort, &myStatus, sizeof(myStatus));
}

void setSTALL(){
    myStatus = STALL;
    ppmPacketnetWrite(handles.controlPort, &myStatus, sizeof(myStatus));
}

void statusUpdate(unsigned int status){
    if(status == TX) transmittionEnd = FALSE;
    //bhmMessage("I", "statusUpdate", "Atualizando status de: %x para: %x",internalStatus,status);
    internalStatus = status;
    DMAC_ab8_data.status.value = htonl(status);
}

void informIteration(){
    //bhmMessage("INFO", "ITERATIONPORT", "Informando iteracao - myInternalStatus: %x", internalStatus);
    unsigned long long int iterate = 0xFFFFFFFFFFFFFFFFULL;
    ppmPacketnetWrite(handles.controlPort, &iterate, sizeof(iterate));
}

void writeMem(unsigned int flit, unsigned int addr){
    usFlit = flit;
    usi2vec();
    ppmAddressSpaceHandle h = ppmOpenAddressSpace("MWRITE");
    if(!h) {
        bhmMessage("I", "NI_ITERATOR", "ERROR_WRITE h handling!");
        while(1){} // error handling
    }
    ppmWriteAddressSpace(h, addr, sizeof(chFlit), chFlit);
    ppmCloseAddressSpace(h);
}

unsigned int readMem(unsigned int addr){
    //TODO encapsular o envio nesta funcao
    return 0;
}

void niIteration(){
    if(control_TX == NI_STATUS_ON && control_in_STALLGO == GO){
        // If the transmittion isn't finished yet...
        if(transmittingCount != EMPTY){
            // READ - encapsular
            ppmAddressSpaceHandle h = ppmOpenAddressSpace("MREAD");
            if(!h) {
                bhmMessage("I", "NI_ITERATOR", "ERROR_READ h handling!");
                while(1){} // error handling
            }
            ppmReadAddressSpace(h, transmittingAddress, sizeof(chFlit), chFlit);
            ppmCloseAddressSpace(h);
            vec2usi(); // transform the data from vector to a single unsigned int
            // READ - encapsular
            //bhmMessage("INFO", "NIITERATION", "Enviando o flit %x\n",htonl(usFlit));
            if(transmittingCount == HEADER){
                transmittingCount = SIZE;
            }
            else if(transmittingCount == SIZE){
                transmittingCount = htonl(usFlit);
            }
            else{
                transmittingCount = transmittingCount - 1;
            }
            // Increments the memory pointer
            transmittingAddress = transmittingAddress + 4;
            // Sends the data to the local router
            ppmPacketnetWrite(handles.dataPort, &usFlit, sizeof(usFlit));
        }
        // If the packet transmittion is done, change the NI status to IDLE
        //bhmMessage("INFO", "NIITERATION", "transmittingCount %x - control_RX %x - control_TX %x\n",transmittingCount, control_RX, control_TX);
        if(transmittingCount == EMPTY && control_RX != NI_STATUS_INTER){
            //bhmMessage("INFO", "NIITERATION", "Terminando envio!!!\n");
            control_TX = NI_STATUS_INTER; // Changes the TX status to INTERRUPTION
            writeMem(htonl(NI_INT_TYPE_TX), intTypeAddr); // Writes the interruption type to the processor
            ppmWriteNet(handles.INT_NI, 1); // Turns the interruption on
        }
    }

    // This is needed to handle the interruption delivery when the packet arrived during another interruption
    if(control_RX == NI_STATUS_ON && receivingCount == EMPTY && control_TX != NI_STATUS_INTER){
        //bhmMessage("INFO", "NIITERATION", "-------------------------------___SPECIAL!!!!\n");
        control_RX = NI_STATUS_INTER;
        writeMem(htonl(NI_INT_TYPE_RX), intTypeAddr); // Writes the interruption type to the processor
        ppmWriteNet(handles.INT_NI, 1); // Turns the interruption on
    }
}

void resetAddress(){
    receivingAddress = addressStart;
}
////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// Callback stubs ////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
PPM_REG_READ_CB(addressRead) {
    // YOUR CODE HERE (addressRead)
    return *(Uns32*)user;
}

PPM_REG_WRITE_CB(addressWrite) {
    // In the beggining of everything the PE will write two addresses in the NI. They will be used to write the incomming packet and the interruptiontype(RX or TX) 
    if(addressStart == 0xFFFFFFFF){
        addressStart = htonl(data);
    }
    else if(intTypeAddr == 0xFFFFFFFF){
        intTypeAddr = htonl(data);
        statusUpdate(IDLE);
    }
    else{
        //bhmMessage("INFO", "ADDRWR", "Recebendo um endereço...\n");
        auxAddress = htonl(data);
        //setSTALL(); // changing to TX state
    }
    *(Uns32*)user = data;
}

PPM_PACKETNET_CB(controlPortUpd) {
    if(bytes == 4){
        unsigned int ctrl = *(unsigned int *)data;
        control_in_STALLGO = ctrl;
    }
    else if(bytes == 8) {
        niIteration();
    }
}

// Receiving a flit from the router...
PPM_PACKETNET_CB(dataPortUpd) {
    unsigned int flit = *((unsigned int*)data);
    //bhmMessage("I", "FLIT", "Flit: %x\n",htonl(flit));
    // This will happen if the NI is receiving a service packet when it is in a idle state
    if(control_RX == NI_STATUS_OFF){
        //statusUpdate(RX);
        control_RX = NI_STATUS_ON;
        resetAddress();
        receivingField = HEADER;
        receivingCount = 0xFF; // qqrcoisa
        setGO();
    }
    // Receiving process
    if(control_RX == NI_STATUS_ON){
        if(receivingField == HEADER){
            receivingField = SIZE;
            writeMem(flit, receivingAddress);
            receivingAddress = receivingAddress + 4;    // Increments the pointer, to write the next flit
        }
        else if(receivingField == SIZE){
            receivingField = PAYLOAD;
            receivingCount = htonl(flit);
            writeMem(flit, receivingAddress);
            receivingAddress = receivingAddress + 4;    // Increments the pointer, to write the next flit
        }
        else{
            receivingCount = receivingCount - 1;
            writeMem(flit, receivingAddress);
            receivingAddress = receivingAddress + 4;    // Increments the pointer, to write the next flit
        }
    }
    //bhmMessage("INFO", "RECEIVING", "==x=x=x=x=x=x=xx=x=x=x=: %x",receivingCount);
    //bhmMessage("INFO", "NIITERATION", "receivingCount %x - control_RX %x - control_TX %x\n",receivingCount, control_RX, control_TX);
    // Detects the receiving finale
    if(receivingCount == EMPTY){
        setSTALL();
        if(control_TX != NI_STATUS_INTER){
            control_RX = NI_STATUS_INTER;
            writeMem(htonl(NI_INT_TYPE_RX), intTypeAddr); // Writes the interruption type to the processor
            ppmWriteNet(handles.INT_NI, 1); // Turns the interruption on
        }
    }
}

PPM_REG_READ_CB(statusRead) {
    DMAC_ab8_data.status.value = htonl(control_TX);
    //bhmMessage("INFO", "INFORMINGITERATION", "Informando iteracao!\n");
    informIteration();  // When the processor is reading the NI status, we have one of two situations: 
                        //      (i) the processor is blocked by a Receive() function or
                        //      (ii) it is waiting the NI to enter in IDLE to start a new transmittion
                        // for both situations we need to generate iterations that will be communicated to the iterator by the router that is attached to this NI
    return *(Uns32*)user;
}

PPM_REG_WRITE_CB(statusWrite) {
    unsigned int command = htonl(data);
    //bhmMessage("I", "COMANDO", "Recebido %x\n",command);
    if(command == TX){
        if(control_TX == NI_STATUS_OFF){
            control_TX = NI_STATUS_ON;
            transmittingAddress = auxAddress;
            transmittingCount = HEADER;
            niIteration();  // Send a flit to the ROUTER, this way it will inform the iterator that this PE is waiting for "iterations"
        }
        else{
            bhmMessage("I", "statusDUMP", "controlTX: %x --- controlRX: %x --- command: %x", control_TX, control_RX, command);
            bhmMessage("I", "statusWrite", "ERROR_TX: UNEXPECTED STATE REACHED"); while(1){}
        }
    }
    else if(command == READING){
        if(control_RX == NI_STATUS_INTER){
            // Turn the interruption signal down
            ppmWriteNet(handles.INT_NI, 0);  
        }
        else{
            bhmMessage("I", "statusWrite", "ERROR_READING: UNEXPECTED STATE REACHED"); while(1){}
        }
    }
    else if(command == DONE){
        if(control_RX == NI_STATUS_INTER){
            control_RX = NI_STATUS_OFF;
            setGO();
        }
        else if(control_TX == NI_STATUS_INTER){
            control_TX = NI_STATUS_OFF;
            ppmWriteNet(handles.INT_NI, 0);  // Turn the interruption signal down
        }
        else{
            bhmMessage("I", "statusWrite", "ERROR_DONE: UNEXPECTED STATE REACHED"); while(1){}
        }
    }
    *(Uns32*)user = data;
}

PPM_CONSTRUCTOR_CB(constructor) {
    // YOUR CODE HERE (pre constructor)
    periphConstructor();
    myStatus = STALL;
    ppmPacketnetWrite(handles.controlPort, &myStatus, sizeof(myStatus));
}

PPM_DESTRUCTOR_CB(destructor) {
    // YOUR CODE HERE (destructor)
}


PPM_SAVE_STATE_FN(peripheralSaveState) {
    bhmMessage("E", "PPM_RSNI", "Model does not implement save/restore");
}

PPM_RESTORE_STATE_FN(peripheralRestoreState) {
    bhmMessage("E", "PPM_RSNI", "Model does not implement save/restore");
}

