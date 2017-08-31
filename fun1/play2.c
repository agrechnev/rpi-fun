// Re-typing of play.c by Oleksiy Grechnyev

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>

#include <bcm_host.h>

#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>
#include <IL/OMX_Video.h>
#include <IL/OMX_Broadcom.h>

// Dunno where this is originally stolen from...
#define OMX_INIT_STRUCTURE(a) \
    memset(&(a), 0, sizeof(a)); \
    (a).nSize = sizeof(a); \
    (a).nVersion.nVersion = OMX_VERSION; \
    (a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
    (a).nVersion.s.nVersionMajor = OMX_VERSION_MINOR; \
    (a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
    (a).nVersion.s.nStep = OMX_VERSION_STEP 

// Global used to quit
static int wannaQuit = 0;

// App context
typedef struct {
    OMX_HANDLETYPE camera;
    OMX_BUFFERHEADERTYPE *cameraPPBufferIn;
    int cameraReady;
    OMX_HANDLETYPE render;
    OMX_HANDLETYPE nullSink;
    int flushed;
    VCOS_SEMAPHORE_T handlerLock;
} appCtx;

//====================================================
static void say(const char * msg){
    fputs(msg, stderr);
}
//====================================================
static void die(const char * msg){
    fputs(msg, stderr);
    exit(1);
}
//====================================================
static void dieOmx(OMX_ERRORTYPE err, const char * msg){
    fprintf(stderr, "Error 0x%08x ; %s \n", err, msg);
    exit(1);
}
//====================================================
// Check for errors
static void check(OMX_ERRORTYPE r,  const char * msg){
    if (r != OMX_ErrorNone)
        dieOmx(r, msg);
}
//====================================================
// Change a state of a component
static void changeState(OMX_HANDLETYPE hand, OMX_STATETYPE state) {
    check(OMX_SendCommand(hand, OMX_CommandStateSet, state, NULL), "Change State");
    
    OMX_STATETYPE state2;
    for(;;){
        OMX_GetState(hand, &state2);
        if (state == state2)
            break;
        usleep(10000);
    }
}
//====================================================
static void blockUntilPortChanged(OMX_HANDLETYPE hand, OMX_U32 port, OMX_BOOL desired){
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    OMX_INIT_STRUCTURE(portdef);
    portdef.nPortIndex = port;
    for (;;){
        check(OMX_GetParameter(hand, OMX_IndexParamPortDefinition, &portdef), "Get port def");
        if (portdef.bEnabled == desired)
            break;
        usleep(10000);
    }
}
//====================================================
static void disablePort(OMX_HANDLETYPE hand, OMX_U32 port){
    check(OMX_SendCommand(hand, OMX_CommandPortDisable, port, NULL), "Disbale Port");
    blockUntilPortChanged(hand, port, OMX_FALSE);
}
//---
static void enablePort(OMX_HANDLETYPE hand, OMX_U32 port){
    check(OMX_SendCommand(hand, OMX_CommandPortEnable, port, NULL), "Enable Port");
    blockUntilPortChanged(hand, port, OMX_TRUE);
}
//====================================================
// Lock/unlock the semaphore/mutex
static void lock(appCtx *ctx){
    vcos_semaphore_wait(&ctx->handlerLock);
}
//---
// Lock/unlock the semaphore/mutex
static void unlock(appCtx *ctx){
    vcos_semaphore_post(&ctx->handlerLock);
}
//====================================================
// Signal handler
static void signalHandler(int signal){
    wannaQuit = 1;
}
//====================================================
// OMX event handler -- important, I guess
static OMX_ERRORTYPE eventHandler(
        OMX_HANDLETYPE hand,
        OMX_PTR pAD,
        OMX_EVENTTYPE e,
        OMX_U32 nD1,
        OMX_U32 nD2,
        OMX_PTR pED){
    appCtx *ctx = (appCtx *)pAD;
    switch (e){
        case OMX_EventCmdComplete:
            lock(ctx);
            if (nD1 == OMX_CommandFlush) 
                ctx->flushed = 1;
            unlock(ctx);
            break;
        case OMX_EventParamOrConfigChanged:
            lock(ctx);
            if (nD2 == OMX_IndexParamCameraDeviceNumber)
                ctx->cameraReady = 1;
            unlock(ctx);
            break;
        case OMX_EventError:
            dieOmx(nD1, "Error Event");
        default: ;
    }
    
    return OMX_ErrorNone;
}
//====================================================
//====================================================
int main(){
    bcm_host_init();
    
    return 0;
}