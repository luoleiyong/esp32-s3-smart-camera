/**
 * Video Record Interface
 *
 * 机内录像：把摄像头 JPEG 帧封装成 AVI（MJPEG）直接写入 SD 卡，
 * 脱离浏览器也能录像。AVI 结构参照 s60sc/ESP32-CAM_MJPEG2SD 的 RIFF/AVI 写法。
 */

#ifndef VIDEO_RECORD_H
#define VIDEO_RECORD_H

#include <Arduino.h>

// 启动录像：暂停预览→切 VIDEO_JPEG→建 AVI 文件写头部；返回是否成功
bool videoRecordStart();

// 停止录像：补写索引与长度字段、关闭文件、恢复预览模式
void videoRecordStop();

// 是否正在录像
bool videoRecordIsActive();

// 由主循环每次调用：到帧间隔就抓一帧写入（内部自判时间，未录像时立即返回）
void videoRecordUpdate();

// 当前录像文件名（未录像时为空串）
const char* videoRecordFileName();

#endif