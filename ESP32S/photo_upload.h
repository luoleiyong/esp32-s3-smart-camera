/**
 * Photo Upload Interface
 *
 * 将 SD 卡中的照片以 multipart/form-data 形式 POST 到用户配置的上传地址。
 * 表单字段固定为 photo（文件名沿用原文件名，Content-Type: image/jpeg），
 * 因此兼容 Telegram Bot API 的 sendPhoto 接口（chat_id 放在 URL 查询串里）。
 */

#ifndef PHOTO_UPLOAD_H
#define PHOTO_UPLOAD_H

#include <Arduino.h>

// 上传 SD 卡中的指定文件（filename 为含前导 '/' 的路径）
// 成功（HTTP 2xx）返回 true；未配置地址 / 打开失败 / 超时 / 非 2xx 返回 false。
// 内部使用流式发送，不会把整张图额外拷贝成一份大缓冲。
bool photoUploadFile(const char *filename);

#endif