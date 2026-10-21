/**
 * TFT Display Interface (White Theme)
 *
 * 硬件：1.44 寸 ST7735S，128×128
 * 白底界面 + U8g2 中文渲染，显示风格参照 SmartTargetV2 项目：
 * 顶部标题栏（深色底白字）、底部状态栏。
 * 中文使用 U8g2 文泉驿 GB2312 点阵字体，无需自建字库。
 */

#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <TJpg_Decoder.h>
#include <SPI.h>
#include "camera_config.h"

// ===== 白底主题配色（RGB565，参照 SmartTargetV2）=====
#define UI_BG_COLOR     0xFFFF   // 页面底色（白）
#define UI_TEXT_COLOR   0x0000   // 正文文字（黑）
#define UI_TITLE_BG     0x0041   // 标题栏底色
#define UI_ACCENT       0x281F   // 强调色
#define UI_HINT_COLOR   0x9CD3   // 提示文字
#define UI_DIVIDER      0xE73C   // 分隔线/状态栏底色
#define UI_SELECT_BG    0xFCFD   // 选中行底色

// ===== 屏幕尺寸 =====
#define TFT_WIDTH       128      // 屏幕宽（ST7735S 1.44"）
#define TFT_HEIGHT      128      // 屏幕高

// ===== 界面布局参数 =====
#define UI_TITLE_H      16       // 标题栏高度
#define UI_STATUS_Y     112      // 底部状态栏 Y
#define UI_STATUS_H     16       // 底部状态栏高度

#define PREVIEW_X       0        // 预览区 X
#define PREVIEW_Y       16       // 预览区 Y
#define PREVIEW_W       128      // 预览区宽
#define PREVIEW_H       96       // 预览区高

// TFT 显示实例与 JPEG 输出偏移
extern Adafruit_ST7735 tft;
extern int16_t display_offset_x;
extern int16_t display_offset_y;

// 初始化 TFT 显示硬件、SPI 接口与 U8g2 中文字体
bool tftInit();

// 以白底清屏
void tftClear();

// 绘制中文/UTF-8 文本（x、y 为文本左上角，large 使用大号字体）
void tftDrawCN(int x, int y, const char *text, uint16_t fg, bool large = false);

// 以屏幕宽度居中绘制中文/UTF-8 文本
void tftDrawCNCenter(int y, const char *text, uint16_t fg, bool large = false);

// 绘制顶部标题栏（深色底 + 白色居中标题 + 分隔线）
void tftDrawTitleBar(const char *title);

// 绘制预览界面框（标题栏、预览区、底部提示与状态栏）
void tftDrawUIFrame();

// 刷新状态栏（WiFi 状态、时间）
void tftUpdateStatusBar(bool wifiConnected, const char *ipStr);

// 绘制指定预览帧（RGB565）：调用方无需再持锁，本函数内部已加锁
void tftDrawPreviewFrame(camera_fb_t *fb);

// 绘制指定预览帧（JPEG，TJpgDec 解码）：调用方无需再持锁，本函数内部已加锁
void tftDrawPreviewFrameJPEG(camera_fb_t *fb);

// 显示摄像头预览帧（RGB565）：内部取帧并调用 tftDrawPreviewFrame 绘制
void tftShowPreviewRGB565();

// 显示摄像头预览帧（JPEG 格式，TJpgDec 解码）
void tftShowPreviewJPEG();

// 快门闪烁动画
void tftShutterAnimation();

// 居中显示提示信息并可选延时
void tftShowMessage(const char *msg, uint16_t color, int delayMs);

// 显示拍照已保存提示
void tftShowPhotoSaved(const char *filename);

// 绘制图库浏览信息（当前张数/总数与操作提示）
void tftDrawGalleryInfo(int current, int total);

// 显示 SD 卡中指定照片
void tftDisplayPhoto(int index, const String &filename);

// TJpgDec 解码回调
bool tftJpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap);

// RGB565 字节序转换
uint16_t swapRGB565(uint16_t color);

// 整块 RGB565 缓冲区字节序转换
void swapRGB565Buffer(uint16_t *buffer, int32_t length);

#endif