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
 */

#include "html_pages.h"

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
<a href='/list' class='b bg'>🖼 图库</a>
<a href='/config' class='b bx'>⚙ 设置</a></div>
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
</script></body></html>)");
    return h;
}

/**
 * Generate settings configuration page HTML
 * Includes sliders for brightness, contrast, saturation, white balance,
 * resolution, quality, and toggles for motion detection and timelapse.
 */
String htmlGetConfigPage(int brightness, int contrast, int saturation, int wbMode,
    int frameSize, int quality, bool hmirror, bool vflip,
    bool motionEnabled, int motionThreshold,
    bool timelapseEnabled, int timelapseInterval)
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
.cb{margin-bottom:12px}
.bt{display:flex;gap:10px;margin-top:20px}
.bt button,.bt a{flex:1;padding:11px;border:none;border-radius:12px;font-size:.9em;font-weight:600;cursor:pointer;text-align:center;text-decoration:none}
.btn1{background:linear-gradient(135deg,#667eea,#764ba2);color:#fff}
.btn2{background:#e0e0e0;color:#333}</style></head><body>
<div class='w'><h1>⚙ 相机设置</h1>
<form method='POST' action='/config'>)");

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
    int vals[] = {4, 10, 8, 12, 9, 13};
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

    // Motion detection settings
    h += F("<hr style='margin:16px 0;border-color:#eee'>");
    h += F("<h3 style='font-size:.95em;margin-bottom:10px;color:#555'>运动检测</h3>");
    h += F("<div class='cb'>"); h += checkbox("motionEnabled", "启用运动检测", motionEnabled); h += F("</div>");
    h += F("<div class='g'><label>灵敏度 (");
    h += String(motionThreshold); h += F(")</label>");
    h += F("<input type='range' name='motionThreshold' min='5' max='50' value='");
    h += String(motionThreshold); h += F("' oninput='this.nextElementSibling.textContent=this.value'>");
    h += F("<span>"); h += String(motionThreshold); h += F("</span></div>");

    // Timelapse settings
    h += F("<hr style='margin:16px 0;border-color:#eee'>");
    h += F("<h3 style='font-size:.95em;margin-bottom:10px;color:#555'>延时摄影</h3>");
    h += F("<div class='cb'>"); h += checkbox("timelapseEnabled", "启用延时摄影", timelapseEnabled); h += F("</div>");
    h += F("<div class='g'><label>间隔 (");
    h += String(timelapseInterval / 1000); h += F("秒)</label>");
    h += F("<input type='range' name='timelapseInterval' min='5000' max='3600000' step='1000' value='");
    h += String(timelapseInterval); h += F("' oninput='this.nextElementSibling.textContent=Math.round(this.value/1000)+&quot;秒&quot;'>");
    h += F("<span>"); h += String(timelapseInterval / 1000); h += F("秒</span></div>");

    // Submit and cancel buttons
    h += F("<div class='bt'><button class='btn1'>💾 保存</button>");
    h += F("<a href='/' class='btn2'>取消</a></div>");
    h += F("</form></div></body></html>");
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
