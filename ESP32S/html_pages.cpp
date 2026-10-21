/**
 * HTML Pages Implementation
 * 
 * Generates responsive HTML content for the camera's web interface.
 * Includes dashboard with live streaming, remote photo capture,
 * photo gallery with download/delete, and settings configuration.
 * 
 * Features:
 * - Mobile-friendly responsive design
 * - Real-time MJPEG streaming with canvas capture
 * - Browser-based video recording (WebM) using MediaRecorder API
 * - Photo gallery with thumbnail cards
 * - Settings form with sliders and checkboxes
 * - Runtime status page (/status): heap, PSRAM, SD, RSSI, fps, temperature
 */

#include "html_pages.h"
#include "camera_config.h"   // 提供 framesize_t 枚举，避免手写分辨率数值过期
#include <math.h>            // isnan()：温度读取失败时显示 "--"

/**
 * Generate checkbox HTML element with label
 */
static String checkbox(const char *id, const char *label, bool val) {
    String s = F("<label style='display:flex;align-items:center;gap:8px;cursor:pointer'>");
    s += F("<input type='checkbox' id='"); s += id; s += F("' name='"); s += id;
    s += F("' value='1'"); if (val) s += F(" checked");
    s += F(" style='width:18px;height:18px'>"); s += label; s += F("</label>");
    return s;
}

/**
 * Generate root dashboard page HTML
 * Includes buttons for live stream, remote capture, gallery, and settings.
 * Features MJPEG streaming with canvas-based video recording (WebM).
 */
String htmlGetRootPage(float totalMB, float usedMB) {
    String h = F(R"(<!DOCTYPE html><html lang='zh'><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1.0'>
<title>ESP32-S3 智能相机</title><style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Microsoft YaHei','PingFang SC',sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px;color:#333}
.c{background:#fff;border-radius:20px;padding:32px 28px;box-shadow:0 20px 60px rgba(0,0,0,.3);text-align:center;max-width:640px;width:100%}
.t{font-size:1.4em;margin-bottom:4px}
.s{color:#999;margin-bottom:18px;font-size:.85em}
.r{display:flex;flex-wrap:wrap;gap:10px;justify-content:center;margin-bottom:16px}
.b{display:inline-block;color:#fff;text-decoration:none;padding:11px 22px;border-radius:25px;font-size:.88em;font-weight:600;border:none;cursor:pointer;transition:transform .15s}
.b:hover{transform:scale(1.05)}
.bs{background:linear-gradient(135deg,#f093fb,#f5576c)}
.bc{background:linear-gradient(135deg,#4facfe,#00f2fe)}
.bg{background:linear-gradient(135deg,#667eea,#764ba2)}
.bx{background:linear-gradient(135deg,#43e97b,#38f9d7);color:#333}
.bst{background:linear-gradient(135deg,#11998e,#38ef7d)}
#v{margin-top:14px;border-radius:12px;overflow:hidden;display:none;background:#000;position:relative;min-height:260px}
#v img{width:100%;display:block;max-height:65vh;object-fit:contain}
#cv{display:none}
.ct{position:absolute;bottom:0;left:0;right:0;padding:10px 14px;background:linear-gradient(transparent,rgba(0,0,0,.85));display:flex;gap:8px;align-items:center;justify-content:center;flex-wrap:wrap}
#rb{background:#e74c3c;color:#fff;border:none;padding:8px 18px;border-radius:20px;font-weight:bold;cursor:pointer;font-size:.82em;display:flex;align-items:center;gap:5px}
#rb.on{background:#c0392b;animation:p 1s infinite}
#rd{width:8px;height:8px;background:#fff;border-radius:50%}
#rb.on #rd{animation:b 1s infinite}
#sb{background:rgba(255,255,255,.25);color:#fff;border:none;padding:8px 16px;border-radius:20px;cursor:pointer;font-size:.82em}
#rt{color:#fff;font-family:monospace;font-size:1em;min-width:68px;text-align:center}
#lg{margin-top:12px;padding:10px 14px;background:#f0f0f0;border-radius:10px;text-align:left;font-size:.82em;display:none}
.inf{margin-top:14px;padding:14px;background:#f5f5f5;border-radius:10px;text-align:left;font-size:.82em}
.inf h3{margin-bottom:8px}
.st{display:flex;justify-content:space-between;padding:5px 8px;margin:3px 0;background:#fff;border-radius:6px}
.sv{font-weight:600}.g{color:#27ae60}
@keyframes p{0%,100%{opacity:1}50%{opacity:.7}}
@keyframes b{0%,100%{opacity:1}50%{opacity:.3}}</style></head><body>
<div class='c'><h1 class='t'>ESP32-S3 智能相机</h1>
<p class='s'>系统就绪</p>
<div class='r'>
<button class='b bs' onclick='S()' id='sb'>📺 实时画面</button>
<button class='b bc' onclick='R()'>📸 远程拍照</button>
<button class='b bs' onclick='V()' id='vb'>⏺ 设备录像</button>
<a href='/list' class='b bg'>🖼 图库</a>
<a href='/config' class='b bx'>⚙ 设置</a>
<a href='/status' class='b bst'>📊 状态</a>
<a href='/update' class='b bst'>⬆ 固件升级</a></div>
<div id='v'><img id='vi' alt='视频流'>
<canvas id='cv'></canvas><div class='ct'>
<button id='rb' onclick='T()'><span id='rd'></span>录像</button>
<span id='rt'>00:00:00</span>
<button id='sb' onclick='X()'>停止</button></div></div>
<div id='lg'></div>
<div class='inf'><h3>💾 SD卡</h3>)");

    // Add SD card usage statistics
    h += F("<div class='st'><span>总容量</span><span class='sv'>");
    h += String(totalMB, 1); h += F(" MB</span></div>");
    h += F("<div class='st'><span>已用</span><span class='sv'>");
    h += String(usedMB, 1); h += F(" MB</span></div>");
    h += F("<div class='st'><span>可用</span><span class='sv g'>");
    h += String(totalMB - usedMB, 1); h += F(" MB</span></div></div></div>");

    // JavaScript for live streaming, recording, and remote capture
    h += F(R"(<script>
var M=null,C=[],t0=0,ti=null,di=null,fc=0;
function L(m){var g=document.getElementById('lg');g.style.display='block';g.textContent=m}
function S(){L('连接视频流...');var b=document.getElementById('v');b.style.display='block';
var i=document.getElementById('vi');i.src='/stream?_t='+Date.now();
i.onload=function(){L('视频流已连接')};i.onerror=function(){L('连接失败!')};
var cv=document.getElementById('cv'),ctx=cv.getContext('2d');
di=setInterval(function(){
if(i.naturalWidth>0){if(cv.width!==i.naturalWidth){cv.width=i.naturalWidth;cv.height=i.naturalHeight}
ctx.drawImage(i,0,0);fc++}},67);
document.getElementById('sb').textContent='运行中...';document.getElementById('sb').disabled=true}
function X(){L('停止视频流...');document.getElementById('vi').src='';
if(di){clearInterval(di);di=null}if(M&&M.state==='recording')M.stop();
document.getElementById('v').style.display='none';
document.getElementById('sb').textContent='📺 实时画面';document.getElementById('sb').disabled=false;
L('已停止,绘制'+fc+'帧');fc=0}
function T(){var b=document.getElementById('rb');
if(!M||M.state==='inactive'){var cv=document.getElementById('cv');
if(cv.width<10){alert('请等待视频流加载');return}L('开始录像...');
var s=cv.captureStream(15);
var o=[{mimeType:'video/webm;codecs=vp9'},{mimeType:'video/webm;codecs=vp8'},{mimeType:'video/webm'}];
var mr=null;for(var i=0;i<o.length;i++){try{mr=new MediaRecorder(s,o[i]);break}catch(e){}}
if(!mr){alert('浏览器不支持录像');return}M=mr;C=[];
M.ondataavailable=function(e){if(e.data.size>0)C.push(e.data)};
M.onstop=function(){if(C.length>0){var bl=new Blob(C,{type:'video/webm'});
var u=URL.createObjectURL(bl),a=document.createElement('a');a.href=u;
a.download='esp32cam_'+Date.now()+'.webm';a.click();
setTimeout(function(){URL.revokeObjectURL(u)},5000);L('已保存:'+Math.round(bl.size/1024)+'KB')}else L('无数据')}
M.start(1000);b.classList.add('on');b.innerHTML='<span id="rd"></span>录像中';
t0=Date.now();ti=setInterval(function(){var s=Math.floor((Date.now()-t0)/1000);
document.getElementById('rt').textContent=String(Math.floor(s/3600)).padStart(2,'0')+
':'+String(Math.floor(s/60)%60).padStart(2,'0')+':'+String(s%60).padStart(2,'0')},1000)}
else{L('停止录像');M.stop();b.classList.remove('on');b.innerHTML='<span id="rd"></span>录像';
clearInterval(ti)}}
function R(){L('拍照...');fetch('/capture',{method:'POST'}).then(function(r){L(r.ok?'拍照成功':'拍照失败')})}
function V(){var b=document.getElementById('vb');var on=b.textContent.indexOf('停止')>=0;
fetch('/record?action='+(on?'stop':'start')).then(function(r){return r.text()}).then(function(t){t=t.trim();
if(t.indexOf('REC')===0){b.textContent='⏹ 停止录像';L('设备录像中...');}
else if(t.indexOf('STOP')===0){b.textContent='⏺ 设备录像';L('设备录像已停止');}
else{L('录像: '+t)}}).catch(function(){L('请求失败')})}
</script></body></html>)");
    return h;
}

/**
 * Generate settings configuration page HTML
 * Includes sliders for brightness, contrast, saturation, white balance,
 * resolution, quality, digital zoom, exposure/shutter, anti-shake and scene modes.
 */
String htmlGetConfigPage(int brightness, int contrast, int saturation, int wbMode,
    int frameSize, int quality, bool hmirror, bool vflip,
    int zoomLevel, int exposureMode, int shutterValue, int gainValue,
    bool antiShake, int sceneMode, int shotDelaySec, bool watermark,
    bool previewJpeg, bool hwZoom, const char *uploadUrl, bool uploadAuto,
    int videoFps, bool previewNotice)
{
    String h = F(R"(<!DOCTYPE html><html lang='zh'><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1.0'>
<title>设置</title><style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Microsoft YaHei','PingFang SC',sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;padding:20px}
.w{max-width:500px;margin:0 auto;background:#fff;border-radius:20px;padding:30px;box-shadow:0 20px 60px rgba(0,0,0,.3)}
h1{text-align:center;margin-bottom:24px;font-size:1.4em;color:#333}
.g{margin-bottom:18px}.g label{display:block;margin-bottom:4px;font-weight:600;font-size:.85em;color:#555}
.g input[type=range]{width:100%}.g span{font-size:.78em;color:#999}
.g select{width:100%;padding:8px;border-radius:8px;border:1px solid #ddd;font-size:.88em}
.g input[type=url]{width:100%;padding:8px;border-radius:8px;border:1px solid #ddd;font-size:.88em}
.cb{margin-bottom:12px}
.nt{background:#fff4d6;color:#8a6d3b;border:1px solid #ffe58f;border-radius:10px;padding:10px 12px;margin-bottom:16px;font-size:.82em}
.bt{display:flex;gap:10px;margin-top:20px}
.bt button,.bt a{flex:1;padding:11px;border:none;border-radius:12px;font-size:.9em;font-weight:600;cursor:pointer;text-align:center;text-decoration:none}
.btn1{background:linear-gradient(135deg,#667eea,#764ba2);color:#fff}
.btn2{background:#e0e0e0;color:#333}</style></head><body>
<div class='w'><h1>⚙ 相机设置</h1>)");

    // 预览模式/硬件变焦变更提示（在页面顶部显示）
    if (previewNotice) h += F("<div class='nt'>预览模式变更将在下次进入预览/重启后生效</div>");
    h += F("<form method='POST' action='/config'>");

    // Camera parameters: brightness, contrast, saturation
    h += F("<div class='g'><label>亮度 (");
    h += String(brightness); h += F(")</label>");
    h += F("<input type='range' name='brightness' min='-2' max='2' value='");
    h += String(brightness); h += F("' oninput='this.nextElementSibling.textContent=this.value'>");
    h += F("<span>"); h += String(brightness); h += F("</span></div>");

    h += F("<div class='g'><label>对比度 (");
    h += String(contrast); h += F(")</label>");
    h += F("<input type='range' name='contrast' min='-2' max='2' value='");
    h += String(contrast); h += F("' oninput='this.nextElementSibling.textContent=this.value'>");
    h += F("<span>"); h += String(contrast); h += F("</span></div>");

    h += F("<div class='g'><label>饱和度 (");
    h += String(saturation); h += F(")</label>");
    h += F("<input type='range' name='saturation' min='-2' max='2' value='");
    h += String(saturation); h += F("' oninput='this.nextElementSibling.textContent=this.value'>");
    h += F("<span>"); h += String(saturation); h += F("</span></div>");

    // White balance mode selection
    h += F("<div class='g'><label>白平衡</label>");
    h += F("<select name='wbMode'>");
    h += F("<option value='0'"); if (wbMode == 0) h += F(" selected");
    h += F(">自动</option>");
    h += F("<option value='1'"); if (wbMode == 1) h += F(" selected");
    h += F(">晴天</option>");
    h += F("<option value='2'"); if (wbMode == 2) h += F(" selected");
    h += F(">阴天</option>");
    h += F("<option value='3'"); if (wbMode == 3) h += F(" selected");
    h += F(">办公室</option>");
    h += F("<option value='4'"); if (wbMode == 4) h += F(" selected");
    h += F(">家庭</option></select></div>");

    // Photo resolution selection
    h += F("<div class='g'><label>拍照分辨率</label>");
    h += F("<select name='frameSize'>");
    String sizes[] = {"QVGA(320x240)", "VGA(640x480)", "SVGA(800x600)", "XGA(1024x768)", "SXGA(1280x1024)", "UXGA(1600x1200)"};
    // 直接引用 esp32-camera 的枚举值：原先写死的 {4,10,8,12,9,13} 是旧版本数值，
    // 会导致选 UXGA 实际设成 HD、且下拉框无选中项、保存时被改成第一项
    int vals[] = {FRAMESIZE_QVGA, FRAMESIZE_VGA, FRAMESIZE_SVGA, FRAMESIZE_XGA, FRAMESIZE_SXGA, FRAMESIZE_UXGA};
    for (int i = 0; i < 6; i++) {
        h += F("<option value='"); h += String(vals[i]); h += F("'");
        if (frameSize == vals[i]) h += F(" selected");
        h += F(">"); h += sizes[i]; h += F("</option>");
    }
    h += F("</select></div>");

    // JPEG quality slider
    h += F("<div class='g'><label>JPEG质量 (");
    h += String(quality); h += F(")</label>");
    h += F("<input type='range' name='quality' min='4' max='15' value='");
    h += String(quality); h += F("' oninput='this.nextElementSibling.textContent=this.value'>");
    h += F("<span>"); h += String(quality); h += F("</span></div>");

    // Mirror and flip checkboxes
    h += F("<div class='cb'>"); h += checkbox("hmirror", "水平镜像", hmirror); h += F("</div>");
    h += F("<div class='cb'>"); h += checkbox("vflip", "垂直翻转", vflip); h += F("</div>");

    // ===== 镜头与算法：数字变焦 / 场景优化 / 手持防抖 =====
    h += F("<hr style='margin:16px 0;border-color:#eee'>");
    h += F("<h3 style='font-size:.95em;margin-bottom:10px;color:#555'>镜头与算法</h3>");

    h += F("<div class='g'><label>数字变焦</label><select name='zoomLevel'>");
    const char *zooms[] = {"1x（原始视角）", "2x", "3x", "4x"};
    for (int i = 0; i < 4; i++) {
        h += F("<option value='"); h += String(i + 1); h += F("'");
        if (zoomLevel == i + 1) h += F(" selected");
        h += F(">"); h += zooms[i]; h += F("</option>");
    }
    h += F("</select><span>预览实时生效；拍照按同视角裁剪保存</span></div>");

    h += F("<div class='g'><label>场景优化</label><select name='sceneMode'>");
    const char *scenes[] = {"标准", "夜间增强", "逆光补偿"};
    for (int i = 0; i < 3; i++) {
        h += F("<option value='"); h += String(i); h += F("'");
        if (sceneMode == i) h += F(" selected");
        h += F(">"); h += scenes[i]; h += F("</option>");
    }
    h += F("</select><span>夜间增强会自动拉高增益与降噪，逆光补偿防止主体发黑</span></div>");

    h += F("<div class='cb'>"); h += checkbox("antiShake", "手持防抖（连拍 3 张选最清晰）", antiShake); h += F("</div>");

    // 自拍倒计时（0=关 / 2 / 5 / 10 秒）
    h += F("<div class='g'><label>自拍倒计时</label><select name='shotDelay'>");
    const char *delays[] = {"关", "2 秒", "5 秒", "10 秒"};
    int delayVals[] = {0, 2, 5, 10};
    for (int i = 0; i < 4; i++) {
        h += F("<option value='"); h += String(delayVals[i]); h += F("'");
        if (shotDelaySec == delayVals[i]) h += F(" selected");
        h += F(">"); h += delays[i]; h += F("</option>");
    }
    h += F("</select><span>触发拍照后延迟执行，期间屏幕逐秒显示</span></div>");

    // 时间水印（右下角时间戳）
    h += F("<div class='cb'>"); h += checkbox("watermark", "时间水印（右下角时间戳，变焦时含倍率）", watermark); h += F("</div>");

    // ===== 曝光 / 快门 =====
    h += F("<hr style='margin:16px 0;border-color:#eee'>");
    h += F("<h3 style='font-size:.95em;margin-bottom:10px;color:#555'>曝光 / 快门</h3>");

    h += F("<div class='g'><label>曝光模式</label><select name='exposureMode'>");
    h += F("<option value='0'"); if (exposureMode == 0) h += F(" selected"); h += F(">自动（推荐日常）</option>");
    h += F("<option value='1'"); if (exposureMode == 1) h += F(" selected"); h += F(">手动快门</option>");
    h += F("</select><span>手动模式可冻结运动、减少手抖糊片</span></div>");

    h += F("<div class='g'><label>快门 / 曝光值 (");
    h += String(shutterValue); h += F(")</label>");
    h += F("<input type='range' name='shutterValue' min='0' max='1200' step='10' value='");
    h += String(shutterValue); h += F("' oninput='this.nextElementSibling.textContent=this.value'>");
    h += F("<span>"); h += String(shutterValue); h += F("</span></div>");

    h += F("<div class='g'><label>增益 / ISO (");
    h += String(gainValue); h += F(")</label>");
    h += F("<input type='range' name='gainValue' min='0' max='30' value='");
    h += String(gainValue); h += F("' oninput='this.nextElementSibling.textContent=this.value'>");
    h += F("<span>"); h += String(gainValue); h += F("</span></div>");

    // ===== 高级：预览模式 / 硬件变焦 / 照片上传 / 录像帧率 =====
    h += F("<hr style='margin:16px 0;border-color:#eee'>");
    h += F("<h3 style='font-size:.95em;margin-bottom:10px;color:#555'>高级 / 上传</h3>");

    h += F("<div class='cb'>");
    h += checkbox("previewJpeg", "JPEG 预览模式（降温/可提帧率，需重启预览生效）", previewJpeg);
    h += F("</div>");
    h += F("<div class='cb'>");
    h += checkbox("hwZoom", "硬件窗口变焦（拍照/推流用，实验性；预览仍用软件变焦）", hwZoom);
    h += F("</div>");

    // 照片上传地址（multipart 的字段名固定为 photo，兼容 Telegram sendPhoto）
    h += F("<div class='g'><label>照片上传地址</label>");
    h += F("<input type='url' name='uploadUrl' maxlength='190' placeholder='https://...' value='");
    h += String(uploadUrl);
    h += F("'>");
    h += F("<span>Telegram 用法：https://api.telegram.org/bot&lt;token&gt;/sendPhoto?chat_id=&lt;id&gt;"
           "（字段名固定为 photo，chat_id 放在查询串里）</span></div>");
    h += F("<div class='cb'>");
    h += checkbox("uploadAuto", "拍照后自动上传", uploadAuto);
    h += F("</div>");

    // 机内录像帧率（设备端录像使用）
    h += F("<div class='g'><label>机内录像帧率</label><select name='videoFps'>");
    const int fpsVals[] = {5, 10, 15};
    for (int i = 0; i < 3; i++) {
        h += F("<option value='"); h += String(fpsVals[i]); h += F("'");
        if (videoFps == fpsVals[i]) h += F(" selected");
        h += F(">"); h += String(fpsVals[i]); h += F(" fps</option>");
    }
    h += F("</select></div>");

    // Submit and cancel buttons
    h += F("<div class='bt'><button class='btn1'>💾 保存</button>");
    h += F("<a href='/' class='btn2'>取消</a></div>");
    h += F("</form></div></body></html>");
    return h;
}

/**
 * Generate web OTA firmware update page（/update）
 * 提供 .bin 文件选择与提交表单，升级期间页面提示"上传中，请勿断电"
 */
String htmlGetUpdatePage() {
    String h = F(R"(<!DOCTYPE html><html lang='zh'><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1.0'>
<title>固件升级</title><style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Microsoft YaHei','PingFang SC',sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px;color:#333}
.w{max-width:500px;width:100%;background:#fff;border-radius:20px;padding:30px;box-shadow:0 20px 60px rgba(0,0,0,.3)}
h1{text-align:center;margin-bottom:10px;font-size:1.4em}
.s{color:#999;text-align:center;margin-bottom:20px;font-size:.85em}
input[type=file]{width:100%;padding:10px;border:1px dashed #bbb;border-radius:10px;font-size:.85em;margin-bottom:16px}
.bt{display:flex;gap:10px}
.bt button,.bt a{flex:1;padding:11px;border:none;border-radius:12px;font-size:.9em;font-weight:600;cursor:pointer;text-align:center;text-decoration:none}
.btn1{background:linear-gradient(135deg,#667eea,#764ba2);color:#fff}
.btn2{background:#e0e0e0;color:#333}
.nt{margin-top:14px;font-size:.82em;color:#e67e22;text-align:center;display:none}
</style></head><body>
<div class='w'><h1>⬆ 固件升级</h1>
<p class='s'>选择 .bin 固件文件后提交，升级期间请勿断电</p>
<form method='POST' action='/update' enctype='multipart/form-data'
 onsubmit="document.getElementById('nt').style.display='block';document.getElementById('sb').disabled=true">
<input type='file' name='firmware' accept='.bin' required>
<div class='bt'><button class='btn1' id='sb'>开始升级</button><a href='/' class='btn2'>取消</a></div>
<div class='nt' id='nt'>上传中，请勿断电...</div>
</form></div></body></html>)");
    return h;
}

/**
 * Generate one status row（label + value），与首页 SD 卡片风格一致
 */
static String statusRow(const char *label, const String &value) {
    String s = F("<div class='st'><span>");
    s += label;
    s += F("</span><span class='sv'>");
    s += value;
    s += F("</span></div>");
    return s;
}

/**
 * Generate runtime status page HTML（/status）
 * 展示可用堆、PSRAM 余量、SD 容量、WiFi RSSI、相机模式、预览帧率、
 * 芯片温度、运行时长与累计拍照数，并提供刷新按钮。
 */
String htmlGetStatusPage(uint32_t freeHeap, uint32_t freePsram,
    bool sdMounted, float sdTotalMB, float sdUsedMB, int rssi,
    const char *cameraMode, float fps, float tempC,
    unsigned long uptimeSec, uint32_t photoCount, const String &videoHtml)
{
    String h = F(R"(<!DOCTYPE html><html lang='zh'><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1.0'>
<meta http-equiv='refresh' content='10'>
<title>运行状态</title><style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Microsoft YaHei','PingFang SC',sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px;color:#333}
.c{background:#fff;border-radius:20px;padding:32px 28px;box-shadow:0 20px 60px rgba(0,0,0,.3);max-width:520px;width:100%}
h1{text-align:center;font-size:1.4em;margin-bottom:6px}
.s{color:#999;text-align:center;margin-bottom:18px;font-size:.85em}
.inf{padding:6px 0;font-size:.86em}
.st{display:flex;justify-content:space-between;padding:8px 10px;margin:4px 0;background:#f5f5f5;border-radius:8px}
.sv{font-weight:600;font-family:monospace}
.r{display:flex;gap:10px;justify-content:center;margin-top:18px}
.b{display:inline-block;color:#fff;text-decoration:none;padding:10px 22px;border-radius:25px;font-size:.88em;font-weight:600;border:none;cursor:pointer}
.bst{background:linear-gradient(135deg,#11998e,#38ef7d)}
.bg{background:linear-gradient(135deg,#667eea,#764ba2)}</style></head><body>
<div class='c'><h1>📊 运行状态</h1>
<p class='s'>每 10 秒自动刷新</p><div class='inf'>)");

    // 内存：可用堆 / PSRAM 余量
    h += statusRow("可用堆内存", String(freeHeap / 1024.0f, 1) + " KB");
    h += statusRow("PSRAM 余量", String(freePsram / 1024.0f, 1) + " KB");

    // SD 容量：未挂载时显示 "--"
    if (sdMounted) {
        h += statusRow("SD 总容量", String(sdTotalMB, 1) + " MB");
        h += statusRow("SD 已用", String(sdUsedMB, 1) + " MB");
    } else {
        h += statusRow("SD 总容量", "--");
        h += statusRow("SD 已用", "--");
    }

    // WiFi 信号强度（dBm）
    h += statusRow("WiFi RSSI", String(rssi) + " dBm");
    // 当前相机模式
    h += statusRow("相机模式", cameraMode);
    // 预览帧率
    h += statusRow("预览帧率", String(fps, 1) + " fps");
    // 芯片温度（读取失败时显示 "--"）
    h += statusRow("芯片温度", isnan(tempC) ? String("--") : String(tempC, 1) + " °C");

    // 运行时长 HH:MM:SS
    unsigned long up = uptimeSec;
    char upBuf[16];
    snprintf(upBuf, sizeof(upBuf), "%02lu:%02lu:%02lu",
             up / 3600, (up / 60) % 60, up % 60);
    h += statusRow("运行时长", upBuf);
    // 累计拍照数
    h += statusRow("累计拍照", String(photoCount) + " 张");

    // 已录制 AVI 文件列表（文件名 + 下载链接），内容由调用方遍历 SD 生成
    h += F("</div><div class='inf'><h3>🎬 录像文件</h3>");
    h += videoHtml;
    h += F("</div><div class='r'>");
    h += F("<button class='b bst' onclick='location.reload()'>🔄 刷新</button>");
    h += F("<a href='/' class='b bg'>← 首页</a>");
    h += F("<a href='/update' class='b bst'>⬆ 固件升级</a></div></div></body></html>");
    return h;
}

/**
 * Generate one recorded-video row（文件名 + 下载链接），沿用状态页 .st/.sv 样式
 */
String htmlGetVideoRow(const String &name) {
    String h = F("<div class='st'><span>");
    h += name;
    h += F("</span><span class='sv'><a href='/video/");
    h += name;
    h += F("' style='color:#667eea;text-decoration:none'>下载</a></span></div>");
    return h;
}

/**
 * Generate gallery page HTML header with SD card statistics
 */
String htmlGetGalleryHeader(float totalMB, float usedMB, float freeMB) {
    String h = F(R"(<!DOCTYPE html><html lang='zh'><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1.0'><title>图库</title>
<style>*{margin:0;padding:0;box-sizing:border-box}body{font-family:'Microsoft YaHei','PingFang SC',sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;padding:20px}
.c{max-width:1100px;margin:0 auto}.h{text-align:center;color:#fff;margin-bottom:22px}
.h h1{font-size:1.8em;margin-bottom:6px}
.h a{color:#fff;text-decoration:none;background:rgba(255,255,255,.2);padding:8px 18px;border-radius:20px;display:inline-block;margin:4px}
.st{background:rgba(255,255,255,.95);border-radius:14px;padding:14px;margin-bottom:22px;text-align:center;font-size:.88em}
.g{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:14px}
.pc{background:#fff;border-radius:14px;overflow:hidden;box-shadow:0 8px 24px rgba(0,0,0,.2)}
.pc img{width:100%;height:170px;object-fit:cover;display:block}
.pc .info{padding:12px}.pc .nm{font-size:.8em;color:#444;word-break:break-all;margin-bottom:8px}
.pc .ac{display:flex;gap:8px}
.ab{flex:1;padding:7px 10px;border:none;border-radius:8px;font-size:.8em;cursor:pointer;text-align:center;text-decoration:none;display:inline-block;color:#fff}
.dl{background:linear-gradient(135deg,#667eea,#764ba2)}
.del{background:#e74c3c}
.up{background:linear-gradient(135deg,#43e97b,#38f9d7);color:#333}
.em{text-align:center;color:#fff;padding:40px;font-size:1em}</style></head><body>
<div class='c'><div class='h'><h1>📷 照片图库</h1>
<a href='/'>← 首页</a><a href='/stream'>📺 实时画面</a></div>)");

    h += F("<div class='st'>总: <b>"); h += String(totalMB, 1); h += F(" MB</b> | 已用: <b>");
    h += String(usedMB, 1); h += F(" MB</b> | 可用: <b style='color:#27ae60'>");
    h += String(freeMB, 1); h += F(" MB</b></div><div class='g'>");
    return h;
}

/**
 * Generate HTML card for a single photo with thumbnail, download, and delete buttons
 */
String htmlGetPhotoCard(const String &name) {
    String h = F("<div class='pc'><img src='/photo/"); h += name;
    h += F("'><div class='info'><div class='nm'>"); h += name; h += F("</div><div class='ac'>");
    h += F("<a href='/photo/"); h += name; h += F("' class='ab dl' download>下载</a>");
    h += F("<button class='ab up' onclick=\"fetch('/upload?f="); h += name;
    h += F("').then(function(r){return r.text()}).then(function(t){alert(t)})\">上传</button>");
    h += F("<button class='ab del' onclick=\"if(confirm('删除 ");
    h += name; h += F("?'))fetch('/delete/"); h += name;
    h += F("',{method:'DELETE'}).then(function(r){if(r.ok)location.reload()})\">删除</button></div></div></div>");
    return h;
}

/**
 * Generate empty gallery message when no photos exist
 */
String htmlGetEmptyGallery() { return F("<div class='em'>📭 还没有照片</div>"); }

/**
 * Generate gallery page footer and closing HTML tags
 */
String htmlGetGalleryFooter() { return F("</div></div></body></html>"); }
