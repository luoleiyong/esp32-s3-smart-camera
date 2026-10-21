/**
 * Photo Upload Implementation
 *
 * 用 WiFiClient(明文) / WiFiClientSecure(https，兼容 Telegram) + HTTPClient
 * 把 SD 卡里的 JPEG 以 multipart/form-data POST 出去。
 * 关键点：自定义一个 Stream 子类，按「边界头 -> JPEG 数据 -> 结束边界」的顺序
 * 边读边吐；HTTPClient 用固定缓冲逐块读取该 Stream，因此全程只有一份固定的
 * TCP 发送缓冲，不会因为上传再把整张图拷一份大缓冲，也不会拼成 300KB 大块。
 */

#include "photo_upload.h"
#include "config_manager.h"
#include <SD_MMC.h>
#include <WiFi.h>
#include <NetworkClientSecure.h>   // https（Telegram）用，HTTPClient.h 已间接包含
#include <HTTPClient.h>

// multipart 边界串（固定即可，JPEG 二进制里出现该串的概率可忽略）
static const char *UPLOAD_BOUNDARY = "----ESP32S3CamBoundary7d3a";

/**
 * 顺序输出「边界头 + 文件内容 + 结束边界」的只读 Stream。
 * HTTPClient 通过 available()/readBytes() 逐块拉取，这里按当前偏移量
 * 决定数据来自头串、SD 文件还是尾串，文件本身不整体载入内存。
 */
class MultipartStream : public Stream {
public:
    void begin(File *file, size_t fileSize, const char *head, size_t headLen,
               const char *foot, size_t footLen) {
        _file = file;
        _fileSize = fileSize;
        _head = head; _headLen = headLen;
        _foot = foot; _footLen = footLen;
        _pos = 0;
        _total = headLen + fileSize + footLen;
    }

    size_t totalLen() const { return _total; }

    int available() override { return (int)(_total - _pos); }

    int read() override {
        uint8_t b;
        return readBytes((char *)&b, 1) == 1 ? (int)b : -1;
    }

    int peek() override { return -1; }

    size_t readBytes(char *buffer, size_t length) override {
        size_t n = 0;
        while (n < length && _pos < _total) {
            if (_pos < _headLen) {
                size_t c = _headLen - _pos;
                if (c > length - n) c = length - n;
                memcpy(buffer + n, _head + _pos, c);
                _pos += c; n += c;
            } else if (_pos < _headLen + _fileSize) {
                size_t off = _pos - _headLen;
                size_t remain = _fileSize - off;
                size_t c = remain;
                if (c > length - n) c = length - n;
                if (_file->position() != off) _file->seek(off);
                size_t r = _file->read((uint8_t *)(buffer + n), c);
                if (r == 0) break;    // 读失败：中断，避免死循环
                _pos += r; n += r;
            } else {
                size_t off = _pos - _headLen - _fileSize;
                size_t c = _footLen - off;
                if (c > length - n) c = length - n;
                memcpy(buffer + n, _foot + off, c);
                _pos += c; n += c;
            }
        }
        return n;
    }

    size_t write(uint8_t) override { return 0; }

private:
    File *_file = nullptr;
    size_t _fileSize = 0;
    const char *_head = nullptr; size_t _headLen = 0;
    const char *_foot = nullptr; size_t _footLen = 0;
    size_t _pos = 0;
    size_t _total = 0;
};

/**
 * 上传 SD 卡中的照片到配置的上传地址
 */
bool photoUploadFile(const char *filename) {
    DeviceConfig cfg = configGet();
    if (cfg.uploadUrl[0] == '\0') {
        Serial.println("[UPLOAD] No upload URL configured");
        return false;
    }

    File file = SD_MMC.open(filename, FILE_READ);
    if (!file) {
        Serial.printf("[UPLOAD] Open failed: %s\n", filename);
        return false;
    }
    size_t fileSize = file.size();

    // 取纯文件名（去掉前导路径），用于 multipart 的 filename 字段
    String name = String(filename);
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);

    char head[256];
    int headLen = snprintf(head, sizeof(head),
        "--%s\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"%s\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n",
        UPLOAD_BOUNDARY, name.c_str());

    char foot[80];
    int footLen = snprintf(foot, sizeof(foot), "\r\n--%s--\r\n", UPLOAD_BOUNDARY);

    MultipartStream stream;
    stream.begin(&file, fileSize, head, (size_t)headLen, foot, (size_t)footLen);

    String url = String(cfg.uploadUrl);

    HTTPClient http;
    http.setTimeout(10000);          // 10 秒超时，避免拖住主循环/网页任务
    http.setConnectTimeout(10000);

    // https（如 Telegram）需要 TLS 客户端；明文地址用普通 NetworkClient
    NetworkClient plainClient;
    NetworkClientSecure secureClient;
    NetworkClient *client;
    if (url.startsWith("https")) {
        secureClient.setInsecure();  // 不校验证书链（设备本地无 CA 库）
        client = &secureClient;
    } else {
        client = &plainClient;
    }

    if (!http.begin(*client, url)) {
        Serial.printf("[UPLOAD] begin failed: %s\n", url.c_str());
        file.close();
        return false;
    }

    http.addHeader("Content-Type",
                   String("multipart/form-data; boundary=") + UPLOAD_BOUNDARY);

    unsigned long t0 = millis();
    int code = http.sendRequest("POST", &stream, stream.totalLen());
    unsigned long dt = millis() - t0;
    http.end();
    file.close();

    Serial.printf("[UPLOAD] %s -> code=%d, %u bytes, %lums\n",
                  url.c_str(), code, (unsigned)fileSize, dt);
    return (code >= 200 && code < 300);
}