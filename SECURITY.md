# Security Policy

## Báo cáo lỗ hổng bảo mật

Nếu bạn phát hiện lỗ hổng bảo mật trong project này (ví dụ: lỗ hổng xử lý file upload, injection trong truy vấn PostGIS, rò rỉ thông tin qua API...), vui lòng **không** tạo Issue công khai.

Thay vào đó, báo cáo riêng qua:
- GitHub: mở [Security Advisory riêng tư](../../security/advisories/new) cho repo này
- Hoặc liên hệ trực tiếp qua GitHub profile: [@Dinhthuy2k5](https://github.com/Dinhthuy2k5)

Vui lòng cung cấp:
- Mô tả lỗ hổng và ảnh hưởng tiềm ẩn
- Các bước để tái hiện lỗi
- Phiên bản/commit bị ảnh hưởng (nếu biết)

## Phạm vi

Project đang trong giai đoạn phát triển (đồ án cá nhân), chưa có bản release chính thức. Các thành phần liên quan đến bảo mật cần lưu ý:

- `cpp-core/` — xử lý file ảnh (.tif) do người dùng upload, cần cẩn trọng với input không tin cậy (buffer overflow, path traversal khi lưu file tạm...)
- `frontend/` — giao tiếp với API backend, cần validate input phía client lẫn server

## Thời gian phản hồi

Đây là project cá nhân nên không cam kết SLA cụ thể, nhưng sẽ cố gắng phản hồi trong vòng 2 ngày.