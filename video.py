import re
import subprocess
import threading
import time
import urllib.parse
import urllib.request
import cv2
import imageio_ffmpeg
import mss
import numpy as np
import yt_dlp
from flask import Flask, Response, request

app = Flask(__name__)

DEFAULT_HEADERS = {
    'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36'
}

def mjpeg_packet(jpg_bytes):
    return b'--frame\r\nContent-Type: image/jpeg\r\n\r\n' + jpg_bytes + b'\r\n'

def fit_and_fix_color(frame, target_w=240, target_h=240):
    target_w = max(32, int(target_w))
    target_h = max(32, int(target_h))
    h, w = frame.shape[:2]
    scale = min(target_w / w, target_h / h)
    nw, nh = max(1, int(w * scale)), max(1, int(h * scale))
    resized = cv2.resize(frame, (nw, nh), interpolation=cv2.INTER_AREA)

    canvas = np.zeros((target_h, target_w, 3), dtype=np.uint8)
    x0, y0 = (target_w - nw) // 2, (target_h - nh) // 2
    canvas[y0:y0 + nh, x0:x0 + nw] = resized
    return cv2.cvtColor(canvas, cv2.COLOR_BGR2RGB)

def stream_pc_screen(target_w=240, target_h=240):
    with mss.mss() as sct:
        monitor = sct.monitors[1]
        while True:
            t0 = time.time()
            frame = np.array(sct.grab(monitor))[:, :, :3]
            _, buf = cv2.imencode('.jpg', fit_and_fix_color(frame, target_w, target_h), [int(cv2.IMWRITE_JPEG_QUALITY), 75])
            yield mjpeg_packet(buf.tobytes())
            dt = time.time() - t0
            if dt < 0.045:
                time.sleep(0.045 - dt)

def fetch_image(img_url, referer):
    headers = dict(DEFAULT_HEADERS)
    headers['Referer'] = referer
    req = urllib.request.Request(img_url, headers=headers)
    with urllib.request.urlopen(req, timeout=15) as resp:
        data = resp.read()
        ctype = resp.headers.get('Content-Type', '').lower()
        if 'image' not in ctype and not any(ext in img_url.lower() for ext in ['.jpg', '.jpeg', '.png', '.webp', '.bmp']):
            return None
        return cv2.imdecode(np.frombuffer(data, np.uint8), cv2.IMREAD_COLOR)

def resolve_media(page_url):
    headers = dict(DEFAULT_HEADERS)
    headers['Referer'] = page_url
    base_url = page_url.lower().split('?')[0]

    if any(ext in base_url for ext in ['.jpg', '.jpeg', '.png', '.webp', '.bmp']):
        img = fetch_image(page_url, page_url)
        if img is not None:
            return 'image', img

    if any(ext in base_url for ext in ['.mp4', '.m3u8', '.avi', '.mkv', '.webm']):
        return 'video', (page_url, headers)

    try:
        img = fetch_image(page_url, page_url)
        if img is not None:
            return 'image', img
    except Exception:
        pass

    try:
        ydl_opts = {
            'format': 'best[height<=480]/best',
            'quiet': True,
            'noplaylist': True,
            'http_headers': headers,
            'extractor_args': {'youtube': {'player_client': ['android', 'ios']}}
        }
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            info = ydl.extract_info(page_url, download=False)
            vheaders = info.get('http_headers', headers)
            if 'url' in info:
                return 'video', (info['url'], vheaders)
            for f in reversed(info.get('formats') or []):
                if f.get('vcodec') != 'none' and f.get('url'):
                    return 'video', (f['url'], f.get('http_headers', vheaders))
    except Exception:
        pass

    req = urllib.request.Request(page_url, headers=headers)
    html = urllib.request.urlopen(req, timeout=15).read().decode('utf-8', errors='ignore').replace('\\/', '/')

    vid_matches = re.findall(r'https?://[^\s"\'<>\\]+?\.(?:mp4|m3u8)(?:\?[^\s"\'<>\\]*)?', html)
    if vid_matches:
        return 'video', (vid_matches[0], headers)

    og = re.search(r'<meta[^>]+(?:property=["\']og:image["\'][^>]+content=|content=["\']([^"\']+)["\'][^>]+property=["\']og:image["\'])["\']?([^"\']*)', html, re.IGNORECASE)
    if og:
        raw_og = og.group(1) or og.group(2)
        if raw_og:
            img = fetch_image(urllib.parse.urljoin(page_url, raw_og), page_url)
            if img is not None:
                return 'image', img

    for raw_src in re.findall(r'<img[^>]+src=["\']([^"\']+)["\']', html, re.IGNORECASE):
        if raw_src.startswith('data:') or any(k in raw_src.lower() for k in ['logo', 'icon', 'avatar', '.svg']):
            continue
        try:
            img = fetch_image(urllib.parse.urljoin(page_url, raw_src), page_url)
            if img is not None and img.shape[0] >= 100 and img.shape[1] >= 100:
                return 'image', img
        except Exception:
            continue

    raise ValueError("Khong tim thay video hoac hinh anh hop le!")

def generate_frames(page_url, target_w=240, target_h=240):
    target_w = max(32, int(target_w))
    target_h = max(32, int(target_h))

    if page_url.strip().lower() == 'screen':
        yield from stream_pc_screen(target_w, target_h)
        return

    media_type, payload = resolve_media(page_url)

    if media_type == 'image':
        _, buf = cv2.imencode('.jpg', fit_and_fix_color(payload, target_w, target_h), [int(cv2.IMWRITE_JPEG_QUALITY), 85])
        pkt = mjpeg_packet(buf.tobytes())
        while True:
            yield pkt
            time.sleep(0.5)

    direct_url, headers = payload
    header_str = "".join(f"{k}: {v}\r\n" for k, v in headers.items())
    vf_filter = (
        f"scale={target_w}:{target_h}:force_original_aspect_ratio=decrease,"
        f"pad={target_w}:{target_h}:(ow-iw)/2:(oh-ih)/2:black,"
        f"colorchannelmixer=rr=0:rb=1:bb=0:br=1,format=yuvj420p"
    )
    cmd = [
        imageio_ffmpeg.get_ffmpeg_exe(),
        '-fflags', 'nobuffer+discardcorrupt',
        '-flags', 'low_delay',
        '-probesize', '32768',
        '-analyzeduration', '100000',
        '-re',
        '-headers', header_str,
        '-i', direct_url,
        '-vf', vf_filter,
        '-f', 'image2pipe',
        '-vcodec', 'mjpeg',
        '-q:v', '8',
        '-r', '20',
        '-'
    ]

    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=65536)
    state = {'jpg': None, 'running': True, 'event': threading.Event()}

    def reader():
        buf = b""
        while state['running']:
            chunk = process.stdout.read(4096)
            if not chunk:
                state['running'] = False
                state['event'].set()
                break
            buf += chunk
            while True:
                s = buf.find(b'\xff\xd8')
                e = buf.find(b'\xff\xd9', s + 2) if s != -1 else -1
                if s != -1 and e != -1:
                    state['jpg'] = buf[s:e + 2]
                    buf = buf[e + 2:]
                    state['event'].set()
                else:
                    break

    threading.Thread(target=reader, daemon=True).start()
    try:
        while state['running']:
            state['event'].wait(timeout=2.0)
            state['event'].clear()
            if state['jpg']:
                yield mjpeg_packet(state['jpg'])
    finally:
        state['running'] = False
        process.kill()

@app.route('/video')
def video():
    target_url = request.args.get('url', '')
    if not target_url:
        return "Thieu tham so url", 400
    w = request.args.get('w', 240, type=int)
    h = request.args.get('h', 240, type=int)
    return Response(generate_frames(target_url, w, h), mimetype='multipart/x-mixed-replace; boundary=frame')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, threaded=True)