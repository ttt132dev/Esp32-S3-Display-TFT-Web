# ESP32-S3 TFT Display 

Hệ thống điều khiển màn hình hiển thị (TFT / LCD / OLED qua `TFT_eSPI` + `TJpg_Decoder`) trên **ESP32-S3**, kết hợp giao diện web 1 trang 2 cột và trạm trung gian **Python (Flask + OpenCV + FFmpeg + yt-dlp + mss)** để phát video/ảnh từ link web hoặc chiếu màn hình máy tính.

## Tính năng chính
- **Cấu hình WiFi trực tiếp trên web:** Khởi động chế độ phát WiFi (`ESP32-S3-Display`) để quét mạng xung quanh, nhập SSID/mật khẩu, lưu vào `Preferences` và tự tắt WiFi phát cá nhân sau khi kết nối router thành công.
- **Giao diện 1 trang 2 cột:** Cột trái cấu hình WiFi & xem thông tin IP; cột phải nhập IP máy tính chạy Python, dán URL media, bấm **Play / Chieu Man Hinh / Stop**.
- **Tự thích ứng kích thước màn hình:** Tự căn giữa thông tin trạng thái, cấp phát bộ đệm JPEG theo kích thước màn hình và gửi kèm tham số `w`, `h` lên server Python để căn tỉ lệ khung hình.
- **Chống trễ luồng phát:** Đọc liên tục khung hình JPEG từ luồng MJPEG và xử lý sự kiện web ngay trong vòng lặp phát.

## Cấu trúc dự án
- `src/main.cpp`: Firmware ESP32-S3 (WiFi, WebServer, `TFT_eSPI`, `TJpg_Decoder`).
- `platformio.ini`: Cấu hình board ESP32-S3, chân giao tiếp và thư viện.
- `video.py`: Server Flask bóc tách link media, chụp màn hình PC và xuất luồng MJPEG.

## Cách chạy nhanh
**Trên máy tính (Python):** Cài các thư viện `flask`, `opencv-python`, `numpy`, `yt-dlp`, `imageio-ffmpeg`, `mss` rồi chạy:
   ```bash
   python video.py