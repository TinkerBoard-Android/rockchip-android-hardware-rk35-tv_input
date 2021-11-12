/*
 * Copyright 2019 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * author: rimon.xu@rock-chips.com
 *   date: 2019/12/31
 * module: sideband window
 */

//#define LOG_NDEBUG 0
//#define LOG_TAG "RTSidebandWindow"

#include "RTSidebandWindow.h"
#include "log/log.h"
#include <sys/time.h>
#include <utils/Timers.h>
#include <string.h>

#include "DrmVopRender.h"

namespace android {

#define MIN_BUFFER_COUNT_UNDEQUEUE      0

RTSidebandWindow::RTSidebandWindow()
        : mBuffMgr(nullptr),
          mVopRender(NULL),
          mThreadRunning(false),
          mMessageQueue("RenderThread", static_cast<int>(MESSAGE_ID_MAX)),
          mMessageThread(nullptr) {
    memset(&mSidebandInfo, 0, sizeof(mSidebandInfo));
    char prop_value[PROPERTY_VALUE_MAX] = {0};
    property_get("DEBUG_LEVEL_PROPNAME", prop_value, "0");
    mDebugLevel = (int)atoi(prop_value);
}

RTSidebandWindow::~RTSidebandWindow() {
    DEBUG_PRINT(mDebugLevel, "%s %d in", __FUNCTION__, __LINE__);
}

status_t RTSidebandWindow::init(RTSidebandInfo info) {
    ALOGD("%s %d in", __FUNCTION__, __LINE__);
    status_t    err = 0;
    bool        ready = false;

    mBuffMgr = common::TvInputBufferManager::GetInstance();

    if (info.structSize != sizeof(RTSidebandInfo)) {
        DEBUG_PRINT(3, "sideband info struct size is invailed!");
        goto __FAILED;
    }

    memcpy(&mSidebandInfo, &info, sizeof(RTSidebandInfo));
    ALOGD("RTSidebandWindow::init width=%d, height=%d, format=%d, usage=%d", mSidebandInfo.width, mSidebandInfo.height, mSidebandInfo.format, mSidebandInfo.usage);

    mVopRender = android::DrmVopRender::GetInstance();
    if (!mVopRender->mInitialized) {
        ready = mVopRender->initialize();
        if (ready) {
            mVopRender->detect();
        }
    }

#if 0
    mMessageThread = std::unique_ptr<MessageThread>(new MessageThread(this, "VOP Render"));
    if (mMessageThread != NULL) {
        mMessageThread->run();
    }
#endif

    return 0;
__FAILED:
    return -1;
}

status_t RTSidebandWindow::release() {
    ALOGD("%s %d in", __FUNCTION__, __LINE__);
    requestExitAndWait();
    if (mMessageThread != NULL) {
        mMessageThread.reset();
        mMessageThread = NULL;
    }

    while (mRenderingQueue.size() > 0) {
        buffer_handle_t tmpBuffer = mRenderingQueue.front();
        mRenderingQueue.erase(mRenderingQueue.begin());
        mBuffMgr->Free(tmpBuffer);
    }

    return 0;
}

status_t RTSidebandWindow::start() {
    DEBUG_PRINT(mDebugLevel, "%s %d in", __FUNCTION__, __LINE__);
    return 0;
}

status_t RTSidebandWindow::stop() {
    DEBUG_PRINT(mDebugLevel, "%s %d in", __FUNCTION__, __LINE__);
    mVopRender->deinitialize();
    if (mVopRender) {
        delete mVopRender;
        mVopRender = NULL;
    }
    return 0;
}

status_t RTSidebandWindow::flush() {
    Message msg;
    memset(&msg, 0, sizeof(Message));
    msg.id = MESSAGE_ID_FLUSH;
    status_t status = mMessageQueue.send(&msg);

    return status;
}

status_t RTSidebandWindow::allocateBuffer(buffer_handle_t *buffer) {
    buffer_handle_t temp_buffer = NULL;
    uint32_t stride = 0;
    int ret = -1;

    ret = mBuffMgr->Allocate(mSidebandInfo.width,
                        mSidebandInfo.height,
                        mSidebandInfo.format,
                        mSidebandInfo.usage,
                        common::GRALLOC,
                        &temp_buffer,
                        &stride);
    if (!temp_buffer) {
        DEBUG_PRINT(3, "RTSidebandWindow::allocateBuffer mBuffMgr->Allocate failed !!!");
    } else {
        *buffer = temp_buffer;
        ret = 0;
    }
    
    return ret;
}

status_t RTSidebandWindow::allocateSidebandHandle(buffer_handle_t *handle) {
    buffer_handle_t temp_buffer = NULL;
    uint32_t stride = 0;
    int ret = -1;

    ret = mBuffMgr->Allocate(mSidebandInfo.width,
                        mSidebandInfo.height,
                        mSidebandInfo.format,
                        0,
                        common::GRALLOC,
                        &temp_buffer,
                        &stride);
    if (!temp_buffer) {
        DEBUG_PRINT(3, "RTSidebandWindow::allocateSidebandHandle mBuffMgr->Allocate failed !!!");
    } else {
        *handle = temp_buffer;
        ret = 0;
    }
    return ret;
}

status_t RTSidebandWindow::freeBuffer(buffer_handle_t *buffer) {
    DEBUG_PRINT(0, "%s %d in unregister", __FUNCTION__, __LINE__);
    // android::Mutex::Autolock _l(mLock);
    if (*buffer) {
        mBuffMgr->Free(*buffer);
    }

    return 0;
}

status_t RTSidebandWindow::remainBuffer(buffer_handle_t buffer) {
    DEBUG_PRINT(mDebugLevel, "remainBuffer buffer: %p", buffer);
    // android::Mutex::Autolock _l(mLock);
    mRenderingQueue.push_back(buffer);
    return 0;
}


status_t RTSidebandWindow::dequeueBuffer(buffer_handle_t *buffer) {
    buffer_handle_t tmpBuffer = NULL;
    status_t status = 0;
    ALOGD("dequeueBuffer size: %d", (int32_t)mRenderingQueue.size());
    if (mRenderingQueue.size() > MIN_BUFFER_COUNT_UNDEQUEUE) {
        tmpBuffer = mRenderingQueue.front();
        Message msg;
        memset(&msg, 0, sizeof(Message));
        msg.id = MESSAGE_ID_DEQUEUE_REQUEST;
        status = mMessageQueue.send(&msg);

    }
    *buffer = tmpBuffer;
    return status;
}

status_t RTSidebandWindow::queueBuffer(buffer_handle_t buffer) {
    (void)buffer;
    ALOGD("%s %d in buffer: %p queue size: %d", __FUNCTION__, __LINE__, buffer, (int32_t)mRenderingQueue.size());
    Message msg;
    memset(&msg, 0, sizeof(Message));
    msg.id = MESSAGE_ID_RENDER_REQUEST;
    msg.streamBuffer.buffer = buffer;

    status_t status = mMessageQueue.send(&msg);
    return status;
}

status_t RTSidebandWindow::setBufferGeometry(int32_t width, int32_t height, int32_t format) {
    DEBUG_PRINT(mDebugLevel, "%s %d width=%d height=%d in", __FUNCTION__, __LINE__, width, height);
    mSidebandInfo.width = width;
    mSidebandInfo.height = height;
    mSidebandInfo.format = format;

    return 0;
}

status_t RTSidebandWindow::setCrop(int32_t left, int32_t top, int32_t right, int32_t bottom) {
    mSidebandInfo.left = left;
    mSidebandInfo.top = top;
    mSidebandInfo.right = right;
    mSidebandInfo.bottom = bottom;

    return 0;
}


status_t RTSidebandWindow::requestExitAndWait()
{
    Message msg;
    memset(&msg, 0, sizeof(Message));
    msg.id = MESSAGE_ID_EXIT;
    status_t status = mMessageQueue.send(&msg, MESSAGE_ID_EXIT);
    //status |= mMessageThread->requestExitAndWait();
    return status;
}

void RTSidebandWindow::messageThreadLoop() {
    mThreadRunning = true;
    while (mThreadRunning) {
        status_t status = NO_ERROR;
        Message msg;
        mMessageQueue.receive(&msg);

        ALOGD("@%s, receive message id:%d", __FUNCTION__, msg.id);
        switch (msg.id) {
        case MESSAGE_ID_EXIT:
          status = handleMessageExit();
        break;
        case MESSAGE_ID_RENDER_REQUEST:
          status = handleRenderRequest(msg);
        break;
        case MESSAGE_ID_DEQUEUE_REQUEST:
          status = handleDequeueRequest(msg);
        break;
        case MESSAGE_ID_FLUSH:
          status = handleFlush();
        break;
        default:
          DEBUG_PRINT(3, "ERROR Unknown message %d", msg.id);
          status = BAD_VALUE;
        break;
        }

        if (status != NO_ERROR)
            DEBUG_PRINT(3, "error %d in handling message: %d", status, static_cast<int>(msg.id));
        DEBUG_PRINT(mDebugLevel, "@%s, finish message id:%d", __FUNCTION__, msg.id);
        mMessageQueue.reply(msg.id, status);
    }
}

status_t RTSidebandWindow::handleMessageExit() {
    mThreadRunning = false;
    return 0;
}

status_t RTSidebandWindow::handleRenderRequest(Message &msg) {
    buffer_handle_t buffer = msg.streamBuffer.buffer;
    ALOGD("%s %d buffer: %p in", __FUNCTION__, __LINE__, buffer);
    mVopRender->SetDrmPlane(0, mSidebandInfo.right - mSidebandInfo.left, mSidebandInfo.bottom - mSidebandInfo.top, buffer);

    mRenderingQueue.push_back(buffer);
    ALOGD("%s    mRenderingQueue.size() = %d", __FUNCTION__, (int32_t)mRenderingQueue.size());

    return 0;
}

status_t RTSidebandWindow::goDisplay(buffer_handle_t handle) {
    mVopRender->SetDrmPlane(0, mSidebandInfo.right - mSidebandInfo.left, mSidebandInfo.bottom - mSidebandInfo.top, handle);
    return 0;
}

status_t RTSidebandWindow::clearVopArea() {
    ALOGD("RTSidebandWindow::clearVopArea()");
    mVopRender->DestoryFB();
    mVopRender->ClearDrmPlaneContent(0, mSidebandInfo.right - mSidebandInfo.left, mSidebandInfo.bottom - mSidebandInfo.top);
    return 0;
}

status_t RTSidebandWindow::handleDequeueRequest(Message &msg) {
    (void)msg;
    mRenderingQueue.erase(mRenderingQueue.begin());
    return 0;
}

status_t RTSidebandWindow::handleFlush() {
    while (mRenderingQueue.size() > 0) {
        buffer_handle_t buffer = NULL;
        buffer = mRenderingQueue.front();
        mRenderingQueue.erase(mRenderingQueue.begin());
        freeBuffer(&buffer);
    }

    return 0;
}

int RTSidebandWindow::getBufferHandleFd(buffer_handle_t buffer){
    if (!buffer) {
        DEBUG_PRINT(3, "%s param buffer is NULL.", __FUNCTION__);
        return -1;
    }
    return mBuffMgr->GetHandleFd(buffer);
}

int RTSidebandWindow::getBufferLength(buffer_handle_t buffer) {
    if (!buffer) {
        DEBUG_PRINT(3, "%s param buffer is NULL.", __FUNCTION__);
        return -1;
    }
    return mBuffMgr->GetHandleBufferSize(buffer);
}

int RTSidebandWindow::importHidlHandleBuffer(buffer_handle_t rawHandle, buffer_handle_t* outBufferHandle) {
    ALOGD("%s rawBuffer :%p", __FUNCTION__, rawHandle);
    if (rawHandle) {
        if(!mBuffMgr->ImportBufferImpl(rawHandle, outBufferHandle)) {
            return getBufferHandleFd(*outBufferHandle);
        } else {
            ALOGE("%s ImportBufferImpl failed.", __FUNCTION__);
        }
    }
    return -1;
}
static int writeNums = 0;
static int writeNums2 = 0;
int RTSidebandWindow::buffCopy(buffer_handle_t srcHandle, buffer_handle_t dstRawHandle) {
    ALOGD("%s in srcHandle=%p, dstRawHandle=%p", __FUNCTION__, srcHandle, dstRawHandle);
    if (srcHandle && dstRawHandle) {
        buffer_handle_t *outBufferHandle = &dstRawHandle;
        void *tmpSrcPtr = NULL, *tmpDstPtr = NULL;
        int srcDatasize = -1;
        // if(!mBuffMgr->Register(dstRawHandle, outBufferHandle)) {
        if(!mBuffMgr->ImportBufferImpl(dstRawHandle, outBufferHandle)) {
            ALOGD("out buffer handle fd = %d", getBufferHandleFd(*outBufferHandle));
            // ALOGD("dst buffer handle fd = %d", mBuffMgr->GetHandleFd(*outBufferHandle));
            int lockMode = GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK | GRALLOC_USAGE_HW_CAMERA_MASK;
            mBuffMgr->Lock(srcHandle, lockMode, 0, 0, mBuffMgr->GetWidth(srcHandle), mBuffMgr->GetHeight(srcHandle), &tmpSrcPtr);
            for (int i = 0; i < mBuffMgr->GetNumPlanes(srcHandle); i++) {
                srcDatasize += mBuffMgr->GetPlaneSize(srcHandle, i);
            }
    FILE* fp = NULL;
    char fileName[128] = {0};
if (writeNums2 < 4) {
    sprintf(fileName, "/data/system/tv_input_src_dump_%d.yuv", writeNums2);
    fp = fopen(fileName, "wb+");
    if (fp != NULL) {
        if (fwrite(tmpSrcPtr, srcDatasize, 1, fp) <= 0) {
            ALOGE("fwrite %s failed.", fileName);
        } else {
            ALOGD("fwirte %s success", fileName);
        }
    } else {
        ALOGE("open failed");
    }
    writeNums2++;
    fclose(fp);
}
ALOGD("data tmpSrcPtr ptr = %p, srcDatasize=%d", tmpSrcPtr, srcDatasize);
            mBuffMgr->LockLocked(*outBufferHandle, lockMode, 0, 0, mBuffMgr->GetWidth(*outBufferHandle), mBuffMgr->GetHeight(*outBufferHandle), &tmpDstPtr);
ALOGD("data tmpDstPtr ptr = %p, width=%d, height=%d", tmpDstPtr, mBuffMgr->GetWidth(*outBufferHandle), mBuffMgr->GetHeight(*outBufferHandle));
            std::memcpy(tmpDstPtr, tmpSrcPtr, srcDatasize);
if (writeNums < 4) {
// if (false) {
    memset(fileName, 0, 128);
    sprintf(fileName, "/data/system/tv_input_result_dump_%d.yuv", writeNums);
    fp = fopen(fileName, "wb+");
    if (fp != NULL) {
        if (fwrite(tmpDstPtr, srcDatasize, 1, fp) <= 0) {
            ALOGE("fwrite %s failed.", fileName);
        } else {
            ALOGD("fwirte %s success", fileName);
        }
    } else {
        ALOGE("open failed");
    }
    writeNums++;
    fclose(fp);
}
            mBuffMgr->UnlockLocked(*outBufferHandle);
            mBuffMgr->Unlock(srcHandle);
            // mBuffMgr->Deregister(*outBufferHandle);
            ALOGV("%s end", __FUNCTION__);
            return 0;
        }
    }
    return -1;
}

int RTSidebandWindow::registerHidlHandleBuffer(buffer_handle_t rawHandle, buffer_handle_t* outBufferHandle) {
    ALOGD("%s rawBuffer :%p", __FUNCTION__, rawHandle);
    if (rawHandle) {
        if(!mBuffMgr->Register(rawHandle, outBufferHandle)) {
            return getBufferHandleFd(*outBufferHandle);
        } else {
            ALOGE("%s ImportBufferImpl failed.", __FUNCTION__);
        }
    }
    return -1;
}

void RTSidebandWindow::getBufferDataLocked(buffer_handle_t srcBufferHandle, void** dstDataPtr, int *dstDataSize) {
    void *tmpPtr = NULL;
    int size = -1;
    int lockMode = GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK | GRALLOC_USAGE_HW_CAMERA_MASK;
    mBuffMgr->Lock(srcBufferHandle, lockMode, 0, 0, mBuffMgr->GetWidth(srcBufferHandle), mBuffMgr->GetHeight(srcBufferHandle), &tmpPtr);
    for (int i = 0; i < mBuffMgr->GetNumPlanes(srcBufferHandle); i++) {
        size += mBuffMgr->GetPlaneSize(srcBufferHandle, i);
    }
    *dstDataPtr = tmpPtr;
    *dstDataSize = size;
    if (*dstDataPtr) {
        ALOGD("%s ptr=%p, dataSize=%d", __FUNCTION__, *dstDataPtr, *dstDataSize);
    } else {
        ALOGE("%s failed", __FUNCTION__);
    }
}

void RTSidebandWindow::unLockBufferHandle(buffer_handle_t srcBufferHandle) {
    mBuffMgr->Unlock(srcBufferHandle);
}

int RTSidebandWindow::dumpImage(buffer_handle_t handle, char* fileName, int mode) {
    int ret = -1;
    if (!handle || !fileName) {
        DEBUG_PRINT(3, "%s param buffer is NULL.", __FUNCTION__);
        return ret;
    }
    ALOGD("%s handle :%p", __FUNCTION__, handle);
    FILE* fp = NULL;
    void *dataPtr = NULL;
    int dataSize = 0;
    fp = fopen(fileName, "wb+");
    if (fp != NULL) {
        struct android_ycbcr ycbrData;
        int lockMode = GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK | GRALLOC_USAGE_HW_CAMERA_MASK;
        if (mode == 1) {
            mBuffMgr->LockYCbCr(handle, lockMode, 0, 0, mBuffMgr->GetWidth(handle), mBuffMgr->GetHeight(handle), &ycbrData);
            dataPtr = ycbrData.y;
        } else {
            ALOGD("width = %d", mBuffMgr->GetWidth(handle));
            ALOGD("height = %d", mBuffMgr->GetHeight(handle));
            mBuffMgr->Lock(handle, lockMode, 0, 0, mBuffMgr->GetWidth(handle), mBuffMgr->GetHeight(handle), &dataPtr);
        }
        ALOGD("planesNum = %d", mBuffMgr->GetNumPlanes(handle));
        for (int i = 0; i < mBuffMgr->GetNumPlanes(handle); i++) {
        ALOGD("planesSize = %zu", mBuffMgr->GetPlaneSize(handle, i));
            dataSize += mBuffMgr->GetPlaneSize(handle, i);
        }
        if (dataSize <= 0) {
            ALOGE("dataSize <= 0 , it can't write file.");
            ret = -1;
        } else {
            if (fwrite(dataPtr, dataSize, 1, fp) <= 0) {
                ALOGE("fwrite %s failed.", fileName);
                ret = -1;
            }
        }
        fclose(fp);
        mBuffMgr->Unlock(handle);
        ALOGI("Write data success to %s",fileName);
        ret = 0;
    } else {
        DEBUG_PRINT(3, "Create %s failed(%p, %s)", fileName, fp, strerror(errno));
    }
    return ret;
}

}
