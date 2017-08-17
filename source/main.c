#include <3ds.h>
#include "srvsys.h"

void __appInit() {
    srvSysInit();
}

void __appExit() {
    srvSysExit();
}

void __sync_init();
void __sync_fini();
void __system_initSyscalls();

void __ctru_exit() {
    __appExit();
    __sync_fini();
    svcExitProcess();
}

void initSystem() {
    __sync_init();
    __system_initSyscalls();
    __appInit();
}
Handle serviceHandles[11];
Handle serviceSessions[5]= {0,0,0,0,0};
int handleCount=0;
Handle getSessionHandle(int session) {
    return serviceSessions[session];
}
void setSessionHandle(int session, Handle handle) {
    serviceSessions[session]=handle;
}
void clearSessionHandle(int session) {
    serviceSessions[session]=0;
}
int findSession(Handle handle) {
    for(int i=0;i<5;i++)
        if(serviceSessions[i]==handle)
            return i;
    return -1;
}
void repackSessions() {
    handleCount=6;
    for(int i=0;i<5;i++) {
        if(serviceSessions[i]) {
            serviceHandles[handleCount++]=serviceSessions[i];
        }
    }
}
u32 *cmdbuf;
void pdn_s() {
   int command = cmdbuf[0] >> 16;
   uint32_t v5[2];
   switch(command) {
        case 1: //Read CFG11_PTM_*
            cmdbuf[0] = 0x100C0;
            cmdbuf[1] = 0;
            cmdbuf[2] = *((u32*)0x1EC41008);
            cmdbuf[3] = *((u32*)0x1EC4100C);
            break;
        case 2: //Set CFG11_PTM_*
            *(u64*)v5 = *(u64*)(cmdbuf+1);
            v5[1] &= ~v5[0];
            *((u64*)0x1EC41008) = *(u64*)v5;
            cmdbuf[0] = 0x20040;
            cmdbuf[1] = 4;
            break;
        case 3: //Set CFG11_PTM1
            *((u32*)0x1EC4100C) = cmdbuf[1];
            cmdbuf[0] = 0x30040;
            cmdbuf[1] = 0;
            break;
        default:
            cmdbuf[0] = 64;
            cmdbuf[1] = 0xD900182F;
            break;
   }
}
void pdn_d() { //Start up DSP
    int command = cmdbuf[0] >> 16;
    if(command != 1) {
        cmdbuf[0] = 64;
        cmdbuf[1] = 0xD900182F;
        return;
    }
    cmdbuf[0] = 0x10040;
    u8 a=cmdbuf[1],b=cmdbuf[2],c=cmdbuf[3];
    if (c ^ ~a) {
        cmdbuf[1] = 0xE0E02401;
        return;
    }
    *((u8*)0x1EC41230) = b^1 | 2 * a;
    if(b & c) {
        volatile int i=48;
        while(i--);
        *((u8*)0x1EC41230) = 2 * c | 1;
    }
    cmdbuf[1]=0;
}
void pdn_i() {
    int command = cmdbuf[0] >> 16;
    switch(command) {
        case 1:
            *((u8*)0x1EC41220) = cmdbuf[1] | *((u8*)0x1EC41220) & 0xFE; //Change unknown bit
            cmdbuf[0]=0x10040;
            cmdbuf[1]=0;
            break;
        case 2:
            *((u8*)0x1EC41220) = cmdbuf[1] | *((u8*)0x1EC41220) & 0xFD; //Change dsp start bit
            cmdbuf[0]=0x10040;
            cmdbuf[1]=0;
            break;
        default:
            cmdbuf[0]=0x40;
            cmdbuf[1]=0xD900182F;
            break;
    }
}
void pdn_g() { //Start up GPU
    int command = cmdbuf[0] >> 16;
    if(command != 1) {
        cmdbuf[0]=64;
        cmdbuf[1]=0xD900182F;
        return;
    }
    cmdbuf[0]=0x10040;
    u32 a=cmdbuf[1], b=cmdbuf[2], c=cmdbuf[3];
    cmdbuf[1] = 0;
    if((b|c) & ~c) {
        cmdbuf[1]=0xE0E02401;
        return;
    }
    u32 flag=c?0x10000:0;
    u32 val = flag | b ^ 1;
    if(c == 0)
        val |= 0x7E;
    *((u32*)0x1EC41200) = val;
    if(b | c) {
        volatile int i=12;
        while(i--);
        *((u32*)0x1EC41200)=flag | 0x7F;
    }
}
void pdn_c() {
    int command = cmdbuf[0] >> 16;
    switch(command) {
        case 1: //Change unknown bit
            *((u8*)0x1EC41224) = cmdbuf[1] | *((u8*)0x1EC41220) & 0xFE;
            cmdbuf[0]=0x10040;
            cmdbuf[1]=0;
            break;
        case 2: //Change camera enabled
            *((u8*)0x1EC41224) = cmdbuf[1] | *((u8*)0x1EC41220) & 0xFD;
            cmdbuf[0]=0x20040;
            cmdbuf[1]=0;
            break;
        default:
            cmdbuf[0]=0x40;
            cmdbuf[1]=0xD900182F;
    }
}
void handle_commands(int service) {
    switch(service) {
        case 0:
            return pdn_s();
        case 1:
            return pdn_d();
        case 2:
            return pdn_i();
        case 3:
            return pdn_g();
        case 4:
            return pdn_c();
    }
}
#define assert(x) {\
    if(!(x)) \
        svcBreak(USERBREAK_ASSERT); \
}
static Result should_terminate(int *term_request) {
    u32 notid;
    Result ret;

    ret = srvSysReceiveNotification(&notid);
    if(R_FAILED(ret))
        return ret;
    if(notid==0x100)
        *term_request=1;
    return 0;
}
int main() {
    Result ret;
    Handle handle;
    Handle reply_target;
    Handle *srv_handle;
    Handle *notification_handle;
    s32 index;
    int i;
    int term_request;
    ret = 0;

    srv_handle = serviceHandles;
    notification_handle = serviceHandles+5;
    char *serviceNames[] = {
        "pdn:s",
        "pdn:d",
        "pdn:i",
        "pdn:g",
        "pdn:c"
    };
    for(i=0;i<5; i++)
        if(R_FAILED(srvSysRegisterService(srv_handle+i, serviceNames[i], 1)))
            svcBreak(USERBREAK_ASSERT);
        else
            handleCount++;

    if(R_FAILED(srvSysEnableNotification(srv_handle)))
        svcBreak(USERBREAK_ASSERT);
    else
        handleCount++;
    index=1;
    reply_target=0;
    term_request=0;
    do {
        if(reply_target == 0) {
            cmdbuf = getThreadCommandBuffer();
            cmdbuf[0] = 0xFFFF0000;
        }
        ret = svcReplyAndReceive(&index, serviceHandles, handleCount, reply_target);
        if(R_FAILED(ret)) {
            assert(ret == (int)0xC920181A);
            if(index == -1) {
                for(i=6;i<10;i++) {
                    if(serviceHandles[i] == reply_target)
                    index = i;
                    break;
                }
            }
            svcCloseHandle(serviceHandles[index]);
            clearSessionHandle(findSession(serviceHandles[index]));
            repackSessions();
        } else {
            reply_target = 0;
            switch(index) {
                case 5:
                    if(R_FAILED(should_terminate(&term_request)))
                        svcBreak(USERBREAK_ASSERT);
                    break;
                case 0: //pdn:s
                case 1: //pdn:d
                case 2: //pdn:i
                case 3: //pdn:g
                case 4: //pdn:c
                    if(R_FAILED(svcAcceptSession(&handle, srv_handle[index])))
                        svcBreak(USERBREAK_ASSERT);
                    if((handleCount >= 11) || (serviceHandles[index]))
                        svcCloseHandle(handle);
                    setSessionHandle(index, handle);
                    repackSessions();
                    break;
                default: //Session
                    handle_commands(findSession(serviceHandles[index]));
                    reply_target = serviceHandles[index];
                    break;
            }
        }
    } while (!term_request || handleCount != 6);
    for(int i=0;i<5;i++) {
        srvSysUnregisterService(serviceNames[i]);
        svcCloseHandle(srv_handle[i]);
    }
    svcCloseHandle(*notification_handle);
    return 0;
}
